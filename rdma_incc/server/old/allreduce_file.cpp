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
#include <thread>
#include <vector>
#include <csignal>
#include <fcntl.h>
#include <sys/stat.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sched.h>
#include <arpa/inet.h>
#include <sys/mman.h>


#include <infiniband/verbs.h>
#include <grpcpp/grpcpp.h>

#include "allreduce.grpc.pb.h"
#include "dep/argparse/argparse.hpp"
#include "dep/jsoncpp/json/json.h"

#define ENABLE_IB_UTILS
#include "net_utils.hpp"
#include "yyt_error.h"
// some assumption of the network
uint32_t PORT_ID; 
#define LID 0
uint32_t GID_INDEX;


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
#define MTU_SIZE (128*(1<<MTU))

#define ALLOC_MEM_SIZE (1<<30)

#define MAX_Q_SIZE 128
const int Q_SIZE = 128;
// Small Q_SIZE will cause few WRs in recv_queue,
// leading to performance degradation of SEND due to message level flow control,
// especailly for small messages

#define MAX_QP_NUM 8 // I think 2 is enough
#define TINY_MESSAGE_LIM (MTU_SIZE*4)
#define HUGE_MESSAGE_LIM (MTU_SIZE*16)

#define MAX_GROUP_NUM 1024

// need GID & QPN for connection
// need VA & RKEY for RDMA operations
using std::swap;
using std::string;
using std::unique_ptr;
using std::map;
using std::to_string;
using std::ifstream;
using std::stoi, std::stoul;
using std::vector;

typedef unsigned short us;

struct agg_addr {
    union ibv_gid gid;
    // assume lid==0 && psn==0
    uint32_t ip;
    int qpn[MAX_QP_NUM];
};

void *malloc_huge(size_t size)
{
	void *ret = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS/* | MAP_HUGETLB*/, -1, 0);
	MYCHECK(ret==NULL, "Error on mmap");
	return ret;
}

vector<uint32_t>group_ids;
unique_ptr<inc::INC::Stub> stub;
/*
 * return a positive groud_id
 * return 0 on error
 */
uint32_t inc_create_group(uint32_t *ip, int *qpn, int size) 
{
    // inc::INC
    grpc::ClientContext ctx;
    inc::CreateGroupRequest req;
    inc::CreateGroupReply rep;

    for(int i = 0; i < size; i++) {
        inc::MemberInfo *mem = req.add_member();
        mem->set_ip(ip[i]);
        mem->set_qpn(qpn[i]);
    }
    grpc::Status status = stub->CreateGroup(&ctx, req, &rep);
    if(!status.ok()) {
        fprintf(stderr, "CreateGroup Error: %d\n", status.error_code());
    }
    return rep.groupid();
}

// return true on success
bool inc_destroy_group(uint32_t group_id) 
{
    grpc::ClientContext ctx;
    inc::DestroyGroupRequest req;
    inc::DestroyGroupReply rep;

    req.set_groupid(group_id);
    grpc::Status status = stub->DestroyGroup(&ctx, req, &rep);
    return true;
}

void return_groups()
{
    for(auto i: group_ids) inc_destroy_group(i);
    printf("groups returned\n");
    fflush(stdout);
}

void signalHandler(int signum) {
    std::cout << "Signal received: " << signum << std::endl;
    return_groups();
    exit(signum);
}

