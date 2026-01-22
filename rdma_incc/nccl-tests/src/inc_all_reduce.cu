#include <iostream>
#include <unordered_map>
#include <csignal>
#include <mpi.h>
#include <cuda_runtime.h>
#include <infiniband/verbs.h>
#include <assert.h>

#include "common.h"
#define ENABLE_IB_UTILS
#include "net_utils.hpp"
#include "yyt_error.h"


#define UPDIV(X, Y) (((X) + (Y) - 1)/(Y))

#define Q_SIZE 128
#define PSN 0
#define LID 0
#define MTU IBV_MTU_256
#define MTU_SIZE (128*(1<<MTU))
#define QP_ACCESS_FLAGS (IBV_ACCESS_REMOTE_WRITE) // SEND is allowd by default, and WRITE/READ/ATOMIC is optional


#define MAX_SEGMENT_SIZE (2 << 20)
#define MIN_SEGMENT_SIZE 1 // (512 << 10) // I found it useless, so set to 1
#define DEFAULT_SPLIT_CNT 8
#define MAX_QP_NUM 2
#define SWITCH_MEMORY_SIZE_PER_QP (128 << 10)
#define SEGMENT_QUEUE_SIZE 8 
#define INC_BUF_SIZE (SEGMENT_QUEUE_SIZE * MAX_SEGMENT_SIZE) 


#define SERVER_WINDOW_SIZE_PER_QP (SWITCH_MEMORY_SIZE_PER_QP/2) // for retransmission
#define MAX_MSG_SIZE_PER_QP (SERVER_WINDOW_SIZE_PER_QP/2) // make a queue holds at least 2 messages
static_assert(MAX_MSG_SIZE_PER_QP % MTU_SIZE == 0);

#define SERVER_WINDOW_SIZE (SERVER_WINDOW_SIZE_PER_QP * MAX_QP_NUM)
#define MAX_MSG_SIZE (MAX_MSG_SIZE_PER_QP * MAX_QP_NUM)


#define GLOBALPRINT(...) do { fprintf(stderr, "[%d] ", print_rank);  fprintf(stderr, __VA_ARGS__); } while (0)

#define CUDACHECKEXIT(cmd) do {                                 \
    cudaError_t err = cmd;                                  \
    if( err != cudaSuccess ) {                              \
        GLOBALPRINT("Cuda failure '%s'", cudaGetErrorString(err)); \
        exit(err);                      \
    }                                                       \
} while(false)

struct agg_addr {
    // assume lid==0 && psn==0
    uint32_t ip;
    uint32_t rkey;
    int qpn[MAX_QP_NUM];
};

struct rdma_address {
    uint64_t remote_addr;    
    uint32_t rkey;
};

struct rdma_ctx_t {
    ibv_device *dev;
    ibv_pd *pd;
    ibv_mr *s_mr;
    ibv_mr *r_mr;
    ibv_qp *qp[MAX_QP_NUM];
    size_t sq_avail_wr[MAX_QP_NUM];
    size_t rq_avail_wr[MAX_QP_NUM];
    // size_t sq_avail_memory[MAX_QP_NUM];
    // size_t rq_avail_memory[MAX_QP_NUM];
    size_t sq_complete_seg[MAX_QP_NUM];
    size_t rq_complete_seg[MAX_QP_NUM];
};

// enum op_status_t {
//     OP_
// };

// struct op_t {
//     op_status_t status;
//     cudaEvent_t event;
// };

// unordered_map<ncclComm* comm, struct comm_ctx_t> *comm_ctx; //ncclComm* is ncclComm_t
 

rdma_ctx_t rdma_ctx;
ncclComm* prev_comm;
cudaStream_t prev_stream;
size_t msg_sent[SEGMENT_QUEUE_SIZE];
cudaEvent_t copy_event[SEGMENT_QUEUE_SIZE];
cudaStream_t h2d_stream, d2h_stream;
agg_addr exch_addr;
bool use_send;
int print_rank;// used for print info

vector<uint32_t> group_ids;

// grpc related functions
uint32_t inc_create_group(uint32_t *ip, int *qpn, uint32_t *rkey, int qp_count, int switch_memory_size, string controller_addr);
bool inc_destroy_group(uint32_t group_id);

