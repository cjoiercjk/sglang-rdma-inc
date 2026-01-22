#include "std_header.p4"
#include "custom_header.p4"
#include "dcqcn.p4"
// #include "byteswap.p4"

parser EgressParser(packet_in packet,
               out headers hdr,
               out egress_metadata md,
               out egress_intrinsic_metadata_t eg_intr_md) {

    // only allreduce flows come in
    state start {
        packet.extract(eg_intr_md);
        packet.extract(hdr.bridge);
        transition select(hdr.bridge.pass_type) {
            ALLREDUCE_FIRST_PASS : parse_recirculate;
            ALLREDUCE_SECOND_PASS : parse_recirculate;
            OTHER_PASS : parse_eth;
        }
    }

    state parse_eth {
        packet.extract(hdr.eth);
        transition select(hdr.eth.protocol) {
            IP_PROTOCOL : parse_ip;
            default     : accept;
        }
    }

    state parse_ip {// for dcqcn
        packet.extract(hdr.ip);
        transition accept;
    }

    state parse_recirculate {
        packet.extract(hdr.recir);
        transition select(hdr.bridge.pass_type) {
            ALLREDUCE_FIRST_PASS : parse_inner_packet;
            ALLREDUCE_SECOND_PASS : parse_conn;
        }
    }

    state parse_conn {
        packet.extract(hdr.conn);
        transition parse_inner_packet;
    }

    state parse_inner_packet{
        packet.extract(hdr.eth);
        packet.extract(hdr.ip);
        packet.extract(hdr.udp);
        packet.extract(hdr.bth);
        transition select(hdr.bth.opcode) {
            RDMA_OP_SEND_FIRST : accept;
            RDMA_OP_SEND_MIDDLE : accept;
            RDMA_OP_SEND_LAST : accept;
            RDMA_OP_SEND_LAST_WITH_IMM : accept;
            RDMA_OP_SEND_ONLY : accept;
            RDMA_OP_SEND_ONLY_WITH_IMM: accept;
            RDMA_OP_WRITE_FIRST : parse_reth;
            RDMA_OP_WRITE_MIDDLE : accept;
            RDMA_OP_WRITE_LAST : accept; 
            RDMA_OP_WRITE_LAST_WITH_IMM: accept;
            RDMA_OP_WRITE_ONLY : parse_reth;
            RDMA_OP_WRITE_ONLY_WITH_IMM: parse_reth;
        }
    }

    state parse_reth {
        packet.extract(hdr.reth);
        transition accept;
    }
}

