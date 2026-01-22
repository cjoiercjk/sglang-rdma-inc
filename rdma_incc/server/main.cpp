#include "rdma.h"
#include "inc.hpp"
#include "utils.hpp"

void fallback_nccl(int group_size, int rank, uint32_t bind_ip, uint32_t rank0_ip, uint16_t rank0_port, size_t msg_size, size_t tot_round, unsigned int wait, string collective);

TxRxType get_txrx_type(int rank);

// Either use ibv_reg_mr_iova(iova=0) or ibv_reg_mr(IBV_ACCESS_ZERO_BASED)
// Do not cancel this flag, since IBV_ACCESS_ZERO_BASED has BUG
#define USE_IOVA 

vector<string> collective_choices = {"allreduce", "reduce", "broadcast", "reducescatter", "allgather", "alltoall", "barrier"};
vector<string> backend_choices = {"inc", "nccl"};

enum CollectiveType {
    ALLREDUCE,
    REDUCE,
    BROADCAST,
    // REDUCESCATTER, // experimental
    // ALLGATHER, // experimental
    // ALLTOALL, // experimental
    // BARRIER, 
};

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
    string collective;
    string backend;
    CollectiveType collective_type;
    unsigned int wait;
    int ret;

    argparse::ArgumentParser program(argv[0], "", argparse::default_arguments::help);
    // parse parameters
    program.add_argument("<group_size>").scan<'u', us>();
    program.add_argument("<rank>").scan<'u', us>();
    program.add_argument("<bind_ip>");
    program.add_argument("<rank0_ip>");
    program.add_argument("<collective>").action([&](const std::string& value) {
        if (std::find(collective_choices.begin(), collective_choices.end(), value) != collective_choices.end()) {
            return value;
        }
        throw std::runtime_error("Invalid collective");
    }).help("Supported collective: allreduce, reduce, broadcast, reducescatter, allgather, alltoall, barrier");
    program.add_argument("--backend").default_value("inc").action([&](const std::string& value) {
        if (std::find(backend_choices.begin(), backend_choices.end(), value) != backend_choices.end()) {
            return value;
        }
        throw std::runtime_error("Invalid backend");
    }).help("Supported backend: inc, nccl");
    program.add_argument("--port").metavar("<port>").default_value((us)12345).scan<'u', us>();
    program.add_argument("--op").metavar("<operation>").default_value("write").action([&](const std::string& value) {
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
    program.add_argument("--wait").default_value(30u).scan<'u', unsigned int>();

    program.add_description(string("Example: ") + argv[0] + " 2 0 192.168.1.1 10.0.0.1 allreduce");
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
    collective = program.get<string>("<collective>");
    backend = program.get<string>("--backend");
    wait = program.get<unsigned int>("--wait");
    if(backend == "nccl" || collective == "alltoall") {
        if(iter_cnt != 1) {
            printf("iter_cnt must be 1 for nccl\n");
            exit(1);
        }
        fallback_nccl(group_size, rank, bind_ip, rank0_ip, rank0_port, msg_size, tot_round, wait, collective);
        exit(0);
    }
    if(collective == "allreduce") {
        collective_type = CollectiveType::ALLREDUCE;
    }
    else if(collective == "reduce") {
        collective_type = CollectiveType::REDUCE;
    }
    else if(collective == "broadcast") {
        collective_type = CollectiveType::BROADCAST;
    }
    else if(collective == "reducescatter") {
        collective_type = CollectiveType::REDUCE;
        // reducescatter == world_size * sub_reduce
    }
    else if(collective == "allgather") {
        collective_type = CollectiveType::BROADCAST;
        // allgather == world_size * sub_broadcast
    }
    else if(collective == "barrier") {
        collective_type = CollectiveType::ALLREDUCE;
        // barrier == single-packet allreduce
        assert(msg_size == 256 && iter_cnt == 1);
    }

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

    if(rank == 0) {
        grpc::ChannelArguments ch_args;
        ch_args.SetInt(GRPC_ARG_ENABLE_HTTP_PROXY, 0);
        auto channel = grpc::CreateCustomChannel(controller_addr, grpc::InsecureChannelCredentials(), ch_args);
        stub = std::move(inc::INC::NewStub(channel));
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
    if((per_qp_switch_win_size & (per_qp_switch_win_size-1)) != 0) {
        printf("Invalid switch window size\n");
        exit(1);
    }
    bool need_reduce = collective_type == CollectiveType::REDUCE || collective_type == CollectiveType::ALLREDUCE;
    if(msg_size == 0 || msg_size > ALLOC_MEM_SIZE || 
        (need_reduce && msg_size*2 > per_qp_server_win_size)) {
        printf("Invalid message size\n");
        exit(1);
    }
    // Query device
    bind_ip = ntohl(inet_addr(bind_ip_str.c_str()));
    string dev_name = std::get<0>(get_device_by_ip(bind_ip));
    // string ib_dev_name = dev_to_ib_dev(dev_name);// This does not work in container
    string ib_dev_name;
    ibv_device *dev = NULL;
    uint8_t port_id, gid_index;
    query_ib_device_by_ip(bind_ip, &dev, &port_id, &gid_index);
    MYCHECK(dev == NULL, "Error on query_ib_hardware_by_ip");

    ib_dev_name = ibv_get_device_name(dev);
    printf("Find dev %d (%s), port_id %u, gid_index %u matches IP %s\n", ibv_get_device_index(dev), ib_dev_name.data(), port_id, gid_index, bind_ip_str.data());

    PORT_ID = port_id;
    GID_INDEX = gid_index;

    // Set affinity
    int socket_id = get_socket_by_pci(get_pci_by_dev(dev_name));
    vector<int> cpu_list = get_cpu_list_by_socket(socket_id);
    MYCHECK(cpu_list.empty(), "CPU list empty");

    printf("Bind cpu %d\n", cpu_list[0]);
    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(cpu_list[0], &mask);
    if (sched_setaffinity(0, sizeof(mask), &mask) == -1) {
        perror("sched_setaffinity");
        exit(1);
    } 

    // Open device
    // show_ib_devices();
    printf("Open device %s (%s)\n", ib_dev_name.c_str(), dev_name.c_str());
    // ibv_device *dev = find_ib_device(ib_dev_name);
    // MYCHECK(dev == NULL, "Error on find_ib_device");
    ibv_context *ctx = ibv_open_device(dev);
    MYCHECK(ctx == NULL, "Error on ibv_open_device");
    // ibv_comp_channel *comp_channel = ibv_create_comp_channel(ctx);
    // MYCHECK(comp_channel == NULL, "Error on ibv_create_comp_channel");

    if(need_reduce) {
        MTU = IBV_MTU_256;
    }
    else {
        ibv_port_attr port_attr;
        ret = ibv_query_port(ctx, PORT_ID, &port_attr);
        MYCHECK(ret, "Error on ibv_query_port");
        MTU = port_attr.active_mtu;
        if(MTU != IBV_MTU_4096) printf("WARNING: MTU is not 4096, perfermance may not be optimal!\n");
    }
    // MTU_SIZE is a macro of MTU
    printf("MTU: %d\n", MTU_SIZE);
    
    if(per_qp_switch_win_size % MTU_SIZE != 0 || per_qp_msg_size % MTU_SIZE != 0) {
        printf("Invalid switch window size or message size for MTU %d\n", MTU_SIZE);
        exit(1);
    }

    // Alloc PD
    printf("Alloc PD\n");
    ibv_pd *pd = ibv_alloc_pd(ctx);
    MYCHECK(pd == NULL, "Error on ibv_alloc_pd");
    
    // MR
#ifdef USE_IOVA
        // IBV_ACCESS_ZERO_BASED is not equal to use ibv_reg_mr_iova()
        // In my test, IBV_ACCESS_ZERO_BASED does not change local VA mapping like ibv_reg_mr_iova(), only has effect on the remote access
        // ibv_mr *s_mr = ibv_reg_mr(pd, malloc(ALLOC_MEM_SIZE), ALLOC_MEM_SIZE, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE/* | IBV_ACCESS_REMOTE_READ*/);
        // ibv_mr *r_mr = ibv_reg_mr(pd, malloc(ALLOC_MEM_SIZE), ALLOC_MEM_SIZE, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE/* | IBV_ACCESS_REMOTE_READ*/);
    // It seems that ibv_reg_mr_iova() needs the memory to be aligned to page size
    ibv_mr *s_mr = ibv_reg_mr_iova(pd, aligned_alloc(4<<10, ALLOC_MEM_SIZE), ALLOC_MEM_SIZE, 0, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE/* | IBV_ACCESS_REMOTE_READ*/);
    ibv_mr *r_mr = ibv_reg_mr_iova(pd, aligned_alloc(4<<10, ALLOC_MEM_SIZE), ALLOC_MEM_SIZE, 0, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE/* | IBV_ACCESS_REMOTE_READ*/);
#else 
    // BUG: Do not use this. IBV_ACCESS_ZERO_BASED does not work on my platform, still need actual VA for both side after setting this flag.
    ibv_mr *s_mr = ibv_reg_mr(pd, aligned_alloc(4<<10, ALLOC_MEM_SIZE), ALLOC_MEM_SIZE, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE/* | IBV_ACCESS_REMOTE_READ*/ | IBV_ACCESS_ZERO_BASED);
    ibv_mr *r_mr = ibv_reg_mr(pd, aligned_alloc(4<<10, ALLOC_MEM_SIZE), ALLOC_MEM_SIZE, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE/* | IBV_ACCESS_REMOTE_READ*/ | IBV_ACCESS_ZERO_BASED);
#endif
    MYCHECK(s_mr == NULL || r_mr == NULL, "Error on ibv_reg_mr");

    for(size_t i = 0; i < s_mr->length/sizeof(uint32_t); i++) {
        ((uint32_t*)s_mr->addr)[i] = htonl((1u << rank) << 24 | (uint32_t)i);
    }
    memset(r_mr->addr, 0, r_mr->length);

    printf("Query port_id & gid_index\n");
    RankAddr addr = {};
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

    // ibv_device_attr dev_attr;
    // ibv_port_attr port_attr;
    // ret = ibv_query_device(ctx, &dev_attr);
    // MYCHECK(ret, "Error on ibv_query_device");
    // for(uint8_t i = 1; i <= dev_attr.phys_port_cnt; i++) {
    //     ret = ibv_query_port(ctx, i, &port_attr);
    //     MYCHECK(ret, "Error on ibv_query_port");
    //     for(int j = 1; j < port_attr.gid_tbl_len; j += 2) {// only query RoCEv2
    //         ibv_gid gid;
    //         ret = ibv_query_gid(ctx, i, j, &gid);
    //         if(gid_to_ipv4(gid) != bind_ip) continue;
    //         PORT_ID = i;
    //         GID_INDEX = j;
    //         break;
    //     }
    // }
    // printf("Using port_id %u, gid_index %u\n", PORT_ID, GID_INDEX);


    printf("Init QP\n");
    ibv_qp **qp = new ibv_qp *[qp_num];
    for(int i = 0; i < qp_num; i++) qp[i] = init_qp(ctx, pd);

    printf("Exchange data\n");
    for(int i = 0; i < qp_num; i++) addr.qpn[i] = qp[i]->qp_num;
    addr.ip = bind_ip;
    addr.rkey = r_mr->rkey;

    printf("local info: send_mem_addr %p, rkey %#x, recv_mem_addr %p, rkey %#x, gid %s\n", 
        s_mr->addr, s_mr->rkey, r_mr->addr, r_mr->rkey, gid_to_str(ipv4_to_gid(addr.ip)).c_str());
    printf("            qpn ");
    for(int i = 0; i < qp_num; i++) printf("%#x%c", addr.qpn[i], ",\n"[i==qp_num-1]);
    printf("\n");

    socket_init(group_size, rank, rank0_ip, rank0_port, wait);
    addr = exch_addr(addr, qp_num, controller_addr, per_qp_switch_win_size);

    printf("remote info: gid %s\n", gid_to_str(ipv4_to_gid(addr.ip)).c_str());
    printf("             qpn ");
    for(int i = 0; i < qp_num; i++) printf("%#x%c", addr.qpn[i], ",\n"[i==qp_num-1]);
    printf("\n");

    printf("Connect QP\n");
    for(int i = 0; i < qp_num; i++) move_qp_to_rts(qp[i], ipv4_to_gid(addr.ip), addr.qpn[i]);

    printf("Barrier\n");
    socket_barrier();

    printf("Testing\n");
    struct ibv_sge s_sge = {
        .addr = (uint64_t)0, 
        .length = (uint32_t)0,          
        .lkey = s_mr->lkey,
    };
    struct ibv_sge r_sge = {
        .addr = (uint64_t)0, 
        .length = (uint32_t)0,          
        .lkey = r_mr->lkey,
    };
    struct MemoryAddress rdma_addr = {
        .memory_address = 0, // both iova and IBV_ACCESS_ZERO_BASED has remote address 0
        .rkey = addr.rkey,
    };

    uint64_t ts = gettimeus();
    TxRxType txrx_type;
    if(collective_type == CollectiveType::REDUCE) {
        txrx_type = rank == 0 ? TxRxType::RX : TxRxType::TX;
    }
    else if(collective_type == CollectiveType::ALLREDUCE) {
        txrx_type = TxRxType::TXRX;
    }
    else if(collective_type == CollectiveType::BROADCAST) {
        txrx_type = rank == 0 ? TxRxType::TX : TxRxType::RX;
    }

    for(int round = 0; round < tot_round; round ++) {
        //printf("round %d\n", round);
        size_t scnt = 0; //, ccnt = 0;
        // unique_ptr<size_t[]>sub_s_ccnt(new size_t[qp_num] ());
        // unique_ptr<size_t[]>sub_r_ccnt(new size_t[qp_num] ());
        unique_ptr<size_t[]>tx_queue_depth(new size_t[qp_num] ());
        unique_ptr<size_t[]>rx_queue_depth(new size_t[qp_num] ());
        
        while(1) {
            size_t max_queue_depth = 0;
            for(int i = 0; i < qp_num; i++) {
                max_queue_depth = std::max(max_queue_depth, std::max(tx_queue_depth[i], rx_queue_depth[i]));
            }
            if (scnt == iter_cnt && max_queue_depth == 0) break;

            size_t max_qsize;
            if(need_reduce) max_qsize = std::min((size_t)Q_SIZE, per_qp_server_win_size/per_qp_msg_size);
            else max_qsize = (size_t)Q_SIZE;
            size_t push_cnt = std::min(max_qsize - max_queue_depth, iter_cnt - scnt);
            // size_t prev_ccnt = ccnt;
            scnt += push_cnt;
            // size_t new_ccnt = scnt;
            struct ibv_sge sub_s_sge = s_sge;
            struct ibv_sge sub_r_sge = r_sge;
            struct MemoryAddress sub_rdma_addr = rdma_addr;

            bool notify_last;
            if(collective_type == CollectiveType::ALLREDUCE) 
                notify_last = 0; // Since we write to ourselves, we don't have to notify the receiver.
            else if(collective_type == CollectiveType::REDUCE) 
                notify_last = scnt == iter_cnt; // last segment
            else if(collective_type == CollectiveType::BROADCAST)
                notify_last = scnt == iter_cnt;

            for(int i = 0; i < qp_num; i++) {
                uint32_t sub_size = per_qp_msg_size;
                sub_s_sge.length = sub_size;
                sub_r_sge.length = sub_size;

                push_qp(qp[i], &sub_s_sge, &sub_r_sge, &sub_rdma_addr, push_cnt, use_send, notify_last, txrx_type, tx_queue_depth[i], rx_queue_depth[i]);
                
                // if(use_send && sub_r_ccnt[i] < ccnt) ccnt = sub_r_ccnt[i];

                sub_s_sge.addr += sub_size;
                sub_r_sge.addr += sub_size;
                sub_rdma_addr.memory_address += sub_size;
            }   
            // ccnt = new_ccnt;
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