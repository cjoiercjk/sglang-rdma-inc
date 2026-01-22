#ifndef INC_HPP
#define INC_HPP

#include "rdma.h"
#include "utils.hpp"

#define MAX_CONNECTIONS 1000

/*
 * return a positive group_id
 * return 0 on error
 */
unique_ptr<inc::INC::Stub> stub;
vector<uint32_t>group_ids;

int rank_fd[MAX_CONNECTIONS];
int listen_fd, connect_fd;
int socket_group_size, socket_rank;

uint32_t inc_create_group(uint32_t *ip, int *qpn, uint32_t *rkey, int qp_count, int switch_memory_size, int root_rank = 0) 
{
    // inc::INC
    grpc::ClientContext ctx;
    inc::CreateGroupRequest req;
    inc::CreateGroupReply rep;

    for(int i = 0; i < qp_count; i++) {
        inc::MemberInfo *mem = req.add_member();
        mem->set_ip(ip[i]);
        mem->set_qpn(qpn[i]);
        mem->set_rkey(rkey[i]);
    }
    req.set_memorysize(switch_memory_size);
    req.set_rootrank(root_rank);
    grpc::Status status = stub->CreateGroup(&ctx, req, &rep);
    if(!status.ok()) {
        fprintf(stderr, "CreateGroup Error: %d\n", status.error_code());
    }
    assert(rep.member_size() == qp_count);
    for(int i = 0; i < qp_count; i++) {
        ip[i] = rep.member(i).ip();
        qpn[i] = rep.member(i).qpn();
        rkey[i] = rep.member(i).rkey();
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

void socket_init(int group_size, int rank, uint32_t ip, uint16_t port, unsigned int wait)
{
    socket_group_size = group_size;
    socket_rank = rank;

    printf("target address: %x:%d\n", ip, port);
    int ret;
    
    struct sockaddr_in rank0_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr = { .s_addr = htonl(ip) },
        .sin_zero = {},
    };

    if(socket_rank == 0) {
        listen_fd = socket(AF_INET, SOCK_STREAM, 0);
        MYCHECK(listen_fd < 0, "Error on socket()");

        int opt = 1;
        ret = setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, (void*)&opt, sizeof(opt));
        MYCHECK(ret < 0, "Error on setsockopt()");

        for(int r = 0; r < socket_group_size; r++) rank_fd[r] = -1;

        ret = bind(listen_fd, (struct sockaddr *)&rank0_addr, sizeof(rank0_addr));
        MYCHECK(ret < 0, "Error on bind()");
        ret = listen(listen_fd, MAX_CONNECTIONS);
        MYCHECK(ret < 0, "Error on listen()");
        // gather
        for(int r = 1; r < socket_group_size; r++) {
            int conn_fd = accept(listen_fd, NULL, NULL);
            MYCHECK(conn_fd < 0, "Error on accept()");
            int client_rank;
            ret = read(conn_fd, &client_rank, sizeof(client_rank));
            MYCHECK(!(0<client_rank && client_rank<socket_group_size), "Rank error");
            MYCHECK(rank_fd[client_rank] != -1, "Rank error");
            MYCHECK(ret < sizeof(client_rank), "Partial read()");

            rank_fd[client_rank] = conn_fd;
        }        
    }
    else {
        auto t_start = std::chrono::high_resolution_clock::now();
        do {
            connect_fd = socket(AF_INET, SOCK_STREAM, 0);
            MYCHECK(connect_fd < 0, "Error on socket()");
            ret = connect(connect_fd, (struct sockaddr *)&rank0_addr, sizeof(rank0_addr));
            if(ret == 0) break;
            close(connect_fd);
            usleep(10000);
        } while(std::chrono::high_resolution_clock::now() - t_start < std::chrono::seconds(wait));
        MYCHECK(ret < 0, "Error on connect()");
        ret = write(connect_fd, &socket_rank, sizeof(socket_rank));
        MYCHECK(ret < sizeof(socket_rank), "Partial write()");
    }
}

