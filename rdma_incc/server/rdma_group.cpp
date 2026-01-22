#define ENABLE_IB_UTILS
#include "net_utils.hpp"
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <algorithm>
#include <memory>
#include <string>
#include <infiniband/verbs.h>
#include <grpcpp/grpcpp.h>
#include <cuda_runtime.h>
#include "allreduce.grpc.pb.h"
#include "rdma.h" // Only for init_qp() and data types. We will remove this dependency later.
#include "rdma_group.h"

static inline void check_cuda(cudaError_t err, const char* msg) {
    if (err != cudaSuccess) {
        throw std::runtime_error(std::string(msg) + ": " + cudaGetErrorString(err));
    }
}

struct RDMAGroup::Impl {
    // Core Allreduce logic with sliding window flow control
    Impl(const Config& config);
    ~Impl();

    void allreduce(void* buffer, size_t size);

    Config cfg_;
    
    // RDMA device attributes
    uint8_t port_id_;
    uint8_t gid_index_;
    const ibv_mtu mtu_enum_ = IBV_MTU_256; // INC only support IBV_MTU_256
    const size_t mtu_size_ = (128<<mtu_enum_); // IBV_MTU_256 == 1, IBV_MTU_512 == 2, ...
    
    // Per-QP Window Constraints
    size_t per_qp_server_win_size_;

    struct ibv_context* ctx_ = nullptr;
    struct ibv_pd* pd_ = nullptr;
    struct ibv_mr* mr_ = nullptr;
    void *buffer_;

    std::vector<struct ibv_qp*> qps_;
    std::vector<RankAddr> all_ranks_addr_;
    
    std::unique_ptr<inc::INC::Stub> stub_;
    std::vector<uint32_t> group_ids_;

    // Internal initialization
    void init();
    void setup_device();
    ibv_qp *init_qp(ibv_context *ctx, ibv_pd *pd);
    void create_qps();
    void alloc_buffer();
    void exchange_info_socket();
    void modify_qp_to_rts(struct ibv_qp* qp, ibv_gid dgid, uint32_t dqpn);
    
    // Wrapped WR posting and polling from rdma.cpp
    void post_message(struct ibv_qp* qp, struct ibv_sge* s_sge, struct ibv_sge* r_sge, 
                      MemoryAddress* remote_addr, bool use_send, bool notify, 
                      TxRxType txrx_type, size_t& tx_depth, size_t& rx_depth);
    
    // void push_qp(struct ibv_qp* qp, struct ibv_sge* s_sge, struct ibv_sge* r_sge, 
    //              MemoryAddress* remote_addr, size_t push_cnt, bool use_send, 
    //              bool notify_last, TxRxType txrx_type, size_t& tx_depth, size_t& rx_depth);
    void post_message_to_qps(size_t offset, size_t size, int qp_num, std::vector<size_t> &tx_depth, std::vector<size_t> &rx_depth);
    void poll_cqs(int qp_num, std::vector<size_t> &tx_depth, std::vector<size_t> &rx_depth);

    void poll_cq(struct ibv_cq* cq, size_t& depth);
    void return_groups();
};

RDMAGroup::RDMAGroup(const Config& config) {
    pImpl = std::make_unique<RDMAGroup::Impl>(config); 
}

RDMAGroup::~RDMAGroup() = default;

void RDMAGroup::allreduce(void* buffer, size_t size) {
    pImpl->allreduce(buffer, size);
}

RDMAGroup::Impl::Impl(const Config& config) : cfg_(config) {
    // server_win_size is fixed as half of switch_win_size
    per_qp_server_win_size_ = cfg_.per_qp_switch_win_size / 2;

    // Validation checks from main.cpp
    if (cfg_.world_size < 1 || cfg_.world_size > 32 || cfg_.rank >= cfg_.world_size) {
        throw std::runtime_error("Invalid world_size or rank");
    }

    // Decide QP number (referencing main.cpp: 2 for huge messages)
    int qp_num = 2;
    qps_.resize(qp_num);
    all_ranks_addr_.resize(cfg_.world_size);

    if ((cfg_.per_qp_switch_win_size & (cfg_.per_qp_switch_win_size - 1)) != 0) {
        throw std::runtime_error("Invalid per_qp_switch_win_size: must be power of 2");
    }

    // Alignment checks for MTU
    if (cfg_.per_qp_switch_win_size % mtu_size_ != 0 || cfg_.per_qp_block_size % mtu_size_ != 0) {
        throw std::runtime_error("Invalid window or block size for MTU alignment");
    }

    // Flow control capacity check
    if (cfg_.per_qp_block_size * 2 > per_qp_server_win_size_) {
         throw std::runtime_error("Invalid message size: 2*block_size exceeds server_win_size");
    }

    init();
}

