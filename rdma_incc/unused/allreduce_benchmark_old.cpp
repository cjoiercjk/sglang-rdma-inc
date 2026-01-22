#include <cstdio>
#include <iostream>
#include <memory>
#include <map>
#include <string>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <endian.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sched.h>
#include <arpa/inet.h>
#include <sys/mman.h>

#include <infiniband/verbs.h>

#include "dep/argparse/argparse.hpp"

#define ENABLE_IB_UTILS
#include "net_utils.hpp"
#include "yyt_error.h"
// some assumption of the network
#define PORT_ID 1
#define LID 0
#define GID_INDEX 3

// some definition of the network
/*
 * Our p4 implementation requires all PSN start from a same value.
 * The stage resources of switch is limited, there is no extra stage 
 * to calculate the offset through PSN difference, so we directly 
 * use PSN (with modulo) as offset.
 */ 
#define PSN 0 // let all PSN start from 0
// We may support MTU of 512 with resubmission
#define MTU IBV_MTU_256 

#define ALLOC_MEM_SIZE (1<<30)
#define ADDR_BW 14 // 16k entries
#define PACKET_SIZE_BW 8 // 256B
#define SWITCH_WIN_SIZE (1<<(ADDR_BW+PACKET_SIZE_BW)) // (1<<22) at most for a single write
#define SERVER_WIN_SIZE (SWITCH_WIN_SIZE>>1)
#define MAX_WRITE_SIZE (SERVER_WIN_SIZE>>1)

#define ADDR_MASK ((1<<ADDR_BW)-1) // (1<<14)-1

#define Q_SIZE 4

#define MAX_QP_NUM 8
#define TINY_MESSAGE_LIM 4096
#define HUGE_MESSAGE_LIM 65536

#define MAX_GROUP_NUM 1024

// need GID & QPN for connection
// need VA & RKEY for RDMA operations
using std::swap;
using std::string;
using std::unique_ptr;
using std::map;
using std::to_string;

typedef unsigned short us;

struct agg_addr {
    union ibv_gid prev_gid;
    int prev_qpn;
    union ibv_gid next_gid;
    int next_qpn;
    // assume lid==0 && psn==0
    uint64_t addr;
    uint32_t rkey;
    uint32_t ip;
    uint32_t next_ip;
};

void *malloc_huge(size_t size)
{
	void *ret = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
	cwm(ret==NULL, "Error on mmap");
	return ret;
}