control Egress(
        inout headers hdr,
        inout egress_metadata md,
        in egress_intrinsic_metadata_t eg_intr_md,
        in egress_intrinsic_metadata_from_parser_t eg_ps_md,
        inout egress_intrinsic_metadata_for_deparser_t eg_dps_md,
        inout egress_intrinsic_metadata_for_output_port_t eg_op_md) {

    action restore_fields(ip_addr_t sip, ip_addr_t dip, bit<24> dqpn) {
        eg_dps_md.drop_ctl = 0;
        hdr.ip.sip = sip;
        hdr.ip.dip = dip;
        hdr.bth.dqpn = dqpn;
        // If we set rkey here, it will cause side effects since rkey's PHV is allocated to other fields
    }

    action restore_fields_with_reth(ip_addr_t sip, ip_addr_t dip, bit<24> dqpn, bit<32> rkey) {
        eg_dps_md.drop_ctl = 0;
        hdr.ip.sip = sip;
        hdr.ip.dip = dip;
        hdr.bth.dqpn = dqpn;
        hdr.reth.rkey = rkey;
    }

    table restore_table{
        key = {
            hdr.conn.group_id : exact;
            hdr.conn.src_rank : exact;
            hdr.reth.isValid() : exact;
        }
        actions = {
            restore_fields;
            restore_fields_with_reth;
            NoAction;
        }
        size = MAX_QP_NUM * 2;
        default_action = NoAction();
    }

    action retore_smac(mac_addr_t smac) {
        hdr.eth.smac = smac;
    }

    table restore_smac_table{
        key = {
            hdr.ip.sip : exact;
        }
        actions = {
            retore_smac;
            NoAction;
        }
        size = MAX_SERVER_NUM;
        default_action = NoAction();
    }

    action retore_dmac(mac_addr_t dmac) {
        hdr.eth.dmac = dmac;
    }

    table restore_dmac_table{
        key = {
            hdr.ip.dip : exact;
        }
        actions = {
            retore_dmac;
            NoAction;
        }
        size = MAX_SERVER_NUM;
        default_action = NoAction();
    }

    action set_ack() {
        hdr.bth.ackreq_rsv_seqnum = hdr.bth.ackreq_rsv_seqnum | 32w0x80000000;
    }

    action clear_ack() {
        hdr.bth.ackreq_rsv_seqnum = hdr.bth.ackreq_rsv_seqnum & 32w0x7fffffff;
    }

//     table ack_table {
//         key = {
//             hdr.conn.ackmap : ternary;
//             hdr.conn.src_rank : ternary;
//             // eg_intr_md.egress_rid : ternary;
//         }
//         actions = {
//             set_ack;
//             clear_ack;
//         }
//         size = 33;
//         const entries = {
// #define POW2(k) (32w1<<(k))
// #define ENTRY(k) (POW2(k) &&& POW2(k), k)
//             ENTRY(31) : set_ack();
//             ENTRY(30) : set_ack();
//             ENTRY(29) : set_ack();
//             ENTRY(28) : set_ack();
//             ENTRY(27) : set_ack();
//             ENTRY(26) : set_ack();
//             ENTRY(25) : set_ack();
//             ENTRY(24) : set_ack();
//             ENTRY(23) : set_ack();
//             ENTRY(22) : set_ack();
//             ENTRY(21) : set_ack();
//             ENTRY(20) : set_ack();
//             ENTRY(19) : set_ack();
//             ENTRY(18) : set_ack();
//             ENTRY(17) : set_ack();
//             ENTRY(16) : set_ack();
//             ENTRY(15) : set_ack();
//             ENTRY(14) : set_ack();
//             ENTRY(13) : set_ack();
//             ENTRY(12) : set_ack();
//             ENTRY(11) : set_ack();
//             ENTRY(10) : set_ack();
//             ENTRY(9) : set_ack();
//             ENTRY(8) : set_ack();
//             ENTRY(7) : set_ack();
//             ENTRY(6) : set_ack();
//             ENTRY(5) : set_ack();
//             ENTRY(4) : set_ack();
//             ENTRY(3) : set_ack();
//             ENTRY(2) : set_ack();
//             ENTRY(1) : set_ack();
//             ENTRY(0) : set_ack();
//             (_, _) : clear_ack();
// #undef ENTRY
// #undef POW2
//         }
//     }

    Register<bit<32>, bit<16>>(8) reg_retrans_cnt;

    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_retrans_cnt) reg_retrans_cnt_update = {
        void apply(inout bit<32> cnt, out bit<32> ret) {
            cnt = cnt + 1;
            ret = cnt;
        }
    };

    action retrans_cnt_update() {
        reg_retrans_cnt_update.execute(hdr.conn.src_rank);
    }

    apply { 
#ifdef TEST
        if(eg_intr_md.egress_rid == 123) {
            hdr.recir.setInvalid();
            hdr.conn.setInvalid();
            hdr.bridge.setInvalid();
            return;
        }
#endif

        if(hdr.recir.enable_multicast == 0) {}
        else {
            hdr.conn.src_rank = eg_intr_md.egress_rid;// note: src_rank is get from ingress in every pass
        }
        // dest_rank = (src_rank + 1) % group_size
        if(hdr.bridge.pass_type == ALLREDUCE_SECOND_PASS) {
// #define DEBUG_CNT_RETRANS
#ifdef DEBUG_CNT_RETRANS
            if(hdr.recir.enable_multicast == 0) {
                retrans_cnt_update();
            }
#endif
            restore_table.apply();
            restore_smac_table.apply();
            restore_dmac_table.apply();
            // ack_table.apply();
            hdr.recir.setInvalid();
            hdr.conn.setInvalid();

            // ByteSwapAll.apply(hdr);
        }
        dcqcn.apply(hdr, eg_intr_md);

        hdr.bridge.setInvalid();
        // hdr.bth.ackreq_rsv_seqnum = hdr.bth.ackreq_rsv_seqnum | 32w0x80000000;
    }
}

control EgressChecksum(inout headers hdr) {
    Checksum() csum;
    apply{
        hdr.ip.checksum = csum.update({
            hdr.ip.ver_hl,
            hdr.ip.dscp_ecn,
            hdr.ip.length,
            hdr.ip.id,
            hdr.ip.flag_offset,
            hdr.ip.ttl,
            hdr.ip.protocol,
            hdr.ip.sip,
            hdr.ip.dip
        });
    }
}

control EgressDeparser(packet_out packet,
                  inout headers hdr,
                  in egress_metadata md,
                  in egress_intrinsic_metadata_for_deparser_t eg_dps_md) {
            
    apply { 
        EgressChecksum.apply(hdr);
        packet.emit(hdr);
    }
}