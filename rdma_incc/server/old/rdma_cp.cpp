#include <cstdio>
#include <iostream>
#include <memory>
#include <map>
#include <string>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <endian.h>
#include <fstream>
#include <map>
#include <thread>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sched.h>
#include <arpa/inet.h>
#include <sys/mman.h>

#include <infiniband/verbs.h>

#include "dep/argparse/argparse.hpp"
#include "dep/jsoncpp/json/json.h"

#define ENABLE_IB_UTILS
#include "net_utils.hpp"
#include "yyt_error.h"
// some assumption of the network
uint32_t PORT_ID; 
#define LID 0
uint32_t GID_INDEX; // it seens that only 2 & 3 can work in ethernet
/*
 * GID_INDEX
 * 0 : native GID (RoCEv1)
 * 1 : native GID (RoCEv2)
 * 2 : GID from IPv4 (RoCEv1)
 * 3 : GID from IPv4 (RoCEv2)
 * This table can be configured 
 * Use "show_gids" to show the table
 */ 

// some definition of the network
/*
 * Our p4 implementation requires all PSN start from a same value.
 * The stage resources of switch is limited, there is no extra stage 
 * to calculate the offset through PSN difference, so we directly 
 * use PSN (with modulo) as offset.
 */ 
#define PSN 0 // let all PSN start from 0

#define MTU_SIZE(MTU_ID) (128*(1<<(MTU_ID)))

#define ALLOC_MEM_SIZE (1<<30)

#define MAX_Q_SIZE 128 // for huge message, 2 is enough 

#define MAX_QP_NUM 8 
#define TINY_MESSAGE_LIM(MTU) ((MTU)*4)
#define HUGE_MESSAGE_LIM(MTU) ((MTU)*16)

#define MAX_GROUP_NUM 4096
#define MAX_GROUP_SIZE 32
// need GID & QPN for connection
// need VA & RKEY for RDMA operations
using std::swap;
using std::string;
using std::unique_ptr;
using std::map;
using std::to_string;
using std::ifstream;
using std::stoi, std::stoul;

typedef unsigned short us;

struct transmission_config {
    uint32_t nqp;
    int qp_size;
    ibv_mtu mtu;
    uint32_t msg_size;
    uint64_t file_size;
    bool use_send;
}__attribute__ ((__packed__));

struct bc_addr {
    union ibv_gid gid;
    // assume lid==0 && psn==0
    uint64_t addr;
    uint32_t rkey;
    uint32_t ip;
    int qpn[MAX_QP_NUM];
}__attribute__ ((__packed__));

struct mem_info_t {
    uint64_t addr;
    uint32_t rkey;
}__attribute__ ((__packed__));

struct mem_array_t {
    uint16_t size;
    mem_info_t mem_info[MAX_GROUP_SIZE];
}__attribute__ ((__packed__));

transmission_config ts_config;
map<string, uint64_t> time_cost;
bool use_huge;

uint64_t gettimeus()
{
	timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec*1000000 + t.tv_nsec/1000;
}

uint64_t get_file_size(string file_name) 
{
    ifstream fin(file_name, std::ios::binary);
    fin.seekg(0, fin.end);
    return fin.tellg();
}