agg_addr exch_addr(int group_size, int rank, agg_addr my_addr, uint32_t ip, uint16_t port)
{
    int ret;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    cwm(fd < 0, "Error on socket()");
    struct sockaddr_in rank0_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr = { .s_addr = htonl(ip) },
        .sin_zero = {},
    };
    int opt = 1;
    ret = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (void*)&opt, sizeof(opt));
    cwm(ret < 0, "Error on setsockopt()");
    agg_addr neighbor_addr;

    if(rank == 0) {
        int *fd_list = new int[group_size];
        agg_addr *addr_list = new agg_addr[group_size];

        ret = bind(fd, (struct sockaddr *)&rank0_addr, sizeof(rank0_addr));
        cwm(ret < 0, "Error on bind()");
        ret = listen(fd, group_size);
        cwm(ret < 0, "Error on listen()");
        // gather
        addr_list[0] = my_addr;
        for(int i = 1; i < group_size; i++) {
            int conn_fd = accept(fd, NULL, NULL);
            cwm(conn_fd < 0, "Error on accept()");
            int client_rank;
            ret = read(conn_fd, &client_rank, sizeof(client_rank));
            cwm(!(0<client_rank && client_rank<group_size), "Rank error");
            cwm(ret < sizeof(client_rank), "Partial read()");

            fd_list[client_rank] = conn_fd;
            ret = read(conn_fd, &addr_list[client_rank], sizeof(agg_addr));
            cwm(ret < sizeof(agg_addr), "Partial read()");
        }        
        // exchange
        for(int i = 0; i < group_size-1; i++) {
            swap(addr_list[i].next_gid, addr_list[i+1].prev_gid);
            swap(addr_list[i].next_qpn, addr_list[i+1].prev_qpn);
            addr_list[i].addr = addr_list[i+1].addr;
            addr_list[i].rkey = addr_list[i+1].rkey;
            addr_list[i].next_ip = addr_list[i+1].ip;
        }
        swap(addr_list[group_size-1].next_gid, addr_list[0].prev_gid);
        swap(addr_list[group_size-1].next_qpn, addr_list[0].prev_qpn);
        addr_list[group_size-1].addr = my_addr.addr;
        addr_list[group_size-1].rkey = my_addr.rkey;
        addr_list[group_size-1].next_ip = my_addr.ip;
        // print 
        srand(time(0));
        int group_id = rand() % MAX_GROUP_NUM;

        for(int i = 0; i < group_size; i++) {
            agg_addr &cur = addr_list[i];
            printf("bfrt.rdma_allreduce.pipe.Ingress.INA_metadata_table.add_with_get_INA_metadata\\\n\
(dip=%#x, dqpn=%#x, group_id=%d, rank=%d, bitmap=%#x, bitmap_mask=%#x, agg_addr=%#x, agg_addr_offset_mask=%#x)\n",
            cur.next_ip, cur.next_qpn, group_id, i, 1<<i, (1<<group_size)-1, 0, ADDR_MASK);
            printf("bfrt.rdma_allreduce.pipe.Egress.restore_table.add_with_restore_fields_with_reth\\\n\
(group_id=%d, src_rank=%d, valid=%s, sip=%#x, dip=%#x, sport=%d, dqpn=%#x, mem_addr=%#lx, rkey=%#x)\n",
            group_id, i, "True", cur.ip, cur.next_ip, 12345, cur.next_qpn, cur.addr, cur.rkey);
            printf("bfrt.rdma_allreduce.pipe.Egress.restore_table.add_with_restore_fields\\\n\
(group_id=%d, src_rank=%d, valid=%s, sip=%#x, dip=%#x, sport=%d, dqpn=%#x)\n",
            group_id, i, "False", cur.ip, cur.next_ip, 12345, cur.next_qpn);
        }
        
        unique_ptr<int[]>node_id(new int[group_size]);
        string node_ids;
        map<uint32_t, int>p4_port;
        p4_port[0xC0A80101] = 180;
        p4_port[0xC0A80102] = 164;
        p4_port[0xC0A80103] = 148;
        p4_port[0xC0A80104] = 132;
        printf("# NOTE: rid == src_rank in allreduce\n");
        for(int i = 0; i < group_size; i++) {
            int j;
            retry:
            node_id[i] = rand();
            for(j = 0; j < i; j++) 
                if(node_id[i] == node_id[j]) 
                    goto retry;
            printf("bfrt.pre.node.add(%d, %d, None, [%d])\n", node_id[i], i, p4_port[addr_list[(i+1)%group_size].ip]);
            if(i) node_ids += ", ";
            node_ids += to_string(node_id[i]);
        }
        printf("bfrt.pre.mgid.add(%d, [%s], [False]*%d, [0]*%d)\n",
            group_id, node_ids.c_str(), group_size, group_size);
        
        printf("\n");
        printf("Press any key to continue\n");
        getchar();
        printf("\n");
        // scatter
        for(int i = 1; i < group_size; i++) {
            ret = write(fd_list[i], &addr_list[i], sizeof(addr_list[i]));
            cwm(ret < sizeof(addr_list[i]), "Partial write()");
            close(fd_list[i]);
        }
        neighbor_addr = addr_list[0];
        delete[] addr_list;
        delete[] fd_list;
    }
    else {
        ret = connect(fd, (struct sockaddr *)&rank0_addr, sizeof(rank0_addr));
        cwm(ret < 0, "Error on connect()");
        ret = write(fd, &rank, sizeof(rank));
        cwm(ret < sizeof(rank), "Partial write()");
        ret = write(fd, &my_addr, sizeof(my_addr));
        cwm(ret < sizeof(my_addr), "Partial write()");
        ret = read(fd, &neighbor_addr, sizeof(neighbor_addr));
        cwm(ret < sizeof(neighbor_addr), "Partial read()");
    }
    close(fd);
    return neighbor_addr;
}