string get_env(string env) {
    char* env_chr = getenv(env.data());
    if(env_chr == NULL) return string();
    return string(env_chr);
}

string get_env(string env, string default_val) {
    string ret = get_env(env);
    if(ret.empty()) ret = default_val;
    return ret;
}

inline int ncclTypeSize(ncclDataType_t type) {
  switch (type) {
  case ncclInt8:
  case ncclUint8:
    return 1;
  case ncclFloat16:
  #if defined(__CUDA_BF16_TYPES_EXIST__)
  case ncclBfloat16:
  #endif
    return 2;
  case ncclInt32:
  case ncclUint32:
  case ncclFloat32:
    return 4;
  case ncclInt64:
  case ncclUint64:
  case ncclFloat64:
    return 8;
  default:
    return -1;
  }
}


ibv_qp *init_qp(ibv_context *ctx, ibv_pd *pd, uint8_t port_id)
{
    int ret;
    // CQ
    ibv_cq *scq = ibv_create_cq(ctx, Q_SIZE, NULL, /*comp_channel*/NULL, 0);// use 0 as comp_vector
    ibv_cq *rcq = ibv_create_cq(ctx, Q_SIZE, NULL, /*comp_channel*/NULL, 0);// use 0 as comp_vector
    MYCHECK(scq == NULL, "Error on ibv_create_cq");
    MYCHECK(rcq == NULL, "Error on ibv_create_cq");
    struct ibv_qp_init_attr init_attr = {};
    init_attr.send_cq = scq;
    init_attr.recv_cq = rcq;
    init_attr.cap.max_send_wr  = Q_SIZE;
    init_attr.cap.max_recv_wr  = Q_SIZE;
    init_attr.cap.max_send_sge = 1;
    init_attr.cap.max_recv_sge = 1;
    init_attr.qp_type = IBV_QPT_RC;
        // sq_sig_all is not neccessary since we can set IBV_SEND_SIGNALED for each wr
    // ibv_req_notify_cq should be called before ibv_get_cq_event
    // once a Completion Notification occurred, ibv_req_notify_cq should be called again

    // QP
    ibv_qp *qp = ibv_create_qp(pd, &init_attr);
    MYCHECK(qp == NULL, "Error on ibv_create_qp");

    // Reset -> INIT
    struct ibv_qp_attr attr = {};
    attr.qp_state        = IBV_QPS_INIT;
    attr.pkey_index      = 0;
    attr.port_num        = port_id;
    attr.qp_access_flags = 0;

    ret = ibv_modify_qp(qp, &attr, IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS);
    MYCHECK(ret != 0, "Error on RESET->INIT");
    return qp;
}

void move_qp_to_rts(ibv_qp *qp, uint8_t port_id, uint8_t gid_index, ibv_gid remote_gid, uint32_t remote_qpn) 
{
    int ret;
    // INIT -> RTR
    struct ibv_qp_attr attr = {};
	attr.qp_state		= IBV_QPS_RTR;
	attr.path_mtu		= MTU;
	attr.dest_qp_num	= remote_qpn;
	attr.rq_psn			= PSN;// we assume remote side uses PSN as sq_psn
	attr.max_dest_rd_atomic	= 1;
	attr.min_rnr_timer		= 12;// wait for 0.64ms to send RNR NACK
	attr.ah_attr.is_global	= 0;
	attr.ah_attr.dlid		= LID;
    attr.ah_attr.sl		    = 0;
	attr.ah_attr.src_path_bits = 0;
	attr.ah_attr.port_num	= port_id;

    // attr.ah_attr.static_rate = IBV_RATE_2_5_GBPS;
    // GLOBALPRINT("Use 2.5Gbps link\n");
    attr.qp_access_flags = QP_ACCESS_FLAGS;

    if(remote_gid.global.interface_id) {
        attr.ah_attr.is_global = 1;
		attr.ah_attr.grh.hop_limit = 1;
		attr.ah_attr.grh.dgid = remote_gid;
		attr.ah_attr.grh.sgid_index = gid_index;
    }
    int attr_mask = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN | 
                    IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER | IBV_QP_ACCESS_FLAGS;
    // IBV_QP_ACCESS_FLAGS is optional ! But it is required for WRITE/READ/ATOMIC
    ret = ibv_modify_qp(qp, &attr, attr_mask);
    MYCHECK(ret != 0, "Error on INIT->RTR");
    // RTR -> RTS
    attr = {};
    attr.qp_state	    = IBV_QPS_RTS;
    // 18 will make the process really slow
	attr.timeout	    = 8;// 4=65us, 8=1ms, 14=67ms, 18=1s, 31=8800s, 0=INF
	attr.retry_cnt	    = 7;
	attr.rnr_retry	    = 7;// 7 means retry infinitely when RNR NACK is received
	attr.sq_psn	        = PSN;
	attr.max_rd_atomic  = 1;
    attr_mask = IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT |
			    IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC;
    ret = ibv_modify_qp(qp, &attr, attr_mask);
    MYCHECK(ret != 0, "Error on RTR->RTS");
}