void *mmap_file(string file_name, size_t file_size, bool overwrite)
{
    int open_flags = overwrite ? (O_RDWR | O_CREAT) : O_RDWR;
    int fd = open(file_name.c_str(), open_flags, 0666);
    MYCHECK(fd < 0, "Error on open");
    if(overwrite) {
        int ret = ftruncate(fd, file_size);
        MYCHECK(ret, "Error: ftruncate");
    }
    
    int mmap_prot = overwrite ? (PROT_READ | PROT_WRITE) : (PROT_READ | PROT_WRITE);
    int mmap_flags = /*MAP_PRIVATE | MAP_ANON; */MAP_SHARED | MAP_POPULATE;
    if(use_huge) mmap_flags |= MAP_HUGETLB;

    time_cost["mmap"] -= gettimeus();

    void *ret = mmap(NULL, file_size, mmap_prot, mmap_flags, fd, 0);

    /*
    size_t nt = std::min((size_t)1, (file_size+(1<<21)-1)/(1<<21));
    size_t pg = 1<<12;
    size_t np = (file_size+pg-1)/pg;
    vector<std::thread>vec;
    char *addr_st = (char *) ret;
    char *addr_ed;
    for(size_t i = 0; i < nt; i++) {
        addr_ed = addr_st + pg*(np/nt + (i<np%nt));
        vec.emplace_back([=] {
            for(char* addr = addr_st; addr < addr_ed; addr += pg) addr[0] = 0;
        });
        addr_st = addr_ed;
    }
    for(size_t i = 0; i < nt; i++) {
        vec[i].join();
    }
    */

    //mlock(ret, file_size);
    //printf("%d %d %d %d\n", (int)((char*)ret)[0], (int)((char*)ret)[10007], (int)((char*)ret)[100000000], (int)((char*)ret)[1000000]);
    // if(overwrite) for(size_t i = 0; i < file_size; i += (1<<12)) __builtin_prefetch((char*)ret + i, 0, 0);
    // if(overwrite) for(size_t i = 0; i < file_size; i += (1<<12)) ((char*)ret)[i] = 0;
    time_cost["mmap"] += gettimeus();
    MYCHECK(ret==NULL, "Error on mmap");
    close(fd);
	return ret;
}

ibv_qp *init_qp(ibv_context *ctx, ibv_pd *pd)
{
    int ret;
    // CQ
    ibv_cq *scq = ibv_create_cq(ctx, ts_config.qp_size, NULL, /*comp_channel*/NULL, 0);// use 0 as comp_vector
    ibv_cq *rcq = ibv_create_cq(ctx, ts_config.qp_size, NULL, /*comp_channel*/NULL, 0);// use 0 as comp_vector
    MYCHECK(scq == NULL, "Error on ibv_create_cq");
    MYCHECK(rcq == NULL, "Error on ibv_create_cq");
    struct ibv_qp_init_attr init_attr = {};
    init_attr.send_cq = scq;
    init_attr.recv_cq = rcq;
    init_attr.cap.max_send_wr  = ts_config.qp_size;
    init_attr.cap.max_recv_wr  = ts_config.qp_size;
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
    attr.port_num        = PORT_ID;
    attr.qp_access_flags = 0;

    ret = ibv_modify_qp(qp, &attr, IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS);
    MYCHECK(ret != 0, "Error on RESET->INIT");
    return qp;
}

void move_qp_to_rts(ibv_qp *qp, ibv_gid dgid, uint32_t dqpn) 
{
    int ret;
    // INIT -> RTR
    struct ibv_qp_attr attr = {};
	attr.qp_state		= IBV_QPS_RTR;
	attr.path_mtu		= ts_config.mtu;
	attr.dest_qp_num	= dqpn;
	attr.rq_psn			= PSN;
	attr.max_dest_rd_atomic	= 1;
	attr.min_rnr_timer		= 12;// wait for 0.64ms to send RNR NACK
	attr.ah_attr.is_global	= 0;
	attr.ah_attr.dlid		= LID;
    attr.ah_attr.sl		    = 0;
	attr.ah_attr.src_path_bits = 0;
	attr.ah_attr.port_num	= PORT_ID;

    attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE;// SEND is allowd by default, and WRITE is optional

    if(dgid.global.interface_id) {
        attr.ah_attr.is_global = 1;
		attr.ah_attr.grh.hop_limit = 1;
		attr.ah_attr.grh.dgid = dgid;
		attr.ah_attr.grh.sgid_index = GID_INDEX;
    }
    int attr_mask = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN | 
                    IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER | IBV_QP_ACCESS_FLAGS;
    // IBV_QP_ACCESS_FLAGS is optional ! 
    ret = ibv_modify_qp(qp, &attr, attr_mask);
    MYCHECK(ret != 0, "Error on INIT->RTR");
    // RTR -> RTS
    attr = {};
    attr.qp_state	    = IBV_QPS_RTS;
	attr.timeout	    = 14;// 4=65us, 8=1ms, 14=67ms, 18=1s, 31=8800s, 0=INF
	attr.retry_cnt	    = 7;
	attr.rnr_retry	    = 7;// 7 means retry infinitely when RNR NACK is received
	attr.sq_psn	        = PSN;
	attr.max_rd_atomic  = 1;
    attr_mask = IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT |
			    IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC;
    ret = ibv_modify_qp(qp, &attr, attr_mask);
    MYCHECK(ret != 0, "Error on RTR->RTS");
}