void socket_barrier()
{
    int ret;
    char c = 0;
    if(socket_rank == 0) {
        for(int r = 1; r < socket_group_size; r++) {
            ret = read(rank_fd[r], &c, 1);
            MYCHECK(ret < 1, "Partial read()");
        }
        for(int r = 1; r < socket_group_size; r++) {
            ret = write(rank_fd[r], &c, 1);
            MYCHECK(ret < 1, "Partial write()");
        }
    }
    else {
        ret = write(connect_fd, &c, 1);
        MYCHECK(ret < 1, "Partial write()");
        ret = read(connect_fd, &c, 1);
        MYCHECK(ret < 1, "Partial read()");
    }
}

void socket_broadcast(void *data, size_t size)
{
    int ret;
    if(socket_rank == 0) {
        for(int r = 1; r < socket_group_size; r++) {
            ret = write(rank_fd[r], data, size);
            MYCHECK(ret < size, "Partial write()");
        }
    }
    else {
        ret = read(connect_fd, data, size);
        MYCHECK(ret < size, "Partial write()");
    }
}

RankAddr exch_addr(RankAddr my_addr, int qp_num, 
    string controller_addr, size_t per_qp_switch_win_size)
{
    int ret;
    RankAddr neighbor_addr;
    if(socket_rank == 0) {
        RankAddr *addr_list = new RankAddr[socket_group_size];
        // gather
        addr_list[0] = my_addr;
        for(int r = 1; r < socket_group_size; r++) {
            ret = read(rank_fd[r], &addr_list[r], sizeof(RankAddr));
            MYCHECK(ret < sizeof(RankAddr), "Partial read()");
        }        
        // register
        vector<vector<uint32_t>>ip(qp_num, vector<uint32_t>(socket_group_size));
        vector<vector<int>>qpn(qp_num, vector<int>(socket_group_size));
        vector<vector<uint32_t>>rkey(qp_num, vector<uint32_t>(socket_group_size));

        for(int q = 0; q < qp_num; q++) {
            for(int r = 0; r < socket_group_size; r++) {
                ip[q][r] = addr_list[r].ip;
                qpn[q][r] = addr_list[r].qpn[q];
                rkey[q][r] = addr_list[r].rkey;
            }
            uint32_t group_id = inc_create_group(ip[q].data(), qpn[q].data(), rkey[q].data(), socket_group_size, (int)per_qp_switch_win_size);
            if(group_id == 0) {// error or no resources
                fprintf(stderr, "No switch resources\n");
                return_groups();
                exit(1);
            }
            group_ids.push_back(group_id);
        }

        // scatter
        for(int r = 0; r < socket_group_size; r++) {
            addr_list[r].ip = ip[0][r];// assert(ip[X][r] == ip[Y][r])
            addr_list[r].rkey = rkey[0][r];
            for(int q = 0; q < qp_num; q++) {
                addr_list[r].qpn[q] = qpn[q][r];
            }
        }

        // scatter
        for(int r = 1; r < socket_group_size; r++) {
            ret = write(rank_fd[r], &addr_list[r], sizeof(addr_list[r]));
            MYCHECK(ret < sizeof(addr_list[r]), "Partial write()");
        }
        neighbor_addr = addr_list[0];
        delete[] addr_list;
    }
    else {
        ret = write(connect_fd, &my_addr, sizeof(my_addr));
        MYCHECK(ret < sizeof(my_addr), "Partial write()");
        ret = read(connect_fd, &neighbor_addr, sizeof(neighbor_addr));
        MYCHECK(ret < sizeof(neighbor_addr), "Partial read()");
    }
    return neighbor_addr;
}

void signalHandler(int signum) {
    std::cout << "Signal received: " << signum << std::endl;
    return_groups();
    exit(signum);
}

#endif