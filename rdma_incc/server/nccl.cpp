#ifndef ALLTOALL_HPP
#define ALLTOALL_HPP

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <nccl.h>
#include <cuda_runtime.h>
#include <infiniband/verbs.h>
#include "utils.hpp"

#define ENABLE_IB_UTILS
#include "net_utils.hpp"

#define CHECK_CUDA(call)                                                   \
  do {                                                                     \
    cudaError_t err = call;                                                \
    if (err != cudaSuccess) {                                              \
      fprintf(stderr, "CUDA error %s:%d '%s'\n", __FILE__, __LINE__,       \
              cudaGetErrorString(err));                                    \
      exit(1);                                                             \
    }                                                                      \
  } while (0)
#define CHECK_NCCL(call)                                                   \
  do {                                                                     \
    ncclResult_t res = call;                                               \
    if (res != ncclSuccess) {                                              \
      fprintf(stderr, "NCCL error %s:%d '%s'\n", __FILE__, __LINE__,       \
              ncclGetErrorString(res));                                    \
      exit(1);                                                             \
    }                                                                      \
  } while (0)

using std::string;

void socket_init(int group_size, int rank, uint32_t ip, uint16_t port, unsigned int wait);

void socket_broadcast(void *data, size_t size);

void fallback_nccl(int group_size, int rank, uint32_t bind_ip, uint32_t rank0_ip, 
    uint16_t rank0_port, size_t msg_size, size_t tot_round, unsigned int wait, string collective)
{
    // set NCCL_IB_HCA=ib_dev_name
    ibv_device *dev = NULL;
    uint8_t port_id, gid_index;
    query_ib_device_by_ip(bind_ip, &dev, &port_id, &gid_index);

    string ib_dev_name = ibv_get_device_name(dev);
    printf("set NCCL_IB_HCA=%s\n", ib_dev_name.c_str());
    setenv("NCCL_IB_HCA", ib_dev_name.c_str(), 1); // overwrite=1

    socket_init(group_size, rank, rank0_ip, rank0_port, wait);

    ncclUniqueId id;    
    if(rank == 0) CHECK_NCCL(ncclGetUniqueId(&id));
    socket_broadcast(&id, sizeof(id));// 0 -> others

    ncclComm_t comm;
    CHECK_NCCL(ncclCommInitRank(&comm, group_size, id, rank));

    assert(msg_size % group_size == 0);
    
    // void *sendbuff = malloc(msg_size);
    // void *recvbuff = malloc(msg_size);
    // memset(sendbuff, 0, msg_size);
    // memset(recvbuff, 0, msg_size);
    
    void *sendbuff;
    void *recvbuff;
    CHECK_CUDA(cudaMalloc(&sendbuff, msg_size));
    CHECK_CUDA(cudaMalloc(&recvbuff, msg_size));
    CHECK_CUDA(cudaMemset(sendbuff, 0, msg_size));
    CHECK_CUDA(cudaMemset(recvbuff, 0, msg_size));

    cudaStream_t stream;
    CHECK_CUDA(cudaStreamCreate(&stream));

    uint64_t ts = gettimeus();
    
    for(int round = 0; round < tot_round; round++) {
        CHECK_NCCL(ncclGroupStart());
        if(collective == "allreduce") {
            CHECK_NCCL(ncclAllReduce(sendbuff, recvbuff, msg_size/2, ncclFloat16, ncclSum, comm, stream));
        }
        else if(collective == "reduce") {
            CHECK_NCCL(ncclReduce(sendbuff, recvbuff, msg_size/2, ncclFloat16, ncclSum, 0, comm, stream));
        }
        else if(collective == "broadcast") {
            CHECK_NCCL(ncclBroadcast(sendbuff, recvbuff, msg_size/2, ncclFloat16, 0, comm, stream));
        }
        else if(collective == "reducescatter") {
            CHECK_NCCL(ncclReduceScatter(sendbuff, recvbuff, msg_size/group_size/2, ncclFloat16, ncclSum, comm, stream));
        }
        else if(collective == "allgather") {
            CHECK_NCCL(ncclAllGather(sendbuff, recvbuff, msg_size/group_size/2, ncclFloat16, comm, stream));
        }
        else if(collective == "alltoall") {
            size_t sub_msg_size = msg_size / group_size;
            // CHECK_NCCL(ncclAllToAll(sendbuff, recvbuff, sub_msg_size, ncclChar, comm, stream));
            for(int r = 0; r < group_size; r++) {
                ncclSend((char*)sendbuff + r*sub_msg_size, sub_msg_size, ncclChar, r, comm, stream);
                ncclRecv((char*)recvbuff + r*sub_msg_size, sub_msg_size, ncclChar, r, comm, stream);
            }
        }
        else if(collective == "barrier") {
            CHECK_NCCL(ncclAllReduce(sendbuff, recvbuff, 1, ncclFloat16, ncclSum, comm, stream));
        }
        CHECK_NCCL(ncclGroupEnd());
        CHECK_CUDA(cudaStreamSynchronize(stream));
    }
        
    printf("%.2lf Gbps\n", 1.0*msg_size*tot_round*8*1e-3/(gettimeus()-ts));

    // free(sendbuff);
    // free(recvbuff);
    CHECK_CUDA(cudaFree(sendbuff));
    CHECK_CUDA(cudaFree(recvbuff));
    CHECK_CUDA(cudaStreamDestroy(stream));
    CHECK_NCCL(ncclCommDestroy(comm));
}

#endif