void exch_addr(int group_size, int rank, bc_addr &loc_addr, 
    uint32_t ip, uint16_t port, string config_file_dir, ibv_pd *pd, string file_name, ibv_context *ctx,
    ibv_mr *&mr, ibv_qp **&qp, bc_addr &rem_addr, mem_array_t &mem_array)
{
    int ret;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    MYCHECK(fd < 0, "Error on socket()");
    struct sockaddr_in rank0_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr = { .s_addr = htonl(ip) },
        .sin_zero = {},
    };
    int opt = 1;
    ret = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (void*)&opt, sizeof(opt));
    MYCHECK(ret < 0, "Error on setsockopt()");

    if(rank == 0) {
        int *fd_list = new int[group_size];
        for(int i = 0; i < group_size; i++) fd_list[i] = -1;

        bc_addr *addr_list = new bc_addr[group_size];

        ret = bind(fd, (struct sockaddr *)&rank0_addr, sizeof(rank0_addr));
        MYCHECK(ret < 0, "Error on bind()");
        ret = listen(fd, group_size);
        MYCHECK(ret < 0, "Error on listen()");
        // gather
        
        for(int i = 1; i < group_size; i++) {
            int conn_fd = accept(fd, NULL, NULL);
            MYCHECK(conn_fd < 0, "Error on accept()");
            int client_rank;
            ret = read(conn_fd, &client_rank, sizeof(client_rank));
            MYCHECK(!(0<client_rank && client_rank<group_size), "Rank error");
            MYCHECK(fd_list[client_rank] != -1, "Rank error");
            MYCHECK(ret < sizeof(client_rank), "Partial read()");

            fd_list[client_rank] = conn_fd;

            ret = write(conn_fd, &ts_config, sizeof(ts_config));
            MYCHECK(ret < sizeof(ts_config), "Partial write()");
        }

        void *mmap_addr = mmap_file(file_name, ts_config.file_size, 0);
        time_cost["ibv_reg_mr"] -= gettimeus();
        mr = ibv_reg_mr(pd, mmap_addr, ts_config.file_size, 0 /*IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE*/);
        MYCHECK(mr == NULL, "Error on ibv_reg_mr");
        time_cost["ibv_reg_mr"] += gettimeus();

        loc_addr.addr = (uint64_t)mr->addr;
        loc_addr.rkey = mr->rkey;

        qp = new ibv_qp *[ts_config.nqp];
        for(int i = 0; i < ts_config.nqp; i++) qp[i] = init_qp(ctx, pd);
        for(int i = 0; i < ts_config.nqp; i++) loc_addr.qpn[i] = qp[i]->qp_num;

        addr_list[0] = loc_addr;
        for(int i = 1; i < group_size; i++) {
            ret = read(fd_list[i], &addr_list[i], sizeof(bc_addr));
            MYCHECK(ret < sizeof(bc_addr), "Partial read()");
        }        

        // print 
        if(config_file_dir != "") {
            // read config files
            ifstream topo_file(config_file_dir + "/topo.json");
            Json::Reader reader;
            Json::Value topo_json;
            MYCHECK(!reader.parse(topo_file, topo_json), "Error on parsing json");
            
            srand(time(0));
            unique_ptr<int[]>group_id(new int[ts_config.nqp]);

            for(int qp_idx = 0; qp_idx < ts_config.nqp; qp_idx++) {
                group_id[qp_idx] = rand() % MAX_GROUP_NUM;

                printf("bfrt.rdma_broadcast.pipe.Ingress.metadata_table.add_with_get_broadcast_forward_metadata\\\n\
(sip=%#x, dip=%#x, dqpn=%#x, group_id=%d, src_rank=%d, dst_rank=%d)\n",
                    addr_list[0].ip, addr_list[1].ip, addr_list[1].qpn[qp_idx], group_id[qp_idx], 0, 0xffff);

                for(int i = 1; i < group_size; i++) {
                    printf("bfrt.rdma_broadcast.pipe.Ingress.metadata_table.add_with_get_broadcast_backward_metadata\\\n\
(sip=%#x, dip=%#x, dqpn=%#x, group_id=%d, src_rank=%d, dst_rank=%d, bitmap=%#x, bitmap_mask=%#x)\n",
                    addr_list[i].ip, addr_list[0].ip, addr_list[0].qpn[qp_idx], group_id[qp_idx], i, 0, 1<<i, ((1<<group_size)-1)^(1<<0));
                        
                    if(i != 1) {
                        printf("bfrt.rdma_broadcast.pipe.Egress.restore_table.add_with_restore_dst_fields\\\n\
(group_id=%d, src_rank=%d, dst_rank=%d, dip=%#x, dqpn=%#x)\n",
                        group_id[qp_idx], 0, i, addr_list[i].ip, addr_list[i].qpn[qp_idx]);
                        printf("bfrt.rdma_broadcast.pipe.Egress.restore_table.add_with_restore_src_fields\\\n\
(group_id=%d, src_rank=%d, dst_rank=%d, sip=%#x)\n",
                        group_id[qp_idx], i, 0, addr_list[1].ip);
                    }
                }

                unique_ptr<int[]>node_id(new int[group_size]);
                string node_ids;
                map<uint32_t, int>p4_port;
                for(auto i = 0; i < topo_json["port"].size(); i++) 
                    p4_port[(uint32_t)stoul(topo_json["IP"][i].asString(), NULL, 16)] = (int)stoi(topo_json["port"][i].asString());
                
                
                printf("# NOTE: rid == dst_rank in broadcast\n");

                for(int i = 0; i < group_size; i++) {
                    node_id[i] = rand();
                    printf("bfrt.pre.node.add(%d, %d, None, [%d])\n", node_id[i], i, p4_port[addr_list[i].ip]);
                    if(i) node_ids += ", ";
                    node_ids += to_string(node_id[i]);
                }

                printf("bfrt.pre.mgid.add(%d, [%s], [True]*%d, list(range(%d)))\n",
                    group_id[qp_idx], node_ids.c_str(), group_size, group_size);
                
                printf("bfrt.rdma_broadcast.pipe.Egress.broadcast_ack_egress.reg_bitmap.mod(%d, 0)\n", group_id[qp_idx]);
                printf("bfrt.rdma_broadcast.pipe.Egress.broadcast_ack_egress.reg_prev_psn.mod(%d, 0xffffff00)\n", group_id[qp_idx]);
                printf("bfrt.rdma_broadcast.pipe.Egress.broadcast_ack_egress.reg_prev_msn.mod(%d, 0)\n", group_id[qp_idx]);
            }           
            printf("\n");
            printf("Press any key to continue\n");
            getchar();
            printf("\n");
        }    

        // exchange
        mem_array.size = group_size;
        for(int i = 0; i < group_size; i++) mem_array.mem_info[i] = {addr_list[i].addr, addr_list[i].rkey};

        bc_addr tmp = addr_list[1];
        for(int i = 1; i < group_size; i++) addr_list[i] = addr_list[0];
        addr_list[0] = tmp;
            
        // scatter
        for(int i = 1; i < group_size; i++) {
            ret = write(fd_list[i], &addr_list[i], sizeof(addr_list[i]));
            MYCHECK(ret < sizeof(addr_list[i]), "Partial write()");
            close(fd_list[i]);
        }
        rem_addr = addr_list[0];
        delete[] addr_list;
        delete[] fd_list;
    }
    else {
        ret = connect(fd, (struct sockaddr *)&rank0_addr, sizeof(rank0_addr));
        MYCHECK(ret < 0, "Error on connect()");

        ret = write(fd, &rank, sizeof(rank));
        MYCHECK(ret < sizeof(rank), "Partial write()");

        ret = read(fd, &ts_config, sizeof(ts_config));
        MYCHECK(ret < sizeof(ts_config), "Partial read()");

        // this may take a long time for huge files, since ibv_reg_mr() will pin physical memory 
        void *mmap_addr = mmap_file(file_name, ts_config.file_size, 1);
        time_cost["ibv_reg_mr"] -= gettimeus();
        mr = ibv_reg_mr(pd, mmap_addr, ts_config.file_size, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE/* | IBV_ACCESS_REMOTE_READ*/);
        MYCHECK(mr == NULL, "Error on ibv_reg_mr");
        time_cost["ibv_reg_mr"] += gettimeus();
        loc_addr.addr = (uint64_t)mr->addr;
        loc_addr.rkey = mr->rkey;

        qp = new ibv_qp *[ts_config.nqp];
        for(int i = 0; i < ts_config.nqp; i++) qp[i] = init_qp(ctx, pd);
        for(int i = 0; i < ts_config.nqp; i++) loc_addr.qpn[i] = qp[i]->qp_num;

        ret = write(fd, &loc_addr, sizeof(loc_addr));
        MYCHECK(ret < sizeof(loc_addr), "Partial write()");
        ret = read(fd, &rem_addr, sizeof(rem_addr));
        MYCHECK(ret < sizeof(rem_addr), "Partial read()");
    }

    close(fd);
}

