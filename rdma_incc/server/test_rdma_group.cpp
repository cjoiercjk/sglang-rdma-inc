#include <string>
#include <cstdlib>
#include <stdexcept>
#include <endian.h>
#include <iostream>
#include <chrono>
#include <cuda_runtime.h>
#include "rdma_group.h"

using std::string;

int main(int argc, char **argv)
{
    size_t size = std::stoull(string(argv[1]));
    size_t world_size = std::stoull(string(argv[2]));
    size_t rank = std::stoull(string(argv[3]));
    string bind_ip = string(argv[4]);
    string rank0_ip = string(argv[5]);
    string controller_ip = string(argv[6]);

    if (size % 4 != 0) {
        throw std::runtime_error("size % 4 != 0");
    }

    auto check_cuda = [](cudaError_t err, const char* msg) {
        if (err != cudaSuccess) {
            throw std::runtime_error(std::string(msg) + ": " + cudaGetErrorString(err));
        }
    };

    void *host_buffer = malloc(size);
    for (size_t i = 0; i < size / 4; i++) {
        ((uint32_t*)host_buffer)[i] = htobe32(i);
    }

    void *buffer = nullptr;
    check_cuda(cudaMalloc(&buffer, size), "cudaMalloc failed");
    check_cuda(cudaMemcpy(buffer, host_buffer, size, cudaMemcpyHostToDevice),
               "cudaMemcpy HostToDevice failed");

    Config config = {
        .rank = rank,
        .world_size = world_size,
        .bind_ip = bind_ip,
        .rank0_ip = rank0_ip,
        .controller_ip = controller_ip,
        // .per_qp_block_size = 256,
        // .per_qp_switch_win_size = 1024,
        // .threshold = 1ull<<60, // INF
    };
    RDMAGroup rdma_group(config);

    auto start = std::chrono::high_resolution_clock::now();
    rdma_group.allreduce(buffer, size);
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    printf("throughput: %.3lf Gbps\n", size * 8 * 1e-9 / elapsed.count());

    check_cuda(cudaMemcpy(host_buffer, buffer, size, cudaMemcpyDeviceToHost),
               "cudaMemcpy DeviceToHost failed");

    bool success = true;
    for(size_t i = 0; i < size / 4; i++) {
        auto result = be32toh(((uint32_t*)host_buffer)[i]);
        auto expect = i * world_size;
        if (result != expect) {
            std::cout << std::string("The result of buffer[") + std::to_string(i) + "] is " + std::to_string(result) + " but should be " + std::to_string(expect) << "\n";
            // printf("Failed\n");
            // return 1;
            success = false;
        }
    }
    printf(success ? "Success\n": "Failed\n");
    check_cuda(cudaFree(buffer), "cudaFree failed");
    free(host_buffer);
    return 0;
}