void return_groups()
{
    for(auto i: group_ids) inc_destroy_group(i);
    GLOBALPRINT("groups returned\n");
    fflush(stdout);
}

void atexit_func()
{
    return_groups();
}

void signalHandler(int signum) {
    std::cout << "Signal received: " << signum << std::endl;
    atexit_func();
    exit(signum);
}

void exchange_addresses(agg_addr *exch_addr, string controller_addr)
{
    int root = 0;
    int rank, group_size;
    MPI_Comm_size(MPI_COMM_WORLD, &group_size);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    size_t mpi_data_size = sizeof(agg_addr);
    if(rank == root) {
        vector<agg_addr>addr_list(group_size);
        vector<vector<uint32_t>>ip(MAX_QP_NUM, vector<uint32_t>(group_size));
        vector<vector<int>>qpn(MAX_QP_NUM, vector<int>(group_size));
        vector<vector<uint32_t>>rkey(MAX_QP_NUM, vector<uint32_t>(group_size));

        MPI_Gather(exch_addr, mpi_data_size, MPI_CHAR, addr_list.data(), mpi_data_size, MPI_CHAR, 0, MPI_COMM_WORLD);

        // register
        for(int q = 0; q < MAX_QP_NUM; q++) {
            for(int r = 0; r < group_size; r++) {
                ip[q][r] = addr_list[r].ip;
                qpn[q][r] = addr_list[r].qpn[q];
                rkey[q][r] = addr_list[r].rkey;
            }
            uint32_t group_id = inc_create_group(ip[q].data(), qpn[q].data(), rkey[q].data(), group_size, (int)SWITCH_MEMORY_SIZE_PER_QP, controller_addr);
            if(group_id == 0) {// error or no resources
                GLOBALPRINT("No switch resources\n");
                return_groups();
                exit(1);
            }
            group_ids.push_back(group_id);
        }

        for(int r = 0; r < group_size; r++) {
            addr_list[r].ip = ip[0][r];// assert(ip[X][r] == ip[Y][r])
            addr_list[r].rkey = rkey[0][r];
            for(int q = 0; q < MAX_QP_NUM; q++) {
                addr_list[r].qpn[q] = qpn[q][r];
            }
        }
        
        MPI_Scatter(addr_list.data(), mpi_data_size, MPI_CHAR, exch_addr, mpi_data_size, MPI_CHAR, 0, MPI_COMM_WORLD);
    }
    else {
        MPI_Gather(exch_addr, mpi_data_size, MPI_CHAR, NULL, 0, 0, root, MPI_COMM_WORLD);
        MPI_Scatter(NULL, 0, 0, exch_addr, mpi_data_size, MPI_CHAR, root, MPI_COMM_WORLD);
    }
}


