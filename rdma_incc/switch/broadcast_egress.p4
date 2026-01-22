#include "std_header.p4"
#include "custom_header.p4"
#include "dcqcn.p4"

parser EgressParser(packet_in packet,
               out headers hdr,
               out egress_metadata md,
               out egress_intrinsic_metadata_t eg_intr_md) {

    // only allreduce flows come in
    state start {
        packet.extract(eg_intr_md);
        packet.extract(hdr.bridge);
        transition select(hdr.bridge.pass_type) {
            // ALLREDUCE_FIRST_PASS : parse_recirculate;
            // ALLREDUCE_SECOND_PASS : parse_recirculate;
            BROADCAST_FORWARD_PASS : parse_conn;
            BROADCAST_BACKWARD_PASS : parse_ack_bitmap;
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

    // state parse_recirculate {
    //     packet.extract(hdr.recir);
    //     transition select(hdr.bridge.pass_type) {
    //         ALLREDUCE_FIRST_PASS : parse_inner_packet;
    //         ALLREDUCE_SECOND_PASS : parse_conn;
    //     }
    // }

    state parse_ack_bitmap {
        packet.extract(hdr.ack_bitmap);
        transition parse_conn;
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
            RDMA_OP_SEND_ONLY_WITH_IMM : accept;
            RDMA_OP_WRITE_FIRST : parse_reth;
            RDMA_OP_WRITE_MIDDLE : accept;
            RDMA_OP_WRITE_LAST : accept; 
            RDMA_OP_WRITE_LAST_WITH_IMM : accept;
            RDMA_OP_WRITE_ONLY : parse_reth;
            RDMA_OP_WRITE_ONLY_WITH_IMM : parse_reth;
            RDMA_OP_ACK : parse_aeth;
            RDMA_OP_CNP : accept;
        }
    }

    state parse_aeth {
        packet.extract(hdr.aeth);
        transition accept;
    }

    state parse_reth {
        packet.extract(hdr.reth);
        transition accept;
    }
}

control broadcast_forward_egress(
        inout headers hdr,
        inout egress_metadata md,
        in egress_intrinsic_metadata_t eg_intr_md,
        inout egress_intrinsic_metadata_for_deparser_t eg_dps_md) {

    apply {
        hdr.conn.dst_rank = eg_intr_md.egress_rid;
    }
}

struct ack_pair{
    bit<32> seqnum;
    bit<32> bitmap;
}

control broadcast_ack_egress(
        inout headers hdr,
        inout egress_metadata md,
        in egress_intrinsic_metadata_t eg_intr_md,
        inout egress_intrinsic_metadata_for_deparser_t eg_dps_md) {

    Register<bit<32>, bit<16>>(MAX_GROUP_NUM) reg_bitmap;
    
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_bitmap) reg_bitmap_update = {
        void apply(inout bit<32> reg, out bit<32> ret) {
            if (reg == hdr.ack_bitmap.bitmap_mask) {
                reg = hdr.ack_bitmap.bitmap;
            }
            else {
                reg = reg | hdr.ack_bitmap.bitmap;
            }
            ret = reg;
        }
    };

    action bitmap_update() {
        md.new_bitmap = reg_bitmap_update.execute(hdr.conn.group_id);
    }

    Register<bit<32>, bit<16>>(MAX_GROUP_NUM) reg_psn;

    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_psn) reg_psn_update = {
        void apply(inout bit<32> reg, out bit<32> ret) {
            if (md.psn - reg < 0) {// signed 
                reg = md.psn;
            }
            ret = reg;
        }
    };

    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_psn) reg_psn_init = {
        void apply(inout bit<32> reg, out bit<32> ret) {
            reg = md.psn;
            ret = reg;
        }
    };

    action psn_update() {
        md.psn = reg_psn_update.execute(hdr.conn.group_id);
    }

    action psn_init() {
        md.psn = reg_psn_init.execute(hdr.conn.group_id);
    }

    Register<bit<32>, bit<16>>(MAX_GROUP_NUM) reg_msn;

    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_msn) reg_msn_update = {
        void apply(inout bit<32> reg, out bit<32> ret) {
            if (md.msn - reg < 0) {// signed 
                reg = md.msn;
            }
            ret = reg;
        }
    };

    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_msn) reg_msn_init = {
        void apply(inout bit<32> reg, out bit<32> ret) {
            reg = md.msn;
            ret = reg;
        }
    };

    action msn_update() {
        md.msn = reg_msn_update.execute(hdr.conn.group_id);
    }

    action msn_init() {
        md.msn = reg_msn_init.execute(hdr.conn.group_id);
    }

    Register<bit<32>, bit<16>>(MAX_GROUP_NUM) reg_prev_psn;// need to be assigned 0xffffff00 by controller

    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_prev_psn) reg_prev_psn_write = {
        void apply(inout bit<32> reg, out bit<32> ret) {
            reg = md.psn;
            ret = reg;
        }
    };

    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_prev_psn) reg_prev_psn_read = {
        void apply(inout bit<32> reg, out bit<32> ret) {
            ret = reg;
        }
    };

    action save_psn() {
        md.psn = reg_prev_psn_write.execute(hdr.conn.group_id);
    }

    action load_psn() {
        md.psn = reg_prev_psn_read.execute(hdr.conn.group_id);
    }

    Register<bit<32>, bit<16>>(MAX_GROUP_NUM) reg_prev_msn;

    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_prev_msn) reg_prev_msn_write = {
        void apply(inout bit<32> reg, out bit<32> ret) {
            reg = md.msn;
            ret = reg;
        }
    };

    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_prev_msn) reg_prev_msn_read = {
        void apply(inout bit<32> reg, out bit<32> ret) {
            ret = reg;
        }
    };

    action save_msn() {
        md.msn = reg_prev_msn_write.execute(hdr.conn.group_id);
    }

    action load_msn() {
        md.msn = reg_prev_msn_read.execute(hdr.conn.group_id);
    }

    action get_psn() {
        md.psn = hdr.bth.ackreq_rsv_seqnum;
        md.ackreq_rsv = hdr.bth.ackreq_rsv_seqnum & 0xff000000;
    }

    action get_dec_psn() {
        md.psn = hdr.bth.ackreq_rsv_seqnum - 1;
        md.ackreq_rsv = hdr.bth.ackreq_rsv_seqnum & 0xff000000;
    }

    action get_msn() {
        md.msn = hdr.aeth.syndrome_msn;
        md.syndrome = hdr.aeth.syndrome_msn & 0xff000000;
        md.syndrome_opcode = hdr.aeth.syndrome_msn & 0x60000000;
    }

    action lshift_sn() {
        md.psn = md.psn << 8;
        md.msn = md.msn << 8;
    }

    action inc_psn() {
        md.psn = md.psn + 32w0x100;
    }

    action rshift_sn() {
        md.psn = md.psn >> 8;
        md.msn = md.msn >> 8;
    }

    action put_sn() {
        hdr.bth.ackreq_rsv_seqnum = md.ackreq_rsv | md.psn;
        hdr.aeth.syndrome_msn = md.syndrome | md.msn;
        // hdr.aeth.msn = md.msn[31:8];
    }

    apply {
        // if(hdr.aeth.syndrome_msn[31:29] == 0) get_psn();// ACK
        get_msn();// we must call get_msn first to get md.syndrome_opcode
        if(md.syndrome_opcode == 0) get_psn();// ACK
        else get_dec_psn();// NAK, RNR NAK
        lshift_sn();

        bitmap_update();
        if(md.new_bitmap == hdr.ack_bitmap.bitmap) {// first packet of a new aggregetion
            psn_init();
            msn_init();
        }
        else {
            psn_update();
            msn_update();
        }
        // newest psn/msn
        if(md.new_bitmap == hdr.ack_bitmap.bitmap_mask) {// last packet
            save_psn();
            save_msn();
        }
        else {
            load_psn();
            load_msn();
            // if(hdr.aeth.syndrome_msn[31:29] == 0) // drop only for ACK, allow NACK/RNR NACK
            if(md.syndrome_opcode == 0) // drop only for ACK, allow NACK/RNR NAK
                eg_dps_md.drop_ctl = 1;
        }
        // if(hdr.aeth.syndrome_msn[31:29] != 0) inc_psn();// NAK, RNR NAK
        if(md.syndrome_opcode != 0) inc_psn();// NAK, RNR NAK
        rshift_sn();
        put_sn();

        // if(hdr.aeth.syndrome_msn[31:29] == 0) 
        //     hdr.aeth.syndrome_msn[31:24] = 8w0b00011111;// turn off message level flow control
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
            hdr.conn.dst_rank : exact;
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

    apply { 
        // switch(hdr.bridge.pass_type) {
        //     BROADCAST_FORWARD_PASS: {
        //         broadcast_forward_egress.apply(hdr, md, eg_intr_md, eg_dps_md);
        //     }
        //     BROADCAST_BACKWARD_PASS: {
        //         broadcast_ack_egress.apply(hdr, md, eg_intr_md, eg_dps_md);
        //     }
        //     ALLREDUCE_FIRST_PASS:
        //     ALLREDUCE_SECOND_PASS: 
        //         {}
        // }
        eg_dps_md.drop_ctl = 0;
#ifdef TEST
        if(eg_intr_md.egress_rid == 123) {
            hdr.ack_bitmap.setInvalid();
            hdr.conn.setInvalid();
            hdr.bridge.setInvalid();
            return;
        }
#endif
        if(hdr.bridge.pass_type == BROADCAST_BACKWARD_PASS) {
            if(hdr.aeth.isValid()) // For ACK/NAK/RNR NAK
                broadcast_ack_egress.apply(hdr, md, eg_intr_md, eg_dps_md);
            // For CNP, forward directly
        }
        else if(hdr.bridge.pass_type == BROADCAST_FORWARD_PASS) {
            broadcast_forward_egress.apply(hdr, md, eg_intr_md, eg_dps_md);
        }

        if(hdr.bridge.pass_type == BROADCAST_FORWARD_PASS || hdr.bridge.pass_type == BROADCAST_BACKWARD_PASS) {
            restore_table.apply();
            restore_smac_table.apply();
            restore_dmac_table.apply();
        }

        dcqcn.apply(hdr, eg_intr_md);
        hdr.ack_bitmap.setInvalid();// only for backward
        hdr.conn.setInvalid();
        hdr.bridge.setInvalid();
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