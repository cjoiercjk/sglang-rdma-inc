#include "rdma.h"
#include "inc.hpp"
#include "utils.hpp"

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
    size_t iter_cnt;
    size_t tot_round;
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
    program.add_argument("-s").metavar("<message_size>").default_value(1u<<20).scan<'u', unsigned int>();
    program.add_argument("-n").metavar("<num_of_message_per_round>").default_value(100u<<10).scan<'u', unsigned int>();
    program.add_argument("-r").metavar("<round>").default_value(1u).scan<'u', unsigned int>().help(
        "The program will clean out CQs before every round begins."
    );
    // program.add_argument("--q_size").metavar("<q_size>").default_value((us)MAX_Q_SIZE).scan<'u', us>();
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
    // Q_SIZE = (int)program.get<us>("--q_size");
    config_file_dir = program.get<string>("<config_file_dir>");

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
    if(iter_cnt == 0) {
        printf("Invalid number of iterations\n");
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
    // ibv_device *dev = find_ib_device(ib_dev_name);
    MYCHECK(dev == NULL, "Error on find_device");
    ibv_context *ctx = ibv_open_device(dev);
    MYCHECK(ctx == NULL, "Error on ibv_open_device");
    // ibv_comp_channel *comp_channel = ibv_create_comp_channel(ctx);
    // MYCHECK(comp_channel == NULL, "Error on ibv_create_comp_channel");
    
    // set MTU
    // ibv_port_attr port_attr;
    // ibv_query_port(ctx, PORT_ID, &port_attr);
    // printf("LID: %d\n", port_attr.lid);
    // MTU = port_attr.active_mtu;
    // printf("MTU: %d\n", 128*(1<<MTU));
    // if(MTU != IBV_MTU_4096) printf("WARNING: MTU is not 4096, perfermance may not be optimal!\n");

    // PD
    printf("Alloc PD\n");
    ibv_pd *pd = ibv_alloc_pd(ctx);
    MYCHECK(pd == NULL, "Error on ibv_alloc_pd");
    
    // MR
    printf("Register MR\n");
    ibv_mr *mr = ibv_reg_mr(pd, malloc(ALLOC_MEM_SIZE), ALLOC_MEM_SIZE, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE/* | IBV_ACCESS_REMOTE_READ*/);
    MYCHECK(mr == NULL, "Error on ibv_reg_mr");

    for(size_t i = 0; i < mr->length/sizeof(uint32_t); i++) {
        ((uint32_t*)mr->addr)[i] = htonl((1u << rank) << 24 | (uint32_t)i);
    }


    printf("Query port_id & gid_index\n");
    bc_addr addr = {};
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
            MTU = port_attr.active_mtu;
            break;
        }
    }
    printf("port_id %u, gid_index %u\n", PORT_ID, GID_INDEX);
    printf("MTU: %d\n", 128*(1<<MTU));
    if(MTU != IBV_MTU_4096) printf("WARNING: MTU is not 4096, perfermance may not be optimal!\n");


    printf("Init QP\n");
    ibv_qp **qp = new ibv_qp *[qp_num];
    for(int i = 0; i < qp_num; i++) qp[i] = init_qp(ctx, pd);

    printf("Exchange data\n");
    
    for(int i = 0; i < qp_num; i++) addr.qpn[i] = qp[i]->qp_num;
    if(! use_send) {
        addr.addr = (uint64_t)mr->addr;
        addr.rkey = mr->rkey;
    }
    addr.ip = bind_ip;

    printf("local info: mem_addr %#lx, rkey %#x, gid %s\n", 
        addr.addr, addr.rkey, gid_to_str(addr.gid).c_str());
    printf("            qpn ");
    for(int i = 0; i < qp_num; i++) printf("%#x%c", addr.qpn[i], ",\n"[i==qp_num-1]);
    printf("\n");

    mem_array_t mem_array;
    std::tie(addr, mem_array) = exch_addr(group_size, rank, addr, qp_num, rank0_ip, rank0_port, config_file_dir);

    printf("remote info: mem_addr %#lx, rkey %#x, gid %s\n", 
        addr.addr, addr.rkey, gid_to_str(addr.gid).c_str());
    printf("             qpn ");
    for(int i = 0; i < qp_num; i++) printf("%#x%c", addr.qpn[i], ",\n"[i==qp_num-1]);
    printf("\n");

    printf("Connect QP\n");
    for(int i = 0; i < qp_num; i++) move_qp_to_rts(qp[i], addr.gid, addr.qpn[i]);

    printf("Testing\n");
    struct ibv_sge bc_sge = {
        .addr = (uint64_t)mr->addr,
        .length = (uint32_t)msg_size,          
        .lkey = mr->lkey,
    };
    
    uint64_t ts = gettimeus();

    bool is_root = rank == 0;
    
    if(!use_send && !is_root) iter_cnt = 1;

    for(int round = 0; round < tot_round; round ++) {
        size_t scnt = 0, ccnt = 0;
        unique_ptr<size_t[]>sub_ccnt(new size_t[qp_num] ());

        while(ccnt < iter_cnt) {
            size_t push_cnt = std::min(Q_SIZE - (scnt - ccnt), iter_cnt - scnt);
            size_t prev_ccnt = ccnt;
            scnt += push_cnt;
            bool notify_last = scnt == iter_cnt;
            ccnt = iter_cnt;
            struct ibv_sge sub_sge = bc_sge;
            struct mem_array_t sub_mem_array = mem_array;

            for(int i = 0; i < qp_num; i++) {
                uint32_t sub_size = msg_size / qp_num + (i < msg_size % qp_num);
                sub_sge.length = sub_size;

                push_qp(is_root, use_send, notify_last, qp[i], push_cnt, sub_ccnt[i], &sub_sge, &sub_mem_array);
                if(sub_ccnt[i] < ccnt) ccnt = sub_ccnt[i];

                sub_sge.addr += sub_size;
                if(!use_send) {
                    for(uint16_t j = 0; j < sub_mem_array.size; j++)
                        sub_mem_array.mem_info[j].addr += sub_size;
                }
            }   
                
            // if(is_root && 10*ccnt/iter_cnt != 10*prev_ccnt/iter_cnt) 
            //     printf("%ld%%\n", 100*ccnt/iter_cnt);
        }
    }
    

    printf("%.2lf Gbps\n", 1.0*msg_size*iter_cnt*tot_round*8*1e-3/(gettimeus()-ts));
    
    int len_in_byte = 512, len = len_in_byte/sizeof(uint32_t), tot_len = msg_size/sizeof(uint32_t);
    printf("first %d bytes\n", len_in_byte);
    for(int i = 0; i < len; i++) printf("%08x ", ntohl(((uint32_t*)mr->addr)[i]));
    printf("\n");
    if(msg_size > 2*len_in_byte) {
        printf("middle %d bytes\n", len_in_byte);
        for(int i = 0; i < len; i++) printf("%08x ", ntohl(((uint32_t*)mr->addr)[tot_len/2 - len/2 + i]));
        printf("\n");
    }
    if(msg_size > len_in_byte) {
        printf("last %d bytes\n", len_in_byte);
        for(int i = 0; i < len; i++) printf("%08x ", ntohl(((uint32_t*)mr->addr)[tot_len - len + i]));
        printf("\n");
    }

    return 0;
}