void init_rdma() 
{
    use_send = stoi(get_env("INC_USE_SEND", "0"));
    string bind_ip_str = get_env("INC_BIND_IP");// may have IP prefix
    string controller_addr = get_env("INC_CONTROLLER_ADDR");
    MYCHECK(bind_ip_str.empty(), "Require INC_BIND_IP");

    // Find NIC
    string dev_name;
    uint32_t bind_ip;
    std::tie(dev_name, bind_ip) = get_device_by_ip(bind_ip_str);
    MYCHECK(dev_name.empty(), "Error on get_device_by_ip");

    {
        struct in_addr in_a{.s_addr = htonl(bind_ip)};
        bind_ip_str = inet_ntoa(in_a);
    }
    

    // string ib_dev_name = dev_to_ib_dev(dev_name);// This does not work in container
    string ib_dev_name;
    ibv_device *dev = NULL;
    uint8_t port_id, gid_index;
    query_ib_device_by_ip(bind_ip, &dev, &port_id, &gid_index);
    MYCHECK(dev == NULL, "Error on query_ib_hardware_by_ip");

    ib_dev_name = ibv_get_device_name(dev);
    GLOBALPRINT("Find dev %d (%s), port_id %u, gid_index %u matches IP %s\n", ibv_get_device_index(dev), ib_dev_name.data(), port_id, gid_index, bind_ip_str.data());

    // Open device
    // show_ib_devices();
    GLOBALPRINT("Open device %s (%s)\n", ib_dev_name.c_str(), dev_name.c_str());
    // ibv_device *dev = find_ib_device(ib_dev_name);
    // MYCHECK(dev == NULL, "Error on find_device");
    ibv_context *ctx = ibv_open_device(dev);
    MYCHECK(ctx == NULL, "Error on ibv_open_device");

    // Alloc PD
    GLOBALPRINT("Alloc PD\n");
    ibv_pd *pd = ibv_alloc_pd(ctx);
    MYCHECK(pd == NULL, "Error on ibv_alloc_pd");

    // Register MR
    GLOBALPRINT("Register MR\n");
    void *inc_send_buf, *inc_recv_buf;
    CUDACHECKEXIT(cudaHostAlloc(&inc_send_buf, INC_BUF_SIZE, cudaHostAllocMapped | cudaHostAllocPortable));
    CUDACHECKEXIT(cudaHostAlloc(&inc_recv_buf, INC_BUF_SIZE, cudaHostAllocMapped | cudaHostAllocPortable));
    MYCHECK(inc_send_buf == NULL || inc_recv_buf == NULL, "Error on cudaHostAlloc");
    ibv_mr *s_mr = ibv_reg_mr_iova(pd, inc_send_buf, INC_BUF_SIZE, 0, IBV_ACCESS_LOCAL_WRITE | QP_ACCESS_FLAGS /* | IBV_ACCESS_REMOTE_READ*/);
    ibv_mr *r_mr = ibv_reg_mr_iova(pd, inc_recv_buf, INC_BUF_SIZE, 0, IBV_ACCESS_LOCAL_WRITE | QP_ACCESS_FLAGS /* | IBV_ACCESS_REMOTE_READ*/);
    MYCHECK(s_mr == NULL || r_mr == NULL, "Error on ibv_reg_mr");

    // Look up gid index 
    // GLOBALPRINT("Query port_id & gid_index\n");
    // uint8_t port_id, gid_index;
    // get_gid_index(bind_ip, ctx, &port_id, &gid_index);

    // Init QP
    GLOBALPRINT("Init QP\n");
    ibv_qp *qp[MAX_QP_NUM];
    for(int i = 0; i < MAX_QP_NUM; i++) qp[i] = init_qp(ctx, pd, port_id);

    // Exchange addresses
    GLOBALPRINT("Exchange addresses\n");
    exch_addr.ip = bind_ip;
    exch_addr.rkey = r_mr->rkey;
    for(int i = 0; i < MAX_QP_NUM; i++) exch_addr.qpn[i] = qp[i]->qp_num;

    GLOBALPRINT("local info: send_mem_addr %p, rkey %#x, recv_mem_addr %p, rkey %#x, gid %s\n", 
        s_mr->addr, s_mr->rkey, r_mr->addr, r_mr->rkey, gid_to_str(ipv4_to_gid(exch_addr.ip)).c_str());
    GLOBALPRINT("            qpn ");
    for(int i = 0; i < MAX_QP_NUM; i++) GLOBALPRINT("%#x%c", exch_addr.qpn[i], ",\n"[i==MAX_QP_NUM-1]);
    GLOBALPRINT("\n");

    exchange_addresses(&exch_addr, controller_addr);

    GLOBALPRINT("remote info: rkey %#x, gid %s\n", exch_addr.rkey, gid_to_str(ipv4_to_gid(exch_addr.ip)).c_str());
    GLOBALPRINT("             qpn ");
    for(int i = 0; i < MAX_QP_NUM; i++) GLOBALPRINT("%#x%c", exch_addr.qpn[i], ",\n"[i==MAX_QP_NUM-1]);
    GLOBALPRINT("\n");

    // Connect QP
    GLOBALPRINT("Connect QP\n");
    for(int i = 0; i < MAX_QP_NUM; i++) move_qp_to_rts(qp[i], port_id, gid_index, ipv4_to_gid(exch_addr.ip), exch_addr.qpn[i]);

    // Store rdma context
    rdma_ctx = {dev, pd, s_mr, r_mr};
    memcpy(rdma_ctx.qp, qp, sizeof(qp));
    for(int i = 0; i < MAX_QP_NUM; i++) {
        rdma_ctx.sq_avail_wr[i] = min(Q_SIZE, SERVER_WINDOW_SIZE_PER_QP / MAX_MSG_SIZE_PER_QP); // typically, this should be 2
        rdma_ctx.rq_avail_wr[i] = min(Q_SIZE, SERVER_WINDOW_SIZE_PER_QP / MAX_MSG_SIZE_PER_QP);
        // rdma_ctx.sq_avail_memory[i] = SERVER_WINDOW_SIZE_PER_QP;
        // rdma_ctx.rq_avail_memory[i] = SERVER_WINDOW_SIZE_PER_QP;
        rdma_ctx.sq_complete_seg[i] = 0;
        rdma_ctx.rq_complete_seg[i] = 0;
    }
}