RDMAGroup::Impl::~Impl() {
    return_groups();
    for (auto qp : qps_) if (qp) ibv_destroy_qp(qp);
    if (mr_) ibv_dereg_mr(mr_);
    if (pd_) ibv_dealloc_pd(pd_);
    if (ctx_) ibv_close_device(ctx_);
}

void RDMAGroup::Impl::init() {
    setup_device();
    alloc_buffer();
    create_qps();
    exchange_info_socket();
    
    // Transitions based on the gathered global address table
    for (size_t i = 0; i < qps_.size(); i++) {
        modify_qp_to_rts(qps_[i], ipv4_to_gid(all_ranks_addr_[cfg_.rank].ip), all_ranks_addr_[cfg_.rank].qpn[i]);
    }
}

void RDMAGroup::Impl::setup_device() {
    uint32_t local_ip = ntohl(inet_addr(cfg_.bind_ip.c_str()));
    struct ibv_device *dev = nullptr;

    // Automatic deduction referencing main.cpp logic
    if (!query_ib_device_by_ip(local_ip, &dev, &port_id_, &gid_index_)) {
        throw std::runtime_error("Failed to find RDMA device for IP: " + cfg_.bind_ip);
    }

    ctx_ = ibv_open_device(dev);
    if (!ctx_) throw std::runtime_error("ibv_open_device failed");

    pd_ = ibv_alloc_pd(ctx_);
    if (!pd_) throw std::runtime_error("ibv_alloc_pd failed");
}

void RDMAGroup::Impl::alloc_buffer() {
    buffer_ = aligned_alloc(4<<10, cfg_.buffer_size);
    mr_ = ibv_reg_mr_iova(pd_, buffer_, cfg_.buffer_size, 0, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE/* | IBV_ACCESS_REMOTE_READ*/);
}

ibv_qp *RDMAGroup::Impl::init_qp(ibv_context *ctx, ibv_pd *pd)
{
    int ret;
    // CQ
    ibv_cq *scq = ibv_create_cq(ctx, cfg_.queue_depth, NULL, /*comp_channel*/NULL, 0);// use 0 as comp_vector
    ibv_cq *rcq = ibv_create_cq(ctx, cfg_.queue_depth, NULL, /*comp_channel*/NULL, 0);// use 0 as comp_vector
    MYCHECK(scq == NULL, "Error on ibv_create_cq");
    MYCHECK(rcq == NULL, "Error on ibv_create_cq");
    struct ibv_qp_init_attr init_attr = {};
    init_attr.send_cq = scq;
    init_attr.recv_cq = rcq;
    init_attr.cap.max_send_wr  = cfg_.queue_depth;
    init_attr.cap.max_recv_wr  = cfg_.queue_depth;
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
    attr.port_num        = port_id_;
    attr.qp_access_flags = 0;

    ret = ibv_modify_qp(qp, &attr, IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS);
    MYCHECK(ret != 0, "Error on RESET->INIT");
    return qp;
}

void RDMAGroup::Impl::create_qps() {
    for (size_t i = 0; i < qps_.size(); ++i) {
        qps_[i] = init_qp(ctx_, pd_);
        if (!qps_[i]) throw std::runtime_error("Failed to create QP");
    }
}

