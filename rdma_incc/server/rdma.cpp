#include "rdma.h"

uint32_t PORT_ID; 
uint32_t GID_INDEX;
ibv_mtu MTU;

void *malloc_huge(size_t size)
{
	void *ret = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS/* | MAP_HUGETLB*/, -1, 0);
	MYCHECK(ret==NULL, "Error on mmap");
	return ret;
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

void post_message(ibv_qp *qp, ibv_sge *s_sge, ibv_sge *r_sge, MemoryAddress *remote_memory_addr, 
    bool use_send, bool notify, TxRxType txrx_type, size_t &tx_queue_depth, size_t &rx_queue_depth)
{
    int ret;
    if((txrx_type == TXRX || txrx_type == RX) && (use_send || notify)) {
        struct ibv_recv_wr wr = {}, *bad_wr;
        wr.wr_id = 1;
        wr.next = NULL;
        wr.sg_list = r_sge;
        wr.num_sge = 1;
        ret = ibv_post_recv(qp, &wr, &bad_wr);
        rx_queue_depth ++;
        if(ret != 0) fprintf(stderr, "%d\n", ret);
        MYCHECK(ret != 0, "Error on ibv_post_recv");
    }
    if(txrx_type == RX) {
        return;
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
        memcpy(&wr.wr.rdma, remote_memory_addr, sizeof(MemoryAddress));
    }
    ret = ibv_post_send(qp, &wr, &bad_wr);
    tx_queue_depth ++;
    MYCHECK(ret != 0, "Error on ibv_post_send");
}

void poll_cq(ibv_cq *cq, size_t &queue_depth)
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
    queue_depth -= ret;
    // if(ret > 0) {
    //     printf("polled %d, queue_depth %ld\n", ret, queue_depth);
    // }
}

void push_qp(ibv_qp *qp,  ibv_sge *s_sge, ibv_sge *r_sge, MemoryAddress *remote_memory_addr, 
    size_t push_cnt, 
    bool use_send, bool notify_last, TxRxType txrx_type, size_t &tx_queue_depth, size_t &rx_queue_depth)
{
    for(size_t i = 0; i < push_cnt; i++) {// push_cnt can be 0
        post_message(qp, s_sge, r_sge, remote_memory_addr, use_send, notify_last && i==push_cnt-1, txrx_type, tx_queue_depth, rx_queue_depth);
    }
    
    // We prefer to clear out CQ rather than send new requests, so we use a loop here
    poll_cq(qp->recv_cq, rx_queue_depth);
    poll_cq(qp->send_cq, tx_queue_depth);
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
	attr.min_rnr_timer		= 12;// wait for 0.64ms to send RNR NACK, 1: 0.01ms, 0: 655ms, 
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
    // IBV_QP_ACCESS_FLAGS is optional ! But it is required for WRITE/READ/ATOMIC
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