void poll_cq(ibv_cq *cq, int *n_wr_complete, int *n_seg_complete)
{
    static struct ibv_wc wc[Q_SIZE];
    *n_wr_complete = 0;
    *n_seg_complete = 0;
    int ret = ibv_poll_cq(cq, Q_SIZE, wc);
    MYCHECK(ret < 0, "Error on ibv_poll_cq");
    for(int i = 0; i < ret; i++) {
        if(wc[i].status != IBV_WC_SUCCESS) {
            GLOBALPRINT("%d\n", (int)wc[i].status);
            MYCHECK(wc[i].status != IBV_WC_SUCCESS, "Error on ibv_poll_cq");
        }
        if(wc[i].wr_id != 0) {
            (*n_seg_complete) ++;// fuck: ++ has higher priority than *
        }
    }
    *n_wr_complete = ret;
}

// poll complete queues
// return: minimal available size of work queues
size_t poll_cqs()
{
    size_t available_wr = Q_SIZE;
    for(int q = 0; q < MAX_QP_NUM; q++) {
        int n_wr_complete, n_seg_complete;
        poll_cq(rdma_ctx.qp[q]->send_cq, &n_wr_complete, &n_seg_complete); 
        rdma_ctx.sq_avail_wr[q] += n_wr_complete;
        rdma_ctx.sq_complete_seg[q] += n_seg_complete;
        poll_cq(rdma_ctx.qp[q]->recv_cq, &n_wr_complete, &n_seg_complete);
        rdma_ctx.rq_avail_wr[q] += n_wr_complete;
        rdma_ctx.rq_complete_seg[q] += n_seg_complete;

        available_wr = min(available_wr, rdma_ctx.sq_avail_wr[q]);
        if(use_send) {
            available_wr = min(available_wr, rdma_ctx.rq_avail_wr[q]);
        }
    }
    return available_wr;
}

// wait until the available size of work queues >= required_size
// void wait_for_queue_available(size_t required_wr)
// {
//     size_t available_wr;
//     do {
//         available_wr = poll_cqs();
//     }
//     while(available_wr < required_wr);
// }