void post_broadcast(bool is_root, bool use_send, bool notify, ibv_qp *qp, ibv_sge *bc_sge, mem_array_t *mem_array)
{
    int ret;
    if(!is_root && !use_send && !notify) {
        errno = EINVAL;
        perror("post_broadcast");
        exit(1);
    }
    if(!is_root) {// use_send or (!use_send && notify)
        struct ibv_recv_wr wr = {}, *bad_wr;
        wr.wr_id = 1;
        wr.next = NULL;
        wr.sg_list = bc_sge;
        wr.num_sge = 1;
        ret = ibv_post_recv(qp, &wr, &bad_wr);
        if(ret != 0) fprintf(stderr, "%d\n", ret);
        MYCHECK(ret != 0, "Error on ibv_post_recv");
        return;
    }
    struct ibv_send_wr wr = {}, *bad_wr;
    wr.wr_id = 1;
    wr.next = NULL;
    wr.sg_list = bc_sge;
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
        wr.wr.rdma.remote_addr = 0;
        wr.wr.rdma.rkey = 0;

        mem_array_t *broadcast_header = (mem_array_t *)wr.sg_list->addr;
        broadcast_header->size = htobe16(mem_array->size);
        for(uint16_t i = 0; i < mem_array->size; i++) 
            broadcast_header->mem_info[i] = 
                {htobe64(mem_array->mem_info[i].addr), htobe32(mem_array->mem_info[i].rkey)};
    }
    ret = ibv_post_send(qp, &wr, &bad_wr);
    if(ret != 0) fprintf(stderr, "%d\n", ret);
    MYCHECK(ret != 0, "Error on ibv_post_send");
}

