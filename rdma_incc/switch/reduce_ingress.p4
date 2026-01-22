#include "std_header.p4"
#include "custom_header.p4"
#include "aggregator.p4"
#include "reduce_sendout.p4"
// #include "byteswap.p4"

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
        transition select(hdr.bth.opcode ++ hdr.udp.length) {
            RDMA_OP_SEND_FIRST ++ 16w280: parse_payload;
            RDMA_OP_SEND_MIDDLE ++ 16w280: parse_payload;
            RDMA_OP_SEND_LAST ++ 16w280: parse_payload;
            RDMA_OP_SEND_LAST_WITH_IMM ++ 16w284: parse_imm;
            RDMA_OP_SEND_ONLY ++ 16w280: parse_payload;
            RDMA_OP_SEND_ONLY_WITH_IMM ++ 16w284: parse_imm;
            RDMA_OP_WRITE_FIRST ++ 16w296: parse_reth;
            RDMA_OP_WRITE_MIDDLE ++ 16w280: parse_payload;
            RDMA_OP_WRITE_LAST ++ 16w280: parse_payload; 
            RDMA_OP_WRITE_LAST_WITH_IMM ++ 16w284: parse_imm;
            RDMA_OP_WRITE_ONLY ++ 16w296: parse_reth;
            RDMA_OP_WRITE_ONLY_WITH_IMM ++ 16w300: parse_reth;
            RDMA_OP_ACK ++ 16w28: parse_aeth;
            RDMA_OP_CNP ++ 16w40: parse_backward;
            default : parse_other;
        }
    }

    state parse_aeth {
        packet.extract(hdr.aeth);
        transition parse_backward;
    }

    state parse_backward {
        hdr.bridge.setValid();
        hdr.bridge.pass_type = INC_PASS;
        md.is_forward = IS_FALSE;
        transition accept;
    }

    state parse_reth {
        packet.extract(hdr.reth);
        transition select(hdr.bth.opcode) {
            RDMA_OP_WRITE_ONLY_WITH_IMM : parse_imm;
            default : parse_payload;
        }
    }

    state parse_imm {
        packet.extract(hdr.imm);
        transition parse_payload;
    }

    state parse_payload {
        packet.extract(hdr.payload);
        transition parse_icrc;
    }

    state parse_icrc {
        packet.extract(hdr.icrc);
        hdr.bridge.setValid();
        hdr.bridge.pass_type = INC_PASS;
        md.is_forward = IS_TRUE;
        transition accept;
    }

    state parse_other {
        hdr.bridge.setValid();
        hdr.bridge.pass_type = OTHER_PASS;
        md.is_forward = IS_OTHER;
        transition accept;
    }
}

struct bitmap_pair {
    agg_addr_t version;
    bitmap_t bitmap; // these two must have the same length
}

control swap_addr(inout headers hdr) {
    mac_addr_t mac_tmp;
    ip_addr_t ip_tmp;
    action swap_mac() {
        hdr.eth.dmac = hdr.eth.smac;
        hdr.eth.smac = mac_tmp;
    }
    action swap_ip() {
        hdr.ip.dip = hdr.ip.sip;
        hdr.ip.sip = ip_tmp;
    }
    apply {
        mac_tmp = hdr.eth.dmac;
        swap_mac();
        ip_tmp = hdr.ip.dip;
        swap_ip();
    }
}