// Do not set notify=true, this is a deprecated argument
void post_message(ibv_qp *qp, ibv_sge *s_sge, ibv_sge *r_sge, rdma_address *rdma_addr, bool notify, uint64_t wr_id = 0)
{
    assert(s_sge->length != 0);// the switch can not handle a packet with no payload for allreduce
    int ret;
    if(use_send || notify) {
        struct ibv_recv_wr wr = {}, *bad_wr;
        wr.wr_id = wr_id;
        wr.next = NULL;
        wr.sg_list = r_sge;
        wr.num_sge = 1;
        ret = ibv_post_recv(qp, &wr, &bad_wr);
        if(ret != 0) GLOBALPRINT("%d\n", ret);
        MYCHECK(ret != 0, "Error on ibv_post_recv");
    }

    struct ibv_send_wr wr = {}, *bad_wr;
    wr.wr_id = wr_id;
    wr.next = NULL;
    wr.sg_list = s_sge;
    wr.num_sge = 1;
    wr.send_flags = IBV_SEND_SIGNALED;
    if(use_send) {
        wr.opcode = IBV_WR_SEND;
    }
    else {
        if(notify) {
            wr.opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
            wr.imm_data = htonl(0);
        }
        else {
            wr.opcode = IBV_WR_RDMA_WRITE;
        }
        memcpy(&wr.wr.rdma, rdma_addr, sizeof(rdma_address));
    }
    ret = ibv_post_send(qp, &wr, &bad_wr);
    MYCHECK(ret != 0, "Error on ibv_post_send");
}

/*
void post_allreduce(size_t offset, size_t size)
{
    static int cnt;
    // GLOBALPRINT("post_allreduce %d\n", ++cnt);
    // Only the last segment of an allreduce is not aligned.
    // Upper-aligned size is still <= seg_size so there is no memory overflow.
    // "size" should be at least MAX_QP_NUM * MTU_SIZE, otherwise, there will be zero-length messages for QPs, 
    // since the switch can not handle zero-payload packet, it will cause infinite retransmission, 
    // reported by poll_cq().
    constexpr size_t align_size = MAX_QP_NUM * MTU_SIZE;
    size = UPDIV(size, align_size) * align_size; 

    // struct ibv_sge s_sge = {
    //     .addr = (uint64_t)offset,  
    //     .length = (uint32_t)0,          
    //     .lkey = s_mr->lkey,
    // };
    // struct ibv_sge r_sge = {
    //     .addr = (uint64_t)offset, 
    //     .length = (uint32_t)0,          
    //     .lkey = r_mr->lkey,
    // };
    // struct rdma_address rdma_addr = {
    //     .remote_addr = (uint64_t)offset,
    //     .rkey = exch_addr.rkey,
    // }
    size_t nmsg = UPDIV(size, MAX_MSG_SIZE);
    
    // split memory into messages
    size_t msg_offset = offset;
    for(size_t msg_id = 0; msg_id < nmsg; msg_id ++) {
        bool is_last = msg_id == nmsg - 1;
        size_t msg_size = is_last ? size - (nmsg - 1) * MAX_MSG_SIZE : MAX_MSG_SIZE;
        // size_t msg_mtu = msg_size / MTU_SIZE;
        // size_t msg_mtu_per_qp_base = msg_mtu / MAX_QP_NUM;
        // size_t msg_mtu_remain = msg_mtu % MAX_QP_NUM;

        wait_for_queue_available(1);   

        size_t sub_offset = msg_offset;
        for(int q = 0; q < MAX_QP_NUM; q++) {
            // uint32_t sub_size = (msg_mtu_per_qp_base + (q < msg_mtu_remain)) * MTU_SIZE;
            uint32_t sub_size = msg_size / MAX_QP_NUM; 
            assert(sub_size > 0 && sub_size % MTU_SIZE == 0);

            struct ibv_sge s_sge = {
                .addr = (uint64_t)sub_offset,  
                .length = (uint32_t)sub_size,          
                .lkey = rdma_ctx.s_mr->lkey,
            };
            struct ibv_sge r_sge = {
                .addr = (uint64_t)sub_offset, 
                .length = (uint32_t)sub_size,          
                .lkey = rdma_ctx.r_mr->lkey,
            };
            struct rdma_address rdma_addr = {
                .remote_addr = (uint64_t)sub_offset,
                .rkey = exch_addr.rkey,
            };
            
            post_message(rdma_ctx.qp[q], &s_sge, &r_sge, &rdma_addr, false, is_last? 1: 0);// use wr_id == 1 to indicate the last message of post_allreduce

            rdma_ctx.sq_avail_wr[q] --;
            if(use_send) {
                rdma_ctx.rq_avail_wr[q] --;
            }

            sub_offset += sub_size;
        }
        msg_offset += msg_size;
    }
}
*/

