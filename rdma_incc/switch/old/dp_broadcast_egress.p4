#include "std_header.p4"
#include "dp_broadcast_custom_header.p4"
#include "dcqcn.p4"

parser EgressParser(packet_in packet,
               out headers hdr,
               out egress_metadata md,
               out egress_intrinsic_metadata_t eg_intr_md) {

    ParserCounter() bc_cnt;

    // only allreduce flows come in
    state start {
        packet.extract(eg_intr_md);
        packet.extract(hdr.bridge);
        transition select(hdr.bridge.pass_type) {
            BROADCAST_CONTROL_PASS : parse_bc;
            BROADCAST_FORWARD_PASS : parse_bc;
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

    state parse_recirculate {
        packet.extract(hdr.recir);
        transition select(hdr.bridge.pass_type) {
            ALLREDUCE_FIRST_PASS : parse_inner_packet;
            ALLREDUCE_SECOND_PASS : parse_conn;
        }
    }

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
            // RDMA_OP_WRITE_FIRST : parse_reth;
            // RDMA_OP_WRITE_MIDDLE : accept;
            // RDMA_OP_WRITE_LAST : accept; 
            // RDMA_OP_WRITE_LAST_WITH_IMM : accept;
            // RDMA_OP_WRITE_ONLY : parse_reth;
            // RDMA_OP_WRITE_ONLY_WITH_IMM : parse_reth;
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
        md.psn = hdr.bth.ackreq_rsv_seqnum + md.psn_offset;
    }

    action dec_psn() {
        md.psn = md.psn - 1;
    }

    action get_msn() {
        md.msn = hdr.aeth.syndrome_msn + md.msn_offset;
    }

    action lshift_sn() {
        md.psn = md.psn << 8;
        md.msn = md.msn << 8;
    }

    action inc_psn() {
        md.psn = md.psn + 32w0x100;
    }

    action put_rshift_sn() {
        hdr.bth.ackreq_rsv_seqnum = md.psn >> 8;
        hdr.aeth.syndrome_msn[23:0] = md.msn[31:8];
    }

    apply {
        // 0 
        get_psn();// ACK
        get_msn();
        // 1
        dec_psn();// NAK, RNR NAK
        // 2
        lshift_sn();

        bitmap_update();
        // 3
        if(md.new_bitmap == hdr.ack_bitmap.bitmap) {// first packet of a new aggregetion
            psn_init();
            msn_init();
        }
        else {
            psn_update();
            msn_update();
        }
        // 4
        // newest psn/msn
        if(md.new_bitmap == hdr.ack_bitmap.bitmap_mask) {// last packet
            save_psn();
            save_msn();
        }
        else {
            load_psn();
            load_msn();
            if(hdr.aeth.syndrome_msn[31:29] == 0) // drop only for ACK, allow NACK/RNR NACK
                eg_dps_md.drop_ctl = 1;
        }
        // 5
        if(hdr.aeth.syndrome_msn[31:29] != 0) inc_psn();// NAK, RNR NAK
        // 6
        put_rshift_sn();

        if(hdr.aeth.syndrome_msn[31:29] == 0) 
            hdr.aeth.syndrome_msn[31:24] = 8w0b00011111;// turn off message level flow control
    }
}

control Egress(
        inout headers hdr,
        inout egress_metadata md,
        in egress_intrinsic_metadata_t eg_intr_md,
        in egress_intrinsic_metadata_from_parser_t eg_ps_md,
        inout egress_intrinsic_metadata_for_deparser_t eg_dps_md,
        inout egress_intrinsic_metadata_for_output_port_t eg_op_md) {

    // 
    Register<rank_t, group_t>(1024) reg_order;

    RegisterAction<rank_t, group_t, rank_t>(reg_order) reg_reset_order = {
        void apply(inout rank_t reg, out rank_t order) {
            reg = 0;
            order = 0;
        }
    }

    RegisterAction<rank_t, group_t, rank_t>(reg_order) reg_update_order = {
        void apply(inout rank_t reg, out rank_t order) {
            if(reg < md.last_order) {
                reg = reg + 1;
            }
            order = reg;
        }
    }

    Register<bit<32>, bit<32>>(1024) reg_vsqpn; 

    RegisterAction<bit<32>, bit<32>, void>(reg_vsqpn) reg_update_vsqpn = {
        void apply(inout bit<32> reg) {
            reg = hdr.bc.vsqpn;
        }
    }

    RegisterAction<bit<32>, bit<32>, bit<32>>(reg_vsqpn) reg_get_vsqpn = {
        void apply(inout bit<32> reg, out bit<32> vsqpn) {
            vsqpn = reg;
        }
    }

    action reset_order() {
        md.bc_order = reg_reset_order.execute(md.gpid);
    }

    action update_order() {
        md.bc_order = reg_update_order.execute(md.gpid);
    }

    action restore_dst_fields(ip_addr_t dip, bit<24> dqpn) {
        hdr.ip.dip = dip;
        hdr.bth.dqpn = dqpn;
    }

    action restore_src_fields(ip_addr_t sip) {
        hdr.ip.sip = sip;
    }

    table restore_table{
        key = {
            hdr.conn.group_id : exact;
            hdr.conn.src_rank : exact;
            hdr.conn.dst_rank : exact;
            // hdr.reth.isValid() : exact;
        }
        actions = {
            restore_dst_fields;
            restore_src_fields;
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
        eg_dps_md.drop_ctl = 0;
        
        if(hdr.bridge.pass_type == BROADCAST_CONTROL_PASS) {// contorl -> ACK (aggregate)
            
            // 
            get_vsqpn_from_pkt.apply();// hdr.bc.vsqpn = pkt.vsqpn[rid]

            bit<32> indirect_addr = hdr.bc.vrqpn + rid;
            reg_update_vsqpn.execute(indirect_addr);// reg_vsqpn[indirect_addr] = hdr.bc.vsqpn;

            update_address_info.apply();// info[hdr.bc.vsqpn] = sip, sqpn, mem_addr, rkey, 
            
            if(md.order == md.last_order) {
                send_control_ACK();
            }
        }
        else if(hdr.bridge.pass_type == BROADCAST_CONTROL_SECOND_PASS) {
            update_sn.apply(); // psn_in, msn_in
        }
        else if(hdr.bridge.pass_type == BROADCAST_FORWARD_PASS) {
            bit<32> order = 16w0 ++ eg_intr_md.egress_rid;
            bit<32> indirect_addr = hdr.bc.vrqpn + order;
            reg_get_vsqpn.execute(indirect_addr);// hdr.bc.vsqpn = reg_vsqpn[md.reg_addr]

            get_address_info.apply();// 

            mirror_table.apply();// set mirror_port, dip, dqpn, drop packet
            hdr.bridge.pass_type = BROADCAST_FORWARD_SECOND_PASS;
        }
        else if(hdr.bridge.pass_type == BROADCAST_FORWARD_SECOND_PASS) {
            // update_sn
            md.psn_offset = md.psn_out - md.psn_in;
            pkt.psn = pkt.psn + md.psn_offset;
            update_pkt1;// overwrite pkt with switch.send_ip, vsqpn, dip, dqpn, mem_addr, rkey, psn_offset
            if(md.psn_offset == 1) {
                psn_in ++;
                psn_out ++;
                if(is_last_table.apply().hit) {// last packet of a message 
                    msn_in ++;
                    msn_out ++;
                }
            }
        }
        else if(hdr.bridge.pass_type == BROADCAST_BACKWARD_PASS) {
            // 0+ 
            get_sn.apply(); // get sip, sqpn, dip, dqpn, mem_addr, rkey, psn_in, msn_in
            // 1
            md.psn_offset = md.psn_in - md.psn_out;// later used in broadcast_ack_egress
            md.msn_offset = md.msn_in - md.msn_out;

            update_pkt2;// overwrite pkt with switch.recv_ip, vrqpn, sip, sqpn, psn_offset

            if(hdr.aeth.isValid())
                broadcast_ack_egress.apply(hdr, md, eg_intr_md, eg_dps_md);

            get_address_info.apply();
        }

        if(hdr.bridge.pass_type != OTHER_PASS) {
            restore_smac_table.apply();
            restore_dmac_table.apply();
        }

        dcqcn.apply(hdr, eg_intr_md);
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