int poll_cq(ibv_cq *cq)
{
    static struct ibv_wc wc[MAX_Q_SIZE];
    int ret = ibv_poll_cq(cq, ts_config.qp_size, wc);
    MYCHECK(ret < 0, "Error on ibv_poll_cq");
    for(int i = 0; i < ret; i++) {
        if(wc[i].status != IBV_WC_SUCCESS) {
            fprintf(stderr, "%d\n", (int)wc[i].status);
            MYCHECK(wc[i].status != IBV_WC_SUCCESS, "Error on ibv_poll_cq");
        }
    }
    return ret;
}

void push_qp(bool is_root, bool use_send, bool notify_last, 
    ibv_qp *qp, bool can_push, size_t &ccnt, ibv_sge *bc_sge, mem_array_t *mem_array)
{
    ibv_cq *cq_to_poll = is_root? qp->send_cq: qp->recv_cq;
            
    if(can_push) {
        post_broadcast(is_root, use_send, notify_last, qp, bc_sge, mem_array);
    }
    
    // We prefer to clear out CQ rather than send new requests, so we use a loop here
    int new_ccnt;
    new_ccnt = poll_cq(cq_to_poll);
    ccnt += new_ccnt;
}

int main(int argc, char **argv)
{
    time_cost["all"] -= gettimeus();
    int group_size;
    int rank;
    string bind_ip_str;
    uint32_t bind_ip;
    string rank0_ip_str;
    uint32_t rank0_ip;
    uint16_t rank0_port;
    string config_file_dir;
    string file_name;
    int ret; 

    argparse::ArgumentParser program(argv[0], "", argparse::default_arguments::help);
    // parse parameters
    program.add_argument("<group_size>").scan<'u', us>();
    program.add_argument("<rank>").scan<'u', us>();
    program.add_argument("<bind_ip>");
    program.add_argument("<rank0_ip>");
    program.add_argument("--port").metavar("<port>").default_value((us)12345).scan<'u', us>();
    program.add_argument("--op").metavar("<operation>").default_value("send").action([&](const std::string& value) {
        static const std::vector<std::string> choices = { "write", "send"};
        if (std::find(choices.begin(), choices.end(), value) != choices.end()) {
            return value;
        }
        throw std::runtime_error("Invalid operation");
    }).help("Rank 0 only. Supported operation: write, send");
    program.add_argument("--qp").metavar("<qp_num>").scan<'u', us>().help(
        "Rank 0 only. If this value > 1, a message will be sliced into multiple sub-messages. \
Suggest leave this to the program to decide."
    );
    program.add_argument("-s").metavar("<message_size>").default_value(1u<<31).scan<'u', unsigned int>().help(
        "Rank 0 only. Require: message_size/qp_num < 2^30. The value of qp_num is typically 2 for large message_size."
    );
    program.add_argument("--q_size").metavar("<q_size>").default_value((us)2).scan<'u', us>().help(
        "Rank 0 only. "
    );
    program.add_argument("--config_dir").metavar("<config_dir>").help(
        "Specify the directory where topo.json located. \
This parameter is required to leverage multicast ability of Tofino switch. \
If not specified, the program will run in 1-to-1 mode without the support of Tofino."
    );
    program.add_argument("--huge").nargs(0).help("Use huge pages.");
    program.add_argument("<file_name>");

    program.add_description(string("Example: ") + argv[0] + " 2 0 192.168.1.1 10.0.0.1 test_file\n\
Note: In the current implementation, memory usage is as large as file size. \
But tmpfs, the memory usage is 0 since the file is already in memory.");
    try{
        program.parse_args(argc, argv);
    }
    catch (const std::runtime_error& err) {
        std::cerr << err.what() << std::endl;
        std::cerr << program;
        std::exit(1);
    }
        
    group_size = (int)program.get<us>("<group_size>");
    rank = (int)program.get<us>("<rank>");
    bind_ip_str = program.get<string>("<bind_ip>");
    bind_ip = ntohl(inet_addr(bind_ip_str.c_str()));
    rank0_ip_str = program.get<string>("<rank0_ip>");
    rank0_ip = ntohl(inet_addr(rank0_ip_str.c_str()));
    rank0_port = program.get<us>("--port");
    ts_config.msg_size = program.get<unsigned int>("-s");

    ts_config.qp_size = (int)program.get<us>("--q_size");
    if(program.is_used("--config_dir")) {
        config_file_dir = program.get<string>("--config_dir");
        MYCHECK(config_file_dir=="", "Invalid config_file_dir");
    }
    else {
        config_file_dir = "";
    }
    if(program.is_used("--huge")) {
        use_huge = 1;
    }
    file_name = program.get<string>("<file_name>");

    if(group_size < 2 || group_size > 32 || rank >= group_size) {
        printf("Invalid group_size or rank\n");
        exit(1);
    }
    if(program.get<string>("--op") == "write") {
        ts_config.use_send = 0;
        printf("Use WRITE\n");
    }
    else{
        ts_config.use_send = 1;
        printf("Use SEND\n");
    }

    string dev_name = std::get<0>(get_device_by_ip(bind_ip_str));
    string ib_dev_name = dev_to_ib_dev(dev_name);
    int socket_id = get_socket_by_pci(get_pci_by_dev(dev_name));
    vector<int> cpu_list = get_cpu_list_by_socket(socket_id);
    MYCHECK(cpu_list.empty(), "CPU list empty");

    // Set affinity
    printf("Bind cpu %d\n", cpu_list[0]);
    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(cpu_list[0], &mask);
    if (sched_setaffinity(0, sizeof(mask), &mask) == -1) {
        perror("sched_setaffinity");
        exit(1);
    } 

    // Open device
    show_ib_devices();
    printf("Open device %s (%s)\n", ib_dev_name.c_str(), dev_name.c_str());
    ibv_device *dev = find_ib_device(ib_dev_name);
    MYCHECK(dev == NULL, "Error on find_device");
    ibv_context *ctx = ibv_open_device(dev);
    MYCHECK(ctx == NULL, "Error on ibv_open_device");
    // ibv_comp_channel *comp_channel = ibv_create_comp_channel(ctx);
    // MYCHECK(comp_channel == NULL, "Error on ibv_create_comp_channel");

    // PD
    printf("Alloc PD\n");
    ibv_pd *pd = ibv_alloc_pd(ctx);
    MYCHECK(pd == NULL, "Error on ibv_alloc_pd");

    printf("Query port_id & gid_index\n");
    bc_addr loc_addr = {};
    // size_t max_entries = 32;
    // unique_ptr<struct ibv_gid_entry[]> gid_entries(new struct ibv_gid_entry[max_entries]);
    // ssize_t num_entries = ibv_query_gid_table(ctx, gid_entries.get(), max_entries, 0);
    // MYCHECK(num_entries <= 0, "Error on ibv_query_gid_table");
    // MYCHECK(num_entries == max_entries, "Device has too many gids");
    // for(ssize_t i = 0; i < num_entries; i++) {
    //     if(gid_entries[i].gid_type != IBV_GID_TYPE_ROCE_V2) continue;
    //     if(gid_to_ipv4(gid_entries[i].gid) != bind_ip) continue;
    //     PORT_ID = gid_entries[i].port_num;
    //     GID_INDEX = gid_entries[i].gid_index;
    //     addr.gid = gid_entries[i].gid;
    //     break;
    // }
    // MYCHECK(PORT_ID == 0, "No matching entry in gid table");
    ibv_device_attr dev_attr;
    ibv_port_attr port_attr;
    ret = ibv_query_device(ctx, &dev_attr);
    MYCHECK(ret, "Error on ibv_query_device");

    loc_addr.ip = bind_ip;
    for(uint8_t i = 1; i <= dev_attr.phys_port_cnt; i++) {
        ret = ibv_query_port(ctx, i, &port_attr);
        MYCHECK(ret, "Error on ibv_query_port");
        for(int j = 1; j < port_attr.gid_tbl_len; j += 2) {// only query RoCEv2
            ret = ibv_query_gid(ctx, i, j, &loc_addr.gid);
            if(gid_to_ipv4(loc_addr.gid) != bind_ip) continue;
            PORT_ID = i;
            GID_INDEX = j;
            ts_config.mtu = port_attr.active_mtu;
            break;
        }
    }
    printf("port_id %u, gid_index %u\n", PORT_ID, GID_INDEX);
    printf("MTU: %d\n", 128*(1<<ts_config.mtu));
    if(ts_config.mtu != IBV_MTU_4096) printf("WARNING: MTU is not 4096, perfermance may not be optimal!\n");


    printf("Exchange data\n");
    
    ibv_qp **qp;
    ts_config.file_size = rank==0 ? get_file_size(file_name) : 0;
    MYCHECK(rank == 0 && ts_config.file_size == 0, "Can not transfer empty file"); 
    ibv_mr *mr = NULL;
    bc_addr rem_addr = {};
    mem_array_t mem_array = {};

    if(ts_config.msg_size > ts_config.file_size) 
        ts_config.msg_size = ts_config.file_size;
    
    if(program.is_used("--qp")) {
        ts_config.nqp = (int)program.get<us>("--qp");
    }
    else {
        ts_config.nqp = ts_config.msg_size >= HUGE_MESSAGE_LIM(ts_config.mtu) ? 2 : 1;
    }
    if(ts_config.nqp == 0 || ts_config.nqp > MAX_QP_NUM) {
        printf("qp_num invalid\n");
        exit(1);
    }
    if(rank == 0 && (ts_config.msg_size == 0 || ts_config.msg_size/ts_config.nqp > ALLOC_MEM_SIZE)) {
        printf("Invalid message size\n");
        exit(1);
    }

    exch_addr(group_size, rank, loc_addr, rank0_ip, rank0_port, config_file_dir, pd, file_name, ctx,
        mr, qp, rem_addr, mem_array);

    printf("local info:  gid %s, mem_addr %#lx, rkey %#x,\n", 
        gid_to_str(loc_addr.gid).c_str(), loc_addr.addr, loc_addr.rkey);
    printf("             qpn ");
    for(int i = 0; i < ts_config.nqp; i++) printf("%#x%c", loc_addr.qpn[i], ",\n"[i==ts_config.nqp-1]);
    printf("\n");

    printf("remote info:  gid %s, mem_addr %#lx, rkey %#x,\n", 
        gid_to_str(rem_addr.gid).c_str(), rem_addr.addr, rem_addr.rkey);
    printf("              qpn ");
    for(int i = 0; i < ts_config.nqp; i++) printf("%#x%c", rem_addr.qpn[i], ",\n"[i==ts_config.nqp-1]);
    printf("\n");

    printf("Connect QP\n");
    for(int i = 0; i < ts_config.nqp; i++) move_qp_to_rts(qp[i], rem_addr.gid, rem_addr.qpn[i]);

    printf("Testing\n");
    struct ibv_sge bc_sge = {
        .addr = (uint64_t)mr->addr,
        .length = (uint32_t)ts_config.msg_size,          
        .lkey = mr->lkey,
    };
    
    time_cost["RDMA"] -= gettimeus();

    bool is_root = rank == 0;
    
    size_t iter_cnt = (ts_config.file_size + ts_config.msg_size - 1) / ts_config.msg_size;
    if(!ts_config.use_send && !is_root) iter_cnt = 1;

    size_t scnt = 0, ccnt = 0;
    unique_ptr<size_t[]>sub_ccnt(new size_t[ts_config.nqp] ());

    while(ccnt < iter_cnt) {
        bool can_push = std::min(ts_config.qp_size - (scnt - ccnt), iter_cnt - scnt) > 0;
    
        size_t prev_ccnt = ccnt;
        bool notify_last = scnt == iter_cnt-1;
        ccnt = iter_cnt;
        
        struct ibv_sge sub_sge = bc_sge;
        struct mem_array_t sub_mem_array = mem_array;

        for(int i = 0; i < ts_config.nqp; i++) {
            uint32_t sub_size = bc_sge.length / ts_config.nqp + (i < bc_sge.length % ts_config.nqp);
            sub_sge.length = sub_size;

            push_qp(is_root, ts_config.use_send, notify_last, qp[i], can_push, sub_ccnt[i], &sub_sge, &sub_mem_array);
            if(sub_ccnt[i] < ccnt) ccnt = sub_ccnt[i];

            sub_sge.addr += sub_size;
            if(!ts_config.use_send) {
                for(uint16_t j = 0; j < sub_mem_array.size; j++)
                    sub_mem_array.mem_info[j].addr += sub_size;
            }
        }   

        if(can_push) {
            scnt++;

            bc_sge.addr += ts_config.msg_size;
            bc_sge.length = scnt == iter_cnt-1 ? ts_config.file_size-ts_config.msg_size*scnt : ts_config.msg_size;
            for(uint16_t j = 0; j < mem_array.size; j++)
                mem_array.mem_info[j].addr += ts_config.msg_size;
        }
        // if(is_root && 10*ccnt/iter_cnt != 10*prev_ccnt/iter_cnt) 
        //     printf("%ld%%\n", 100*ccnt/iter_cnt);
    }
    
    time_cost["RDMA"] += gettimeus();
    printf("%.2lf Gbps\n", 1.0*ts_config.file_size*8*1e-3/time_cost["RDMA"]);

    /*
    void *mmap_addr = mr->addr;

    time_cost["ibv_dereg_mr"] -= gettimeus();
    ret = ibv_dereg_mr(mr);
    time_cost["ibv_dereg_mr"] += gettimeus();
    MYCHECK(ret, "Error on ibv_dereg_mr");

    time_cost["munmap"] -= gettimeus();
    ret = munmap(mmap_addr, ts_config.file_size);
    time_cost["munmap"] += gettimeus();
    MYCHECK(ret, "Error on munmap");
    */

    time_cost["all"] += gettimeus();
    uint64_t other_time = 0;
    for(auto &p: time_cost) {
        if(p.first == "all") other_time += p.second;
        else other_time -= p.second;
        //std::cout << p.first << " " << p.second << " " << other_time << std::endl;
    }
        
    time_cost["other"] = other_time;

    printf("Time:\n");
    auto print_list = {"all", "RDMA", "mmap", "ibv_reg_mr", /*"ibv_dereg_mr", "munmap",*/ "other"};
    for(auto e: print_list) 
        printf("%s: %lf s\n", e, time_cost[e]*1e-6);
    
    return 0;
}