bool poll_segment()
{
    for(int q = 0; q < MAX_QP_NUM; q++) {
        if(rdma_ctx.sq_complete_seg[q] == 0) 
            return false;
        if(use_send) {
            if(rdma_ctx.rq_complete_seg[q] == 0) 
                return false;
        } 
    }
    for(int q = 0; q < MAX_QP_NUM; q++) {
        rdma_ctx.sq_complete_seg[q] --;
        if(use_send) {
            rdma_ctx.rq_complete_seg[q] --;
        } 
    }
    return true;
}

void try_post_message(size_t seg_id_begin, size_t seg_id_end, size_t max_seg_size, size_t total_size)
{
    if(seg_id_begin == seg_id_end) return;
    size_t available_wr = poll_cqs();
    if(available_wr == 0) return;// no room for a message
    size_t seg_id, seg_index, seg_size;
    size_t msg_id, nmsg;

    // find a message to send
    for(seg_id = seg_id_begin; seg_id < seg_id_end; seg_id++) {
        seg_index = seg_id % SEGMENT_QUEUE_SIZE;
        msg_id = msg_sent[seg_index];
        seg_size = min(max_seg_size, total_size - max_seg_size * seg_id);
        nmsg = UPDIV(seg_size, MAX_MSG_SIZE);
        if(msg_id < nmsg) break;
    }
    if(seg_id == seg_id_end) return;
    
    size_t seg_offset = max_seg_size * seg_index;// buffer_offset
    constexpr size_t align_size = MAX_QP_NUM * MTU_SIZE;
    seg_size = UPDIV(seg_size, align_size) * align_size; 

    size_t inner_msg_offset = msg_id * MAX_MSG_SIZE;
    size_t msg_offset = seg_offset + inner_msg_offset;
    bool is_last = msg_id == nmsg - 1;
    size_t msg_size = min(seg_size - inner_msg_offset, (size_t)MAX_MSG_SIZE);

    size_t sub_offset = msg_offset;
    for(int q = 0; q < MAX_QP_NUM; q++) {
        // uint32_t sub_size = (msg_mtu_per_qp_base + (q < msg_mtu_remain)) * MTU_SIZE;
        uint32_t sub_size = msg_size / MAX_QP_NUM; 
        assert(sub_size > 0 && sub_size % MTU_SIZE == 0);

        struct ibv_sge s_sge = {
            .addr = (uint64_t)sub_offset,  
            .length = (uint32_t)sub_size,          
            .lkey = rdma_ctx.s_mr->lkey,
        };
        struct ibv_sge r_sge = {
            .addr = (uint64_t)sub_offset, 
            .length = (uint32_t)sub_size,          
            .lkey = rdma_ctx.r_mr->lkey,
        };
        struct rdma_address rdma_addr = {
            .remote_addr = (uint64_t)sub_offset,
            .rkey = exch_addr.rkey,
        };
        
        post_message(rdma_ctx.qp[q], &s_sge, &r_sge, &rdma_addr, false, is_last? 1: 0);// use wr_id == 1 to indicate the last message of post_allreduce

        rdma_ctx.sq_avail_wr[q] --;
        if(use_send) {
            rdma_ctx.rq_avail_wr[q] --;
        }

        sub_offset += sub_size;
    }
    msg_sent[seg_index] ++;
}

void init_cuda()
{
    CUDACHECKEXIT(cudaStreamCreate(&h2d_stream));
    CUDACHECKEXIT(cudaStreamCreate(&d2h_stream));
    for(size_t i = 0; i < SEGMENT_QUEUE_SIZE; i++) CUDACHECKEXIT(cudaEventCreate(&copy_event[i]));
}