agg_addr exch_addr(int group_size, int rank, agg_addr my_addr, int qp_num, 
    uint32_t ip, uint16_t port, string config_file_dir, string controller_addr)
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
    agg_addr neighbor_addr;

    if(rank == 0) {
        // read config files
        ifstream topo_file(config_file_dir + "/topo.json");
        ifstream allreduce_file(config_file_dir + "/allreduce.json");
        Json::Reader reader;
        Json::Value topo_json, allreduce_json;
        MYCHECK(!reader.parse(topo_file, topo_json), "Error on parsing json");
        MYCHECK(!reader.parse(allreduce_file, allreduce_json), "Error on parsing json");

        
        int *fd_list = new int[group_size];
        for(int i = 0; i < group_size; i++) fd_list[i] = -1;

        agg_addr *addr_list = new agg_addr[group_size];

        ret = bind(fd, (struct sockaddr *)&rank0_addr, sizeof(rank0_addr));
        MYCHECK(ret < 0, "Error on bind()");
        ret = listen(fd, group_size);
        MYCHECK(ret < 0, "Error on listen()");
        // gather
        addr_list[0] = my_addr;
        for(int i = 1; i < group_size; i++) {
            int conn_fd = accept(fd, NULL, NULL);
            MYCHECK(conn_fd < 0, "Error on accept()");
            int client_rank;
            ret = read(conn_fd, &client_rank, sizeof(client_rank));
            MYCHECK(!(0<client_rank && client_rank<group_size), "Rank error");
            MYCHECK(fd_list[client_rank] != -1, "Rank error");
            MYCHECK(ret < sizeof(client_rank), "Partial read()");

            fd_list[client_rank] = conn_fd;
            ret = read(conn_fd, &addr_list[client_rank], sizeof(agg_addr));
            MYCHECK(ret < sizeof(agg_addr), "Partial read()");
        }        
        // switch address
        agg_addr switch_addr = {};
        switch_addr.ip = (uint32_t)stoul(allreduce_json["switch_IP"].asString(), NULL, 16);
        switch_addr.gid = ipv4_to_gid(switch_addr.ip);
        // qpn is the same as the sender's

        for(int qp_idx = 0; qp_idx < qp_num; qp_idx++) {
            unique_ptr<uint32_t[]>ip(new uint32_t[group_size]);
            unique_ptr<int[]>qpn(new int[group_size]);
            for(int i = 0; i < group_size; i++) {
                ip[i] = addr_list[i].ip;
                qpn[i] = addr_list[i].qpn[qp_idx];
            }
            uint32_t group_id = inc_create_group(ip.get(), qpn.get(), group_size);
            if(group_id == 0) {// error or no resources
                fprintf(stderr, "No switch resources\n");
                return_groups();
                exit(1);
            }
            group_ids.push_back(group_id);
        }
        
        // print 
        /*
        srand(time(0));
        auto recir_port_set = allreduce_json["recir_port"];
        int recir_port = stoi(recir_port_set[rand()%recir_port_set.size()].asString());

        unique_ptr<int[]>group_id(new int[qp_num]);

        for(int qp_idx = 0; qp_idx < qp_num; qp_idx++) {
            group_id[qp_idx] = rand() % MAX_GROUP_NUM;

            printf("bfrt.rdma_allreduce.pipe.Ingress.sendout.recirculate_table.add_with_forward(%d, %d)\n", 
                group_id[qp_idx], recir_port);

            for(int i = 0; i < group_size; i++) {
                printf("bfrt.rdma_allreduce.pipe.Ingress.metadata_table.add_with_get_allreduce_metadata\\\n\
    (sip=%#x, dip=%#x, dqpn=%#x, group_id=%d, rank=%d, bitmap=%#x, bitmap_mask=%#x, agg_addr=%#x, agg_addr_offset_mask=%#x)\n",
                addr_list[i].ip, switch_addr.ip, addr_list[i].qpn[qp_idx], group_id[qp_idx], i, 1<<i, (1<<group_size)-1, 
                qp_idx*per_qp_switch_win_size/MTU_SIZE, per_qp_switch_win_size/MTU_SIZE-1);
    //             printf("bfrt.rdma_allreduce.pipe.Ingress.metadata_table.add_with_get_allreduce_backward_metadata\\\n\
    // (sip=%#x, dip=%#x, dqpn=%#x, is_forward=0)\n", addr_list[i].ip, switch_addr.ip, addr_list[i].qpn[qp_idx]);
                printf("bfrt.rdma_allreduce.pipe.Egress.restore_table.add_with_restore_fields\\\n\
    (group_id=%d, src_rank=%d, sip=%#x, dip=%#x, dqpn=%#x)\n",
                group_id[qp_idx], i, switch_addr.ip, addr_list[i].ip, addr_list[i].qpn[qp_idx]);
            }
            
            unique_ptr<int[]>node_id(new int[group_size]);
            string node_ids;
            map<uint32_t, int>p4_port;
            for(auto i = 0; i < topo_json["port"].size(); i++) 
                p4_port[(uint32_t)stoul(topo_json["IP"][i].asString(), NULL, 16)] = (int)stoi(topo_json["port"][i].asString());
                

            printf("# NOTE: rid == src_rank in allreduce\n");

            for(int i = 0; i < group_size; i++) {
                node_id[i] = rand();
                printf("bfrt.pre.node.add(%d, %d, None, [%d])\n", node_id[i], i, p4_port[addr_list[i].ip]);// go back to sender
                if(i) node_ids += ", ";
                node_ids += to_string(node_id[i]);
            }
            printf("bfrt.pre.mgid.add(%d, [%s], [False]*%d, [0]*%d)\n",
                group_id[qp_idx], node_ids.c_str(), group_size, group_size);
        }
        printf("bfrt.rdma_allreduce.pipe.Ingress.reg_bitmap.clear()\n");
        // printf("bfrt.rdma_allreduce.pipe.Ingress.reg_ackmap.clear()\n");
        */

        // exchange
        for(int i = 0; i < group_size; i++) {
            addr_list[i].gid = switch_addr.gid;
            addr_list[i].ip = switch_addr.ip;
        }

        // printf("\n");
        // printf("Press any key to continue\n");
        // getchar();
        // printf("\n");

        // scatter
        for(int i = 1; i < group_size; i++) {
            ret = write(fd_list[i], &addr_list[i], sizeof(addr_list[i]));
            MYCHECK(ret < sizeof(addr_list[i]), "Partial write()");
            close(fd_list[i]);
        }
        neighbor_addr = addr_list[0];
        delete[] addr_list;
        delete[] fd_list;
    }
    else {
        ret = connect(fd, (struct sockaddr *)&rank0_addr, sizeof(rank0_addr));
        MYCHECK(ret < 0, "Error on connect()");
        ret = write(fd, &rank, sizeof(rank));
        MYCHECK(ret < sizeof(rank), "Partial write()");
        ret = write(fd, &my_addr, sizeof(my_addr));
        MYCHECK(ret < sizeof(my_addr), "Partial write()");
        ret = read(fd, &neighbor_addr, sizeof(neighbor_addr));
        MYCHECK(ret < sizeof(neighbor_addr), "Partial read()");
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

uint64_t gettimeus()
{
	timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec*1000000 + t.tv_nsec/1000;
}

void post_allreduce(ibv_sge *s_sge, ibv_sge *r_sge, uint32_t r_rkey, ibv_qp *qp, bool use_send, bool notify)
{
    int ret;
    if(use_send || notify) {
        struct ibv_recv_wr wr = {}, *bad_wr;
        wr.wr_id = 1;
        wr.next = NULL;
        wr.sg_list = r_sge;
        wr.num_sge = 1;
        ret = ibv_post_recv(qp, &wr, &bad_wr);
        if(ret != 0) fprintf(stderr, "%d\n", ret);
        MYCHECK(ret != 0, "Error on ibv_post_recv");
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
        wr.wr.rdma.remote_addr = r_sge->addr;
        wr.wr.rdma.rkey = r_rkey;
    }
    ret = ibv_post_send(qp, &wr, &bad_wr);
    MYCHECK(ret != 0, "Error on ibv_post_send");
}

void post_allreduce(ibv_sge *s_sge, ibv_sge *r_sge, uint32_t r_rkey, ibv_qp *qp)
{
    post_allreduce(s_sge, r_sge, r_rkey, qp, true, false);
}

int poll_cq(ibv_cq *cq)
{
    static struct ibv_wc wc[MAX_Q_SIZE];
    int ret = ibv_poll_cq(cq, Q_SIZE, wc);
    MYCHECK(ret < 0, "Error on ibv_poll_cq");
    for(int i = 0; i < ret; i++) {
        if(wc[i].status != IBV_WC_SUCCESS) {
            fprintf(stderr, "%d\n", (int)wc[i].status);
            MYCHECK(wc[i].status != IBV_WC_SUCCESS, "Error on ibv_poll_cq");
        }
    }
    return ret;
}

void push_qp(bool use_send, bool notify_last, ibv_qp *qp, size_t push_cnt, 
    size_t &s_ccnt, size_t &r_ccnt, ibv_sge *s_sge, ibv_sge *r_sge, uint32_t r_rkey)
{
    for(size_t i = 0; i < push_cnt; i++) {
        post_allreduce(s_sge, r_sge, r_rkey, qp);
        // post_allreduce(s_sge, r_sge, r_rkey, qp, use_send, notify_last && i==push_cnt-1);
    }
    
    // We prefer to clear out CQ rather than send new requests, so we use a loop here
    int new_ccnt;
    new_ccnt = poll_cq(qp->recv_cq);
    r_ccnt += new_ccnt;
    new_ccnt = poll_cq(qp->send_cq);
    s_ccnt += new_ccnt;
}

int main(int argc, char **argv)
{
    signal(SIGINT, signalHandler);
    signal(SIGQUIT, signalHandler);
    signal(SIGTERM, signalHandler);

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
    size_t iter_cnt;
    size_t tot_round;
    size_t per_qp_switch_win_size;
    size_t per_qp_server_win_size;
    size_t per_qp_msg_size;
    string controller_addr;
    string config_file_dir;
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
    }).help("Supported operation: write, send");
    program.add_argument("--qp").metavar("<qp_num>").scan<'u', us>().help(
        "Note: If this value > 1, a message will be sliced into multiple sub-messages. \
Suggest leave this to the program to decide."
    );
    program.add_argument("-s").metavar("<message_size>").default_value(1u<<15).scan<'u', unsigned int>();
    program.add_argument("-n").metavar("<num_of_message_per_round>").default_value(10u*16*1024).scan<'u', unsigned int>();
    program.add_argument("-r").metavar("<round>").default_value(1u).scan<'u', unsigned int>().help(
        "The program will clean out CQs before every round begins."
    );
    program.add_argument("--controller").default_value("").metavar("<IP:port>");
    program.add_argument("--switch-win-size").default_value(1u<<17).scan<'u', unsigned int>();
    program.add_argument("<config_file_dir>");

    program.add_description(string("Example: ") + argv[0] + " 2 0 192.168.1.1 10.0.0.1 ../common");
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
    iter_cnt = (size_t)program.get<unsigned int>("-n");
    tot_round = (size_t)program.get<unsigned int>("-r");
    if(program.is_used("--qp")) {
        qp_num = (int)program.get<us>("--qp");
    }
    else {
        qp_num = msg_size >= HUGE_MESSAGE_LIM ? 2 : 1;
    }
    if(!program.is_used("--controller") && rank == 0) {
        printf("Rank 0 must specify controller address\n");
        exit(1);
    }
    controller_addr = program.get<string>("--controller");
    config_file_dir = program.get<string>("<config_file_dir>");

    if(rank == 0) {
        stub = std::move(inc::INC::NewStub(grpc::CreateChannel(controller_addr, grpc::InsecureChannelCredentials())));
    }

    per_qp_msg_size = msg_size / qp_num;
    per_qp_switch_win_size = (size_t)program.get<unsigned int>("--switch-win-size");
    per_qp_server_win_size = per_qp_switch_win_size / 2;

    if(group_size < 1 || group_size > 32 || rank >= group_size) {
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
    if(iter_cnt == 0) {
        printf("Invalid number of iterations\n");
        exit(1);
    }
    if(msg_size % qp_num) {
        printf("msg_size must be multiple of qp_num\n");
        exit(1);
    }
    if((per_qp_switch_win_size&(per_qp_switch_win_size-1)) != 0 || per_qp_switch_win_size % MTU_SIZE != 0) {
        printf("Invalid switch window size\n");
        exit(1);
    }
    if(msg_size == 0 || msg_size > ALLOC_MEM_SIZE || msg_size*2 > per_qp_server_win_size ||
        per_qp_msg_size % MTU_SIZE != 0) {
        printf("Invalid message size\n");
        exit(1);
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
    show_devices();
    printf("Open device %s (%s)\n", ib_dev_name.c_str(), dev_name.c_str());
    ibv_device *dev = find_device(ib_dev_name);
    MYCHECK(dev == NULL, "Error on find_device");
    ibv_context *ctx = ibv_open_device(dev);
    MYCHECK(ctx == NULL, "Error on ibv_open_device");
    // ibv_comp_channel *comp_channel = ibv_create_comp_channel(ctx);
    // MYCHECK(comp_channel == NULL, "Error on ibv_create_comp_channel");
    
    // ibv_port_attr port_attr;
    // ibv_query_port(ctx, PORT_ID, &port_attr);
    // printf("LID: %d\n", port_attr.lid);

    // PD
    printf("Alloc PD\n");
    ibv_pd *pd = ibv_alloc_pd(ctx);
    MYCHECK(pd == NULL, "Error on ibv_alloc_pd");
    
    // MR
    printf("Register MR\n");
    int send_fd = open("allreduce_data.bin", O_RDWR, 0);
    struct stat fileStat;
    ret = fstat(send_fd, &fileStat);
    MYCHECK(ret != 0, "Error on fstat");
    off_t fileSize = fileStat.st_size;


    if(msg_size > fileSize) {
        msg_size = fileSize;
        printf("Shrink msg_size to %d\n", msg_size);
        per_qp_msg_size = msg_size / qp_num;
        // no further check
    }

    // ibv_mr *s_mr = ibv_reg_dmabuf_mr(pd, 0/*offset*/, fileSize, 0/*iova*/, send_fd, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE/* | IBV_ACCESS_REMOTE_READ*/);
    ibv_mr *s_mr = ibv_reg_dmabuf_mr(pd, 0, 0, 0, -1, 0);
    if(errno == EOPNOTSUPP || errno == EPROTONOSUPPORT) printf("NO SUPPORT\n");
    MYCHECK(s_mr == NULL, "Error on ibv_reg_dmabuf_mr");
    ibv_mr *r_mr = ibv_reg_mr(pd, malloc_huge(ALLOC_MEM_SIZE), ALLOC_MEM_SIZE, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE/* | IBV_ACCESS_REMOTE_READ*/);
    MYCHECK(r_mr == NULL, "Error on ibv_reg_mr");

    // for(size_t i = 0; i < s_mr->length/sizeof(uint32_t); i++) {
    //     ((uint32_t*)s_mr->addr)[i] = htonl((1u << rank) << 24 | (uint32_t)i);
    // }
    memset(r_mr->addr, 0, r_mr->length);

    printf("Query port_id & gid_index\n");
    agg_addr addr = {};
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
    for(uint8_t i = 1; i <= dev_attr.phys_port_cnt; i++) {
        ret = ibv_query_port(ctx, i, &port_attr);
        MYCHECK(ret, "Error on ibv_query_port");
        for(int j = 1; j < port_attr.gid_tbl_len; j += 2) {// only query RoCEv2
            ret = ibv_query_gid(ctx, i, j, &addr.gid);
            if(gid_to_ipv4(addr.gid) != bind_ip) continue;
            PORT_ID = i;
            GID_INDEX = j;
            break;
        }
    }
    printf("port_id %u, gid_index %u\n", PORT_ID, GID_INDEX);


    printf("Init QP\n");
    ibv_qp **qp = new ibv_qp *[qp_num];
    for(int i = 0; i < qp_num; i++) qp[i] = init_qp(ctx, pd);

    printf("Exchange data\n");
    for(int i = 0; i < qp_num; i++) addr.qpn[i] = qp[i]->qp_num;
    addr.ip = bind_ip;

    printf("local info: send_mem_addr %#lx, rkey %#x, recv_mem_addr %#lx, rkey %#x, gid %s\n", 
        (unsigned long)s_mr->addr, s_mr->rkey, (unsigned long)r_mr->addr, r_mr->rkey, gid_to_str(addr.gid).c_str());
    printf("            qpn ");
    for(int i = 0; i < qp_num; i++) printf("%#x%c", addr.qpn[i], ",\n"[i==qp_num-1]);
    printf("\n");

    addr = exch_addr(group_size, rank, addr, qp_num, rank0_ip, rank0_port, config_file_dir, controller_addr);

    printf("remote info: gid %s\n", gid_to_str(addr.gid).c_str());
    printf("             qpn ");
    for(int i = 0; i < qp_num; i++) printf("%#x%c", addr.qpn[i], ",\n"[i==qp_num-1]);
    printf("\n");

    printf("Connect QP\n");
    for(int i = 0; i < qp_num; i++) move_qp_to_rts(qp[i], addr.gid, addr.qpn[i]);

    printf("Testing\n");
    struct ibv_sge s_sge = {
        .addr = (uint64_t)s_mr->addr,
        .length = (uint32_t)0,          
        .lkey = s_mr->lkey,
    };
    struct ibv_sge r_sge = {
        .addr = (uint64_t)r_mr->addr,
        .length = (uint32_t)0,          
        .lkey = r_mr->lkey,
    };

    uint64_t ts = gettimeus();

    // post_allreduce(use_send, iter_cnt==1, prev_qp, &r_sge, next_qp, &s_sge, &addr);
    
    // for(int i = 1; i < iter_cnt; i++) {
    //     post_allreduce(use_send, i==iter_cnt-1, prev_qp, &r_sge, next_qp, &s_sge, &addr);

    //     if(20*i/iter_cnt != 20*(i-1)/iter_cnt) fprintf(stderr, "%d%%\n", 100*i/iter_cnt);

    //     poll_cq(next_qp->send_cq);
    //     if(use_send) poll_cq(prev_qp->recv_cq);
    // }
    // poll_cq(next_qp->send_cq);
    // // write_imm or send
    // poll_cq(prev_qp->recv_cq);
    for(int round = 0; round < tot_round; round ++) {
        //printf("round %d\n", round);
        size_t scnt = 0, ccnt = 0;
        unique_ptr<size_t[]>sub_s_ccnt(new size_t[qp_num] ());
        unique_ptr<size_t[]>sub_r_ccnt(new size_t[qp_num] ());
        while(ccnt < iter_cnt) {
            size_t push_cnt = std::min(std::min((size_t)Q_SIZE, per_qp_server_win_size/per_qp_msg_size) - (scnt - ccnt), iter_cnt - scnt);
            size_t prev_ccnt = ccnt;
            scnt += push_cnt;
            bool notify_last = 0;// Since we write to ourselves, we don't have to notify the receiver.
            ccnt = iter_cnt;
            struct ibv_sge sub_s_sge = s_sge;
            struct ibv_sge sub_r_sge = r_sge;

            for(int i = 0; i < qp_num; i++) {
                uint32_t sub_size = per_qp_msg_size;
                sub_s_sge.length = sub_size;
                sub_r_sge.length = sub_size;

                push_qp(use_send, notify_last, qp[i], push_cnt, sub_s_ccnt[i], sub_r_ccnt[i], &sub_s_sge, &sub_r_sge, r_mr->rkey);
                if(sub_s_ccnt[i] < ccnt) ccnt = sub_s_ccnt[i];
                if(use_send && sub_r_ccnt[i] < ccnt) ccnt = sub_r_ccnt[i];

                sub_s_sge.addr += sub_size;
                sub_r_sge.addr += sub_size;
            }   
            // if(10*ccnt/iter_cnt != 10*prev_ccnt/iter_cnt) 
            //     printf("%ld%%\n", 100*ccnt/iter_cnt);
        }
    }
        

    printf("%.2lf Gbps\n", 1.0*msg_size*iter_cnt*tot_round*8*1e-3/(gettimeus()-ts));
    
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
    return_groups();
    return 0;
}