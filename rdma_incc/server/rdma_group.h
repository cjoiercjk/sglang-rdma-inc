#ifndef RDMA_GROUP_H
#define RDMA_GROUP_H

#include <cstdint>
#include <vector>
#include <string>
#include <memory>

struct Config {
    int rank;
    int world_size;

    // IP Configurations
    std::string bind_ip;      // Local IP for RDMA NIC binding
    std::string rank0_ip;     // Rank 0 IP for TCP socket negotiation
    std::string controller_ip; // Controller IP for requesting INC resources via RPC

    // Port Configurations
    uint16_t rank0_port = 12345;     // Port for rank coordination
    uint16_t controller_port = 50051; // Default gRPC port for controller

    // Tuning Parameters
    size_t queue_depth = 128;      // Q_SIZE (MAX_Q_SIZE is 128)
    size_t per_qp_block_size = 32768;     // Default 32KB
    size_t per_qp_switch_win_size = 131072; // Default 128KB
    size_t threshold = 4096;       // Msg size threshold for multi-QP usage
    size_t buffer_size = 1024ull*1024*1024;
};

class RDMAGroup {
public:
    // Constructor includes checks from main.cpp
    RDMAGroup(const Config& config);
    ~RDMAGroup();// NOTE: Don't delete this. Don't use "=default" here. 
    
    // Core Allreduce logic with sliding window flow control
    void allreduce(void* buffer, size_t size);
private:
    struct Impl;
    std::unique_ptr<Impl> pImpl;
};

#endif // RDMA_GROUP_H