void init_atexit()
{
    signal(SIGINT, signalHandler);
    signal(SIGQUIT, signalHandler);
    signal(SIGTERM, signalHandler);
    MYCHECK(atexit(atexit_func) != 0, "Error on atexit");
}
// Make sure:
// 1. the caller thread only calls this function with the same comm and stream (always true in nccl-test)
// 2. nccl-test run in "-t 1 -g 1" mode for MPI call
// 3. MPI and NCCL communicators have the same size and rank 
ncclResult_t incAllReduce(const void* sendbuff, void* recvbuff, size_t count,
    ncclDataType_t datatype, ncclRedOp_t op, ncclComm* comm, cudaStream_t stream) {
    static const bool inc_enable = stoi(get_env("INC_ENABLE", "0"));
    if(!inc_enable) return ncclAllReduce(sendbuff, recvbuff, count, datatype, op, comm, stream);

    if(!prev_comm) {
        prev_comm = comm;
        prev_stream = stream;
        MPI_Comm_rank(MPI_COMM_WORLD, &print_rank);
        init_atexit();
        init_cuda();
        init_rdma();
    }
    if(prev_comm != comm || prev_stream != stream) {
        GLOBALPRINT("Unsupported arguments\n");
        exit(1);
    }
    size_t size = count * ncclTypeSize(datatype);

    static int cnt;
    // GLOBALPRINT("incAllReduce %d, size = %d\n", ++cnt, (int)size);

    constexpr size_t align_size = MAX_QP_NUM * MTU_SIZE;
    static_assert(MAX_SEGMENT_SIZE % align_size == 0);

    size_t seg_size = size / DEFAULT_SPLIT_CNT;
    if(seg_size < MIN_SEGMENT_SIZE) seg_size = MIN_SEGMENT_SIZE;
    if(seg_size > MAX_SEGMENT_SIZE) seg_size = MAX_SEGMENT_SIZE;
    seg_size = UPDIV(seg_size, align_size) * align_size;// upper align
    size_t nseg = UPDIV(size, seg_size);
    size_t head = 0, middle = 0, tail = 0;
    
    CUDACHECKEXIT(cudaEventRecord(copy_event[0], stream));
    CUDACHECKEXIT(cudaStreamWaitEvent(d2h_stream, copy_event[0]));
    while(head < nseg) {
        size_t tail_index = tail % SEGMENT_QUEUE_SIZE; // optimized to &
        if(nseg > tail && tail - head < SEGMENT_QUEUE_SIZE && (tail < SEGMENT_QUEUE_SIZE || cudaEventQuery(copy_event[tail_index]) == cudaSuccess)) {
            size_t user_offset = seg_size * tail;
            size_t buffer_offset = seg_size * tail_index;
            size_t copy_size = min(seg_size, size - user_offset);
            CUDACHECKEXIT(cudaMemcpyAsync((char*)rdma_ctx.s_mr->addr + buffer_offset, (const char*)sendbuff + user_offset, copy_size, cudaMemcpyDeviceToHost, d2h_stream));
            CUDACHECKEXIT(cudaEventRecord(copy_event[tail_index], d2h_stream));
            tail ++;
        }
        size_t middle_index = middle % SEGMENT_QUEUE_SIZE; // optimized to &
        if(tail > middle && cudaEventQuery(copy_event[middle_index]) == cudaSuccess) {
            // size_t user_offset = seg_size * middle;
            // size_t buffer_offset = seg_size * middle_index;
            // size_t copy_size = min(seg_size, size - user_offset);
            // post_allreduce(buffer_offset, copy_size);// has inner queue
            msg_sent[middle_index] = 0;
            middle ++;
        }
        if(middle > head && poll_segment()) {
            size_t head_index = head % SEGMENT_QUEUE_SIZE; // optimized to &]
            size_t user_offset = seg_size * head;
            size_t buffer_offset = seg_size * head_index;
            size_t copy_size = min(seg_size, size - user_offset);
            CUDACHECKEXIT(cudaMemcpyAsync((char*)recvbuff + user_offset, (char*)rdma_ctx.r_mr->addr + buffer_offset, copy_size, cudaMemcpyHostToDevice, h2d_stream));
            CUDACHECKEXIT(cudaEventRecord(copy_event[head_index], h2d_stream));
            head ++;
        }
        try_post_message(head, middle, seg_size, size);
    }
    CUDACHECKEXIT(cudaEventRecord(copy_event[0], h2d_stream));
    CUDACHECKEXIT(cudaStreamWaitEvent(stream, copy_event[0]));
    
    // CUDACHECKEXIT(cudaStreamSynchronize(stream));

    // GLOBALPRINT("incAllReduce return\n");
    return ncclSuccess;
}