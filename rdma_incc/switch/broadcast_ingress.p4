#include "std_header.p4"
#include "custom_header.p4"
#include "broadcast_sendout.p4"

parser IngressParser(packet_in packet,
               out headers hdr,
               out ingress_metadata md,
               out ingress_intrinsic_metadata_t ig_intr_md) {

    state start {
        packet.extract(ig_intr_md);
        transition select(ig_intr_md.resubmit_flag) {
            0 : parse_port_metadata;
        }
    }

    state parse_port_metadata {
        md.port_metadata = port_metadata_unpack<port_metadata_t>(packet);
        transition select(md.port_metadata.is_recirculate_port) {
            1 : parse_recirculate;
            0 : parse_ethernet;
        }
    }
    
    state parse_recirculate {
        packet.extract(hdr.recir);
        transition parse_ethernet;
    }

    state parse_ethernet {
        packet.extract(hdr.eth);
        transition select(hdr.eth.protocol) {
            IP_PROTOCOL : parse_ip;
            default     : parse_other;
        }
    }

    state parse_ip {
        packet.extract(hdr.ip);
        transition select(hdr.ip.protocol) {
            UDP_PROTOCOL : parse_udp;
            default      : parse_other;
        }
    }

    state parse_udp {
        packet.extract(hdr.udp);
        transition select(hdr.udp.dport) {
            RDMA_DPORT : parse_bth;
            default    : parse_other;
        }
    }

    state parse_bth {
        packet.extract(hdr.bth);
        transition select(hdr.bth.opcode) {
            RDMA_OP_SEND_FIRST: inc_accept;
            RDMA_OP_SEND_MIDDLE: inc_accept;
            RDMA_OP_SEND_LAST: inc_accept;
            RDMA_OP_SEND_LAST_WITH_IMM : parse_imm;
            RDMA_OP_SEND_ONLY: inc_accept;
            RDMA_OP_SEND_ONLY_WITH_IMM : parse_imm;
            RDMA_OP_WRITE_FIRST: parse_reth;
            RDMA_OP_WRITE_MIDDLE: inc_accept;
            RDMA_OP_WRITE_LAST: inc_accept; 
            RDMA_OP_WRITE_LAST_WITH_IMM: parse_imm;
            RDMA_OP_WRITE_ONLY: parse_reth;
            RDMA_OP_WRITE_ONLY_WITH_IMM: parse_reth;
            RDMA_OP_ACK: parse_aeth;
            RDMA_OP_CNP: parse_cnp;
            default : parse_other;
        }
    }

    state parse_reth {
        packet.extract(hdr.reth);
        transition select(hdr.bth.opcode) {
            RDMA_OP_WRITE_ONLY_WITH_IMM : parse_imm;
            default : inc_accept;
        }
    }

    state parse_imm {
        packet.extract(hdr.imm);
        transition inc_accept;
    }

    state parse_aeth {
        packet.extract(hdr.aeth);
        transition inc_accept;
    }

    state parse_cnp {
        packet.extract(hdr.cnp);
        transition inc_accept;
    }

    state inc_accept{
        hdr.bridge.setValid();
        hdr.bridge.pass_type = INC_PASS;
        transition accept;
    }

    state parse_other {
        hdr.bridge.setValid();
        hdr.bridge.pass_type = OTHER_PASS;
        transition accept;
    }
}

control Ingress(
        inout headers hdr,
        inout ingress_metadata md,
        in ingress_intrinsic_metadata_t ig_intr_md,
        in ingress_intrinsic_metadata_from_parser_t ig_ps_md,
        inout ingress_intrinsic_metadata_for_deparser_t ig_dps_md,
        inout ingress_intrinsic_metadata_for_tm_t ig_tm_md) {

    action get_forward_metadata(MulticastGroupId_t group_id, ReplicationId_t src_rank, 
        ReplicationId_t root_rank) {
        hdr.conn.setValid();
        hdr.conn.group_id = group_id;
        hdr.conn.src_rank = src_rank;
        hdr.conn.dst_rank = (ReplicationId_t)-1;
    }

    action get_backward_metadata(MulticastGroupId_t group_id, ReplicationId_t src_rank, 
        ReplicationId_t root_rank, bit<32> bitmap, bit<32> bitmap_mask) {
        hdr.conn.setValid();
        hdr.conn.group_id = group_id;
        hdr.conn.src_rank = src_rank;
        hdr.conn.dst_rank = root_rank;
        hdr.ack_bitmap.setValid();
        hdr.ack_bitmap.bitmap = bitmap;
        hdr.ack_bitmap.bitmap_mask = bitmap_mask;
    }

    action set_otherpass() {
    }

    table metadata_table {
        key = {
            // Since there is N->1 traffic, we need an extra field "SIP" to recognize a flow
            hdr.ip.sip: exact; 
            hdr.ip.dip: exact;
            hdr.bth.dqpn: exact;
        }
        actions = {
            get_forward_metadata;
            get_backward_metadata;
            //get_resubmit_INA_metadata;
            set_otherpass;
        }
        size = MAX_QP_NUM * 2;
        default_action = set_otherpass();
    }

    action drop() {
        ig_dps_md.drop_ctl = 0x1;
    }

    action forward(bit<9> port) {
        ig_dps_md.drop_ctl = 0;
        ig_tm_md.ucast_egress_port = port;
    }

    action multicast(MulticastGroupId_t group) {// 16 bit
        ig_dps_md.drop_ctl = 0;
        ig_tm_md.mcast_grp_a = group;
    }

    table unicast_table{
        key = {
            hdr.eth.dmac : exact;
        }
        actions = {
            forward;
            multicast;
            drop;
        }
        size = MAX_SERVER_NUM;
        default_action = drop();
    }
#ifdef TEST
    action filter_hit() {

    }

    table filter_table{
        key = {
            hdr.bth.opcode : ternary;
            hdr.aeth.syndrome_msn[31:29] : ternary;
        }
        actions = {
            filter_hit;
        }
        const entries = {
            (0 &&& 0xf8, _) : filter_hit();
            (8 &&& 0xfc, _) : filter_hit();
            (0x11, 0) : filter_hit();
        }
    }
#endif
    apply {
        // 0
        if(hdr.bridge.pass_type == INC_PASS) {
            switch(metadata_table.apply().action_run) {
                get_forward_metadata : {
                    hdr.bridge.pass_type = BROADCAST_FORWARD_PASS;
                }
                get_backward_metadata : {
                    hdr.bridge.pass_type = BROADCAST_BACKWARD_PASS;
                }
                set_otherpass: {
                    hdr.bridge.pass_type = OTHER_PASS;
                }
            }
        }  
        sendout.apply(hdr, md, ig_intr_md, ig_dps_md, ig_tm_md);
#ifdef TEST
        if(hdr.bth.isValid()) {
            if(filter_table.apply().miss) {
                ig_tm_md.mcast_grp_b = 2;
            }
        }
#endif
    }
}

control IngressDeparser(
        packet_out packet,
        inout headers hdr,
        in ingress_metadata md,
        in ingress_intrinsic_metadata_for_deparser_t ig_dps_md) {

    apply{
        packet.emit(hdr);
    }
}