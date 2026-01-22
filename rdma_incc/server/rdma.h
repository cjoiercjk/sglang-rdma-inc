#ifndef RDMA_H
#define RDMA_H

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
#include "error.h"
// some assumption of the network
extern uint32_t PORT_ID; 
#define LID 0
extern uint32_t GID_INDEX;


// some definition of the network
/*
 * Our p4 implementation requires all PSN start from a same value.
 * The stage resources of switch is limited, there is no extra stage 
 * to calculate the offset through PSN difference, so we directly 
 * use PSN (with modulo) as offset.
 */ 
#define PSN 0 // let all PSN start from 0
// We may support MTU of 512 with resubmission
extern ibv_mtu MTU;
#define MTU_SIZE (128*(1<<MTU))

#define ALLOC_MEM_SIZE (1<<30)

#define MAX_Q_SIZE 128
#define Q_SIZE 128
// Small Q_SIZE will cause few WRs in recv_queue,
// leading to performance degradation of SEND due to message level flow control,
// especailly for small messages

#define MAX_QP_NUM 8 // I think 2 is enough
#define TINY_MESSAGE_LIM (MTU_SIZE*4)
#define HUGE_MESSAGE_LIM (MTU_SIZE*16)

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

struct RankAddr {
    // assume lid==0 && psn==0
    uint32_t ip;
    uint32_t rkey;
    int qpn[MAX_QP_NUM];
};

struct MemoryAddress {
    uint64_t memory_address;    
    uint32_t rkey;
};

enum TxRxType: int {
    TX = 0,
    RX = 1,
    TXRX = 2,
};

void *malloc_huge(size_t size);

ibv_qp *init_qp(ibv_context *ctx, ibv_pd *pd);

void post_message(ibv_qp *qp, ibv_sge *s_sge, ibv_sge *r_sge, MemoryAddress *remote_memory_addr, 
    bool use_send, bool notify, TxRxType txrx_type);

int poll_cq(ibv_cq *cq);

void push_qp(ibv_qp *qp,  ibv_sge *s_sge, ibv_sge *r_sge, MemoryAddress *remote_memory_addr, 
    size_t push_cnt, 
    bool use_send, bool notify_last, TxRxType txrx_type, size_t &tx_queue_depth, size_t &rx_queue_depth);

void move_qp_to_rts(ibv_qp *qp, ibv_gid dgid, uint32_t dqpn);

#endif