ibv_qp *init_qp(ibv_context *ctx, ibv_pd *pd)
{
    int ret;
    // CQ
    ibv_cq *scq = ibv_create_cq(ctx, Q_SIZE, NULL, /*comp_channel*/NULL, 0);// use 0 as comp_vector
    ibv_cq *rcq = ibv_create_cq(ctx, Q_SIZE, NULL, /*comp_channel*/NULL, 0);// use 0 as comp_vector
    cwm(scq == NULL, "Error on ibv_create_cq");
    cwm(rcq == NULL, "Error on ibv_create_cq");
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
    cwm(qp == NULL, "Error on ibv_create_qp");

    // Reset -> INIT
    struct ibv_qp_attr attr = {};
    attr.qp_state        = IBV_QPS_INIT;
    attr.pkey_index      = 0;
    attr.port_num        = PORT_ID;
    attr.qp_access_flags = 0;

    ret = ibv_modify_qp(qp, &attr, IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS);
    cwm(ret != 0, "Error on RESET->INIT");
    return qp;
}

void move_qp_to_rts(ibv_qp *qp, ibv_gid dgid, uint32_t dqpn) 
{
    int ret;
    // INIT -> RTR
    struct ibv_qp_attr attr = {};
	attr.qp_state		= IBV_QPS_RTR;
	attr.path_mtu		= MTU;
	attr.dest_qp_num	= dqpn;
	attr.rq_psn			= PSN;
	attr.max_dest_rd_atomic	= 1;
	attr.min_rnr_timer		= 12;// wait for 0.64ms to send RNR NACK
	attr.ah_attr.is_global	= 0;
	attr.ah_attr.dlid		= LID;
    attr.ah_attr.sl		    = 0;
	attr.ah_attr.src_path_bits = 0;
	attr.ah_attr.port_num	= PORT_ID;

    // attr.ah_attr.static_rate = IBV_RATE_2_5_GBPS;
    // printf("Use 2.5Gbps link\n");
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
    cwm(ret != 0, "Error on INIT->RTR");
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
    cwm(ret != 0, "Error on RTR->RTS");
}

uint64_t gettimeus()
{
	timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec*1000000 + t.tv_nsec/1000;
}

void post_allreduce(bool use_send, bool notify, ibv_qp *prev_qp, ibv_sge *r_sge, ibv_qp *next_qp, ibv_sge *s_sge, agg_addr *addr)
{
    int ret;
    if(use_send || notify) {
        struct ibv_recv_wr wr = {}, *bad_wr;
        wr.wr_id = 1;
        wr.next = NULL;
        wr.sg_list = r_sge;
        wr.num_sge = 1;
        ret = ibv_post_recv(prev_qp, &wr, &bad_wr);
        cwm(ret != 0, "Error on ibv_post_recv");
    }

    struct ibv_send_wr wr = {}, *bad_wr;
    wr.wr_id = 1;
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
        wr.wr.rdma.remote_addr = addr->addr;
        wr.wr.rdma.rkey = addr->rkey;
    }
    ret = ibv_post_send(next_qp, &wr, &bad_wr);
    cwm(ret != 0, "Error on ibv_post_send");
}

void poll_cq(ibv_cq *cq)
{
    struct ibv_wc wc;
    int ret;
    while((ret = ibv_poll_cq(cq, 1, &wc)) == 0);
    cwm(ret != 1, "Error on ibv_poll_cq");
    if(wc.status != IBV_WC_SUCCESS) fprintf(stderr, "%d\n", (int)wc.status);
    cwm(wc.status != IBV_WC_SUCCESS, "Error on ibv_poll_cq");
}

int main(int argc, char **argv)
{
    int group_size;
    int rank;
    string bind_ip_str;
    uint32_t bind_ip;
    string rank0_ip_str;
    uint32_t rank0_ip;
    uint16_t rank0_port;
    bool use_send;
    int qp_num;
    size_t msg_size;
    size_t tot_iter_cnt;
    int ret;

    argparse::ArgumentParser program("allreduce_benchmark", "", argparse::default_arguments::help);
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
    }).help("Supported operation: write, send");
    program.add_argument("--qp").metavar("<qp_num>").scan<'u', us>().help(
        "Note: If this value > 1, a message will be sliced into multiple sub-messages. \
Suggest leave this for program to decide."
    );
    program.add_argument("-s").metavar("<message_size>").default_value(1u<<20).scan<'u', unsigned int>();
    program.add_argument("-n").metavar("<iters>").default_value(100u<<10).scan<'u', unsigned int>();

    program.add_description("Example: ./allreduce_benchmark 2 0 192.168.1.1 10.0.0.1");
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
    msg_size = (size_t)program.get<unsigned int>("-s");
    tot_iter_cnt = (size_t)program.get<unsigned int>("-n");
    if(program.is_used("--qp")) {
        qp_num = (int)program.get<us>("--qp");
    }
    else {
        qp_num = msg_size >= HUGE_MESSAGE_LIM ? 2 : 1;
    }

    if(group_size < 2 || group_size > 32 || rank >= group_size) {
        printf("Invalid group_size or rank\n");
        exit(1);
    }
    if(program.get<string>("--op") == "write") {
        use_send = 0;
        printf("Use WRITE\n");
    }
    else{
        use_send = 1;
        printf("Use SEND\n");
    }
    if(qp_num == 0 || qp_num > MAX_QP_NUM) {
        printf("qp_num invalid\n");
        exit(1);
    }
    if(msg_size/qp_num < TINY_MESSAGE_LIM) {
        printf("WARNING: Suggest using 1 QP for tiny messages.\n");
    }
    else if(qp_num == 1 && msg_size >= HUGE_MESSAGE_LIM) {
        printf("INFO: Suggest using 2 QPs for large messages.\n");
    }
    if(qp_num > 2) {
        printf("WARNING: Suggest using no more than 2 QPs.\n");
    }
    if(msg_size == 0 || msg_size > ALLOC_MEM_SIZE) {
        printf("Invalid message size\n");
        exit(1);
    }
    if(tot_iter_cnt == 0) {
        printf("Invalid number of iterations\n");
        exit(1);
    }

    string dev_name = get_device_by_ip(bind_ip_str);
    string ib_dev_name = dev_to_ib_dev(dev_name);
    int socket_id = get_socket_by_pci(get_pci_by_dev(dev_name));
    vector<int> cpu_list = get_cpu_list_by_socket(socket_id);
    if(cpu_list.empty()) {
        printf("Wrong binding IP\n");
        exit(0);
    }

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
    show_devices();
    printf("Open device %s\n", ib_dev_name.c_str());
    ibv_device *dev = find_device(ib_dev_name);
    cwm(dev == NULL, "Error on find_device");
    ibv_context *ctx = ibv_open_device(dev);
    cwm(ctx == NULL, "Error on ibv_open_device");
    // ibv_comp_channel *comp_channel = ibv_create_comp_channel(ctx);
    // cwm(comp_channel == NULL, "Error on ibv_create_comp_channel");
    
    ibv_port_attr port_attr;
    ibv_query_port(ctx, PORT_ID, &port_attr);
    printf("LID: %d\n", port_attr.lid);

    // PD
    printf("Alloc PD\n");
    ibv_pd *pd = ibv_alloc_pd(ctx);
    cwm(pd == NULL, "Error on ibv_alloc_pd");
    
    // MR
    printf("Register MR\n");
    ibv_mr *s_mr = ibv_reg_mr(pd, malloc_huge(ALLOC_MEM_SIZE), ALLOC_MEM_SIZE, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE/* | IBV_ACCESS_REMOTE_READ*/);
    ibv_mr *r_mr = ibv_reg_mr(pd, malloc_huge(ALLOC_MEM_SIZE), ALLOC_MEM_SIZE, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE/* | IBV_ACCESS_REMOTE_READ*/);
    cwm(s_mr == NULL || r_mr == NULL, "Error on ibv_reg_mr");

    for(size_t i = 0; i < s_mr->length/sizeof(uint32_t); i++) {
        ((uint32_t*)s_mr->addr)[i] = htonl((1u << rank) << 24 | (uint32_t)i);
    }
    memset(r_mr->addr, 0, r_mr->length);

    printf("Init QP\n");
    ibv_qp *prev_qp = init_qp(ctx, pd);
    ibv_qp *next_qp = init_qp(ctx, pd);
    

    printf("Exchange data\n");
    agg_addr addr = {};
    addr.prev_qpn = prev_qp->qp_num;
    addr.next_qpn = next_qp->qp_num;
    if(! use_send) {
        addr.addr = (uint64_t)r_mr->addr;
        addr.rkey = r_mr->rkey;
    }
    addr.ip = bind_ip;
    addr.next_ip = 0;

    ibv_query_gid(ctx, PORT_ID, GID_INDEX, &addr.prev_gid);
    addr.next_gid = addr.prev_gid;


    printf("local info: prev_qpn %#x, next_qpn %#x, recv_mem_addr %#lx, rkey %#x, gid %s\n\n", 
        addr.prev_qpn, addr.next_qpn, addr.addr, addr.rkey, gid_to_str(addr.next_gid).c_str());

    addr = exch_addr(group_size, rank, addr, rank0_ip, rank0_port);

    printf("remote info: prev_qpn %#x, next_qpn %#x, recv_mem_addr %#lx, rkey %#x, prev_gid %s, next_gid %s\n\n", 
        addr.prev_qpn, addr.next_qpn, addr.addr, addr.rkey, gid_to_str(addr.prev_gid).c_str(), gid_to_str(addr.next_gid).c_str());

    printf("Connect QP\n");
    move_qp_to_rts(prev_qp, addr.prev_gid, addr.prev_qpn);
    move_qp_to_rts(next_qp, addr.next_gid, addr.next_qpn);

    printf("Testing\n");
    struct ibv_sge s_sge = {
        .addr = (uint64_t)s_mr->addr,
        .length = (uint32_t)msg_size,          
        .lkey = s_mr->lkey,
    };
    struct ibv_sge r_sge = {
        .addr = (uint64_t)r_mr->addr,
        .length = (uint32_t)msg_size,          
        .lkey = r_mr->lkey,
    };

    uint64_t ts = gettimeus();

    int iter_cnt = tot_iter_cnt;

    post_allreduce(use_send, iter_cnt==1, prev_qp, &r_sge, next_qp, &s_sge, &addr);
    
    for(int i = 1; i < iter_cnt; i++) {
        post_allreduce(use_send, i==iter_cnt-1, prev_qp, &r_sge, next_qp, &s_sge, &addr);

        if(20*i/iter_cnt != 20*(i-1)/iter_cnt) fprintf(stderr, "%d%%\n", 100*i/iter_cnt);

        poll_cq(next_qp->send_cq);
        if(use_send) poll_cq(prev_qp->recv_cq);
    }
    poll_cq(next_qp->send_cq);
    // write_imm or send
    poll_cq(prev_qp->recv_cq);
    
    printf("%.2lf Gbps\n", 1.0*msg_size*tot_iter_cnt*8*1e-3/(gettimeus()-ts));
    
    int len_in_byte = 512, len = len_in_byte/sizeof(uint32_t), tot_len = msg_size/sizeof(uint32_t);
    printf("first %d bytes\n", len_in_byte);
    for(int i = 0; i < len; i++) printf("%08x ", ntohl(((uint32_t*)r_mr->addr)[i]));
    printf("\n");
    if(msg_size > 2*len_in_byte) {
        printf("middle %d bytes\n", len_in_byte);
        for(int i = 0; i < len; i++) printf("%08x ", ntohl(((uint32_t*)r_mr->addr)[tot_len/2 - len/2 + i]));
        printf("\n");
    }
    if(msg_size > len_in_byte) {
        printf("last %d bytes\n", len_in_byte);
        for(int i = 0; i < len; i++) printf("%08x ", ntohl(((uint32_t*)r_mr->addr)[tot_len - len + i]));
        printf("\n");
    }

    sleep(1);// to avoid ACK loss???

    return 0;
}