void RDMAGroup::Impl::exchange_info_socket() {
    RankAddr local_addr = {};
    local_addr.ip = ntohl(inet_addr(cfg_.bind_ip.c_str()));
    local_addr.rkey = mr_->rkey;
    for(size_t i = 0; i < qps_.size(); ++i) local_addr.qpn[i] = qps_[i]->qp_num;

    if (cfg_.rank == 0) {
        // Multi-rank hub logic referencing inc.hpp
        int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in serv_addr = { .sin_family = AF_INET, .sin_port = htons(cfg_.rank0_port) };
        serv_addr.sin_addr.s_addr = INADDR_ANY;
        
        int opt = 1;
        setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        bind(listen_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
        listen(listen_fd, cfg_.world_size);

        std::vector<RankAddr> addr_list(cfg_.world_size);
        std::vector<RankAddr> remote_addr_list(cfg_.world_size);
        addr_list[cfg_.rank] = local_addr;
        std::vector<int> client_fds(cfg_.world_size, -1);

        for (int i = 1; i < cfg_.world_size; i++) {
            int conn_fd = accept(listen_fd, NULL, NULL);
            int remote_rank;
            read(conn_fd, &remote_rank, sizeof(int));
            read(conn_fd, &addr_list[remote_rank], sizeof(RankAddr));
            client_fds[remote_rank] = conn_fd;
        }

        // RPC to Controller using CreateGroup
        std::string ctrl_url = cfg_.controller_ip + ":" + std::to_string(cfg_.controller_port);
        auto channel = grpc::CreateChannel(ctrl_url, grpc::InsecureChannelCredentials());
        stub_ = inc::INC::NewStub(channel);

        for (size_t q = 0; q < qps_.size(); q++) {
            grpc::ClientContext rpc_ctx;
            inc::CreateGroupRequest req;
            inc::CreateGroupReply rep;

            for (int r = 0; r < cfg_.world_size; r++) {
                auto* m = req.add_member();
                m->set_ip(addr_list[r].ip);
                m->set_qpn(addr_list[r].qpn[q]);
                m->set_rkey(addr_list[r].rkey);
            }
            req.set_memorysize(cfg_.per_qp_switch_win_size);
            req.set_rootrank(0);

            if (stub_->CreateGroup(&rpc_ctx, req, &rep).ok()) {
                group_ids_.push_back(rep.groupid());
                // Update with switch-mapped addresses
                for (int r = 0; r < cfg_.world_size; r++) {
                    remote_addr_list[r].ip = rep.member(r).ip();
                    remote_addr_list[r].qpn[q] = rep.member(r).qpn();
                    remote_addr_list[r].rkey = rep.member(r).rkey();
                }
            } else {
                throw std::runtime_error("RPC CreateGroup failed");
            }
        }

        // Broadcast to all other ranks
        for (int i = 1; i < cfg_.world_size; i++) {
            write(client_fds[i], remote_addr_list.data(), sizeof(RankAddr) * cfg_.world_size);
            close(client_fds[i]);
        }
        all_ranks_addr_ = remote_addr_list;
        close(listen_fd);
    } else {
        // Client connecting to Rank 0 hub
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in serv_addr = { .sin_family = AF_INET, .sin_port = htons(cfg_.rank0_port) };
        serv_addr.sin_addr.s_addr = inet_addr(cfg_.rank0_ip.c_str());

        while (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) usleep(10000);
        
        write(sock, &cfg_.rank, sizeof(int));
        write(sock, &local_addr, sizeof(RankAddr));
        read(sock, all_ranks_addr_.data(), sizeof(RankAddr) * cfg_.world_size);
        close(sock);
    }
}

void RDMAGroup::Impl::modify_qp_to_rts(struct ibv_qp* qp, ibv_gid dgid, uint32_t dqpn) {
    struct ibv_qp_attr attr = {};
    
    // INIT
    attr.qp_state = IBV_QPS_INIT;
    attr.port_num = port_id_;
    attr.pkey_index = 0;
    attr.qp_access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE;
    ibv_modify_qp(qp, &attr, IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS);

    // RTR (Ready To Receive)
    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_RTR;
    attr.path_mtu = mtu_enum_;
    attr.dest_qp_num = dqpn;
    attr.rq_psn = PSN;
    attr.max_dest_rd_atomic = 1;
    attr.min_rnr_timer = 12;
    attr.ah_attr.port_num = port_id_;
    
    // Standard GID based is_global check from rdma.cpp
    if (dgid.global.interface_id) {
        attr.ah_attr.is_global = 1;
        attr.ah_attr.grh.hop_limit = 1;
        attr.ah_attr.grh.dgid = dgid;
        attr.ah_attr.grh.sgid_index = gid_index_;
    } else {
        attr.ah_attr.is_global = 0;
        attr.ah_attr.dlid = 0; 
    }

    ibv_modify_qp(qp, &attr, IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN | IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER);

    // RTS (Ready To Send)
    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_RTS;
    attr.timeout = 8;
    attr.retry_cnt = 7;
    attr.rnr_retry = 7;
    attr.sq_psn = PSN;
    attr.max_rd_atomic = 1;
    ibv_modify_qp(qp, &attr, IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC);
}

void RDMAGroup::Impl::post_message_to_qps(size_t offset, size_t size, int qp_num, std::vector<size_t> &tx_depth, std::vector<size_t> &rx_depth) 
{
    struct ibv_sge sge = { .lkey = mr_->lkey };
    struct MemoryAddress remote_addr = { .rkey = mr_->rkey }; 
    size_t num_pkt = size / mtu_size_;
    if (size % mtu_size_ != 0) {
        throw std::runtime_error("size % mtu_size != 0");
    }
    size_t qp_offset = offset;
    for (int i = 0; i < qp_num; i++) {
        size_t sub_size = (num_pkt / qp_num + (i < num_pkt % qp_num)) * mtu_size_;
        sge.addr = qp_offset;
        sge.length = sub_size;
        remote_addr.memory_address = qp_offset;
        post_message(qps_[i], &sge, NULL, &remote_addr, false, false, TXRX, tx_depth[i], rx_depth[i]);// for allreduce
        qp_offset += sub_size;
    }
}

void RDMAGroup::Impl::poll_cqs(int qp_num, std::vector<size_t> &tx_depth, std::vector<size_t> &rx_depth)
{
    for (int i = 0; i < qp_num; i++) {
        poll_cq(qps_[i]->recv_cq, rx_depth[i]);
        poll_cq(qps_[i]->send_cq, tx_depth[i]);
    }
}

void RDMAGroup::Impl::allreduce(void* buffer, size_t size) {
    check_cuda(cudaMemcpy(buffer_, buffer, size, cudaMemcpyDeviceToHost),
               "cudaMemcpy DeviceToHost failed");

    size_t padded_size = (size + mtu_size_ - 1) / mtu_size_ * mtu_size_; 
    int qp_num = padded_size >= cfg_.threshold ? 2 : 1;

    size_t merged_block_size = qp_num * cfg_.per_qp_block_size;
    size_t total_blocks = (padded_size + merged_block_size - 1) / merged_block_size;
    size_t scnt = 0; 

    std::vector<size_t> tx_depth(qp_num, 0);
    std::vector<size_t> rx_depth(qp_num, 0);

    // Flow control: ensure total inflight bytes <= per_qp_server_win_size
    size_t max_qsize = std::min((size_t)cfg_.queue_depth, per_qp_server_win_size_ / cfg_.per_qp_block_size);

    while(1) {
        size_t current_depth = 0;
        for(int i = 0; i < qp_num; i++) {
            current_depth = std::max(current_depth, std::max(tx_depth[i], rx_depth[i]));
        }
        if (scnt == total_blocks && current_depth == 0) break;
        size_t push_cnt = std::min(max_qsize - current_depth, total_blocks - scnt);

        for(int push_iter = 0; push_iter < push_cnt; push_iter ++) {
            size_t sub_msg_offset = scnt * merged_block_size;
            size_t sub_msg_size = std::min(merged_block_size, padded_size - sub_msg_offset);
            post_message_to_qps(sub_msg_offset, sub_msg_size, qp_num, tx_depth, rx_depth);
            scnt ++;
        }
        poll_cqs(qp_num, tx_depth, rx_depth);
    }

    check_cuda(cudaMemcpy(buffer, buffer_, size, cudaMemcpyHostToDevice),
               "cudaMemcpy HostToDevice failed");
}

void RDMAGroup::Impl::post_message(struct ibv_qp* qp, struct ibv_sge* s_sge, struct ibv_sge* r_sge, 
                           MemoryAddress* remote_addr, bool use_send, bool notify, 
                           TxRxType txrx_type, size_t& tx_depth, size_t& rx_depth) {
    if ((txrx_type == TXRX || txrx_type == RX) && (use_send || notify)) {
        struct ibv_recv_wr wr = { .wr_id = 1, .sg_list = r_sge, .num_sge = 1 };
        ibv_post_recv(qp, &wr, nullptr);
        rx_depth++;
    }
    if (txrx_type == RX) return;

    struct ibv_send_wr wr = { .wr_id = 1, .sg_list = s_sge, .num_sge = 1, .send_flags = IBV_SEND_SIGNALED };
    if (use_send) {
        wr.opcode = IBV_WR_SEND;
    } else {
        wr.opcode = notify ? IBV_WR_RDMA_WRITE_WITH_IMM : IBV_WR_RDMA_WRITE;
        if (notify) wr.imm_data = htonl(0);
        memcpy(&wr.wr.rdma, remote_addr, sizeof(MemoryAddress));
    }
    ibv_post_send(qp, &wr, nullptr);
    tx_depth++;
}

// void RDMAGroup::push_qp(struct ibv_qp* qp, struct ibv_sge* s_sge, struct ibv_sge* r_sge, 
//                       MemoryAddress* remote_addr, size_t push_cnt, bool use_send, 
//                       bool notify_last, TxRxType txrx_type, size_t& tx_depth, size_t& rx_depth) {
//     for (size_t i = 0; i < push_cnt; i++) {
//         post_message(qp, s_sge, r_sge, remote_addr, use_send, notify_last && (i == push_cnt - 1), txrx_type, tx_depth, rx_depth);
//     }
//     poll_cq(qp->recv_cq, rx_depth);
//     poll_cq(qp->send_cq, tx_depth);
// }

void RDMAGroup::Impl::poll_cq(struct ibv_cq* cq, size_t& depth) {
    struct ibv_wc wc;
    int ret = ibv_poll_cq(cq, 1, &wc);
    if (ret > 0) depth -= ret;
}

void RDMAGroup::Impl::return_groups() {
    if (!stub_) return;
    for (auto id : group_ids_) {
        grpc::ClientContext rpc_ctx;
        inc::DestroyGroupRequest req;
        inc::DestroyGroupReply rep;
        req.set_groupid(id);
        stub_->DestroyGroup(&rpc_ctx, req, &rep);
    }
    group_ids_.clear();
}