control Ingress(
        inout headers hdr,
        inout ingress_metadata md,
        in ingress_intrinsic_metadata_t ig_intr_md,
        in ingress_intrinsic_metadata_from_parser_t ig_ps_md,
        inout ingress_intrinsic_metadata_for_deparser_t ig_dps_md,
        inout ingress_intrinsic_metadata_for_tm_t ig_tm_md) {

    // Register<bit<32>, agg_addr_t>(REGISTER_LEN, 0) reg_version;

    // RegisterAction<bit<32>, agg_addr_t, bit<1> >(reg_version) version_update_on_diff = {
    //     void apply(inout bit<32> ver, out bit<1> ret) {
    //         ret = 0;
    //         if(ver != md.seq_num) {
    //             ver = md.seq_num;
    //             ret = 1;
    //         }
    //     }
    // };

    Register<bitmap_pair, agg_addr_t>(REGISTER_LEN, {0,0}) reg_bitmap;

    RegisterAction<bitmap_pair, agg_addr_t, bitmap_t>(reg_bitmap) reg_bitmap_update = {
        void apply(inout bitmap_pair reg, out bitmap_t ret) {
            // ret = 0;
            // if(reg.version == md.seq_num) {
            //     ret = reg.bitmap;
            // }

            // if(reg.version == md.seq_num) {
            //     reg.bitmap = reg.bitmap | md.bitmap;
            // }
            // else {
            //     reg.version = md.seq_num;
            //     reg.bitmap = md.bitmap;
            // }

            // this version has no BUG
            if(reg.version == md.seq_num) {
                ret = reg.bitmap;
                reg.bitmap = reg.bitmap | md.bitmap;
            }
            else {
                reg.version = md.seq_num;
                ret = 0;
                reg.bitmap = md.bitmap;
            }
            // The code below will cause a BUG (needs >= 2 workers to trigger)
            // In this version, ret is set to be alu_hi instead of mem_hi
            // if(reg.version != md.seq_num) {
            //     reg.version = md.seq_num;
            //     ret = 0;
            //     reg.bitmap = md.bitmap;
            // }
            // else {
            //     ret = reg.bitmap;
            //     reg.bitmap = reg.bitmap | md.bitmap;
            // }
        }
    };

    action bitmap_update() {
        md.bitmap_old = reg_bitmap_update.execute(md.agg_addr);
    }

    // Register<bitmap_pair, agg_addr_t>(REGISTER_LEN, {0,0}) reg_ackmap;

    // RegisterAction<bitmap_pair, agg_addr_t, bitmap_t>(reg_ackmap) reg_ackmap_update = {
    //     void apply(inout bitmap_pair reg, out bitmap_t ret) {
    //         if(reg.version == md.seq_num) {
    //             reg.bitmap = reg.bitmap | md.ackmap;
    //         }
    //         else {
    //             reg.version = md.seq_num;
    //             reg.bitmap = md.ackmap;
    //         }
    //         ret = reg.bitmap;
    //     }
    // };

    // action ackmap_update() {
    //     md.ackmap_new = reg_ackmap_update.execute(md.agg_addr);
    // }

    // RegisterAction<bitmap_t, agg_addr_t, bitmap_t>(reg_bitmap) bitmap_update = {
    //     void apply(inout bitmap_t bitmap, out bitmap_t ret) {
    //         ret = bitmap;
    //         bitmap = bitmap | md.bitmap;
    //     }
    // };

    action get_metadata(MulticastGroupId_t group_id, ReplicationId_t src_rank, ReplicationId_t root_rank,
        bitmap_t bitmap, bitmap_t bitmap_mask, agg_addr_t agg_addr, 
        agg_addr_t agg_addr_offset_mask) {
        hdr.conn.setValid();
        hdr.conn.group_id = group_id;
        hdr.conn.src_rank = src_rank;
        hdr.conn.dst_rank = root_rank;
        md.bitmap = bitmap; // bitmap == 1 << rank
        md.bitmap_mask = bitmap_mask;// e.g., bitmap_mask == 0b110 in 3 workers
        md.agg_addr = agg_addr;
        // md.agg_addr_offset_mask = agg_addr_offset_mask;
        md.agg_offset = hdr.bth.ackreq_rsv_seqnum & agg_addr_offset_mask;
        // agg_addr_offset_mask must be less than 0xffffff
    }

    table metadata_table {
        key = {
            //md.resubmit: exact;
            hdr.ip.sip: exact;
            hdr.ip.dip: exact;
            hdr.bth.dqpn: exact;
        }
        actions = {
            get_metadata;
            NoAction;
        }
        size = MAX_QP_NUM; 
        default_action = NoAction();
    }

    action set_increase() {
        md.increase = 1;
    }

    action clear_increase() {
        md.increase = 0;
    }

    table increase_bit {
        key = {
            md.bitmap_old : ternary;
            md.bitmap : ternary;
        }
        actions = {
            set_increase;
            clear_increase;
        }
        const entries = {
#define POW2(k) (32w1<<(k))
#define ENTRY(k) (0 &&& POW2(k), POW2(k) &&& POW2(k))
            ENTRY(31) : set_increase();
            ENTRY(30) : set_increase();
            ENTRY(29) : set_increase();
            ENTRY(28) : set_increase();
            ENTRY(27) : set_increase();
            ENTRY(26) : set_increase();
            ENTRY(25) : set_increase();
            ENTRY(24) : set_increase();
            ENTRY(23) : set_increase();
            ENTRY(22) : set_increase();
            ENTRY(21) : set_increase();
            ENTRY(20) : set_increase();
            ENTRY(19) : set_increase();
            ENTRY(18) : set_increase();
            ENTRY(17) : set_increase();
            ENTRY(16) : set_increase();
            ENTRY(15) : set_increase();
            ENTRY(14) : set_increase();
            ENTRY(13) : set_increase();
            ENTRY(12) : set_increase();
            ENTRY(11) : set_increase();
            ENTRY(10) : set_increase();
            ENTRY(9) : set_increase();
            ENTRY(8) : set_increase();
            ENTRY(7) : set_increase();
            ENTRY(6) : set_increase();
            ENTRY(5) : set_increase();
            ENTRY(4) : set_increase();
            ENTRY(3) : set_increase();
            ENTRY(2) : set_increase();
            ENTRY(1) : set_increase();
            ENTRY(0) : set_increase();
            (_, _) : clear_increase();
#undef ENTRY
#undef POW2
        }
        size = 33;
    }

    action stage1() {
        md.agg_addr = md.agg_addr + md.agg_offset; 
        md.bitmap_flip = md.bitmap ^ md.bitmap_mask;
    }

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

    apply {
        // if(!hdr.recir.isValid()) {
        //     ByteSwapAll.apply(hdr);
        // }

        // on default, drop the packet if ig_intr_prsr_md!=0
        // if(md.other == 0) {
        //     hdr.payload.data = 0x40404040; // '@'
        //     hdr.ip.id = hdr.ip.id + 16w10000;
        //     hdr.udp.sport = hdr.udp.sport + 16w10000;
        // }
        // if(md.other == 1) {
        //     ig_tm_md.bypass_egress = 1;
        // }

        // 0
        if(hdr.bridge.pass_type == INC_PASS) {
            switch(metadata_table.apply().action_run) {// backward flows (ACK) for AllReduce are not in this table
                get_metadata: {
                    if(md.is_forward == IS_FALSE){
                        hdr.bridge.pass_type = REDUCE_BACKWARD_PASS;
                        // swap_addr.apply(hdr);
                    }
                    else if(hdr.recir.isValid()) {
                        hdr.bridge.pass_type = ALLREDUCE_SECOND_PASS;
                        hdr.eth.dmac = hdr.eth.smac;
                    }
                    else 
                        hdr.bridge.pass_type = ALLREDUCE_FIRST_PASS;
                }
                NoAction: {
                    hdr.bridge.pass_type = OTHER_PASS;
                }
            }
        }
        md.seq_num = hdr.bth.ackreq_rsv_seqnum & 32w0xffffff;

        // Turn off message level flow control
        // This is VERY VERY VERY important for the performance of SEND!!! 
        // two member: 1 QP 2 queue depth: 26->45 Gbps, 2 QP 2 queue depth: 0.7->75 Gbps, 1 QP 4 queue depth: 0.5->XXX
        // one member: 55 Gbps -> 75 Gbps in all cases
        if(md.is_forward == IS_FALSE) 
            if(hdr.aeth.isValid())
                if(hdr.aeth.syndrome_msn[31:29] == 0) // ACK packet
                    hdr.aeth.syndrome_msn[31:24] = 8w0b00011111;
        

        // 1
        stage1();

        // if((hdr.bth.ackreq_rsv_seqnum & 32w0x80000000) == 0) {
        //     md.ackmap = 0;
        // }
        // else {
        //     md.ackmap = md.bitmap;
        // }

        // 2
        if(hdr.bridge.pass_type == ALLREDUCE_FIRST_PASS) {
            bitmap_update();
            increase_bit.apply();
        }

        // if(hdr.bridge.pass_type != OTHER_PASS) {
        //     ackmap_update();
        // }

        // 3
        // Tofino even does not support "&" for two variables in PHV.
        //md.bit_old = md.bitmap_old & md.bitmap;
        
        //if((md.bitmap_to_fill & md.bitmap) != 0) md.increase = 1;
        //if(md.bitmap_old == 0) md.first = 1;

        // 4-11
        if(hdr.bridge.pass_type == ALLREDUCE_FIRST_PASS || hdr.bridge.pass_type == ALLREDUCE_SECOND_PASS) {
            AllAggregatorAccess.apply(hdr, md);
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