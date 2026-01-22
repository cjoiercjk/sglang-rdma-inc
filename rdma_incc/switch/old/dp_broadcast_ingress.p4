#include "std_header.p4"
#include "dp_broadcast_custom_header.p4"

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
            RDMA_OP_WRITE_FIRST: parse_reth;
            RDMA_OP_WRITE_MIDDLE: inc_accept;
            RDMA_OP_WRITE_LAST: inc_accept; 
            RDMA_OP_WRITE_LAST_WITH_IMM: inc_accept;
            RDMA_OP_WRITE_ONLY: parse_reth;
            RDMA_OP_WRITE_ONLY_WITH_IMM: parse_reth;
            RDMA_OP_ACK: parse_aeth;
            RDMA_OP_CNP: parse_cnp;
            RDMA_OP_SEND_FIRST: inc_accept;
            RDMA_OP_SEND_MIDDLE: inc_accept;
            RDMA_OP_SEND_LAST: inc_accept;
            RDMA_OP_SEND_LAST_WITH_IMM : inc_accept;
            RDMA_OP_SEND_ONLY: inc_accept;
            RDMA_OP_SEND_ONLY_WITH_IMM : inc_accept;
            default : parse_other;
        }
    }

    state parse_reth {
        packet.extract(hdr.reth);
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

    action get_broadcast_forward_metadata() {
    }

    action get_broadcast_backward_metadata() {
    }

    action set_otherpass() {
    }

    table metadata_table {
        key = {
            // Since there is N->1 traffic, we need an extra field "SIP" to recognize a flow
            hdr.ip.dip: exact;
        }
        actions = {
            get_broadcast_forward_metadata;
            get_broadcast_backward_metadata;
            //get_resubmit_INA_metadata;
            set_otherpass;
        }
        size = MAX_QP_NUM * 2;
        default_action = set_otherpass();
    }

    action get_control_metadata() {

    }

    table control_filter_table {
        key = {
            hdr.bth.opcode: exact;
            hdr.reth.rkey: exact;
            hdr.reth.mem_addr: exact;
        }
        actions = {
            get_control_metadata;
        }
        const entries = {
            (RDMA_OP_WRITE_ONLY, ~32w0, ~64w0): get_control_metadata();
        }
    }

    action drop() {
        ig_dps_md.drop_ctl = 0x1;
    }

    action forward(bit<9> port) {
        ig_dps_md.drop_ctl = 0;
        ig_tm_md.ucast_egress_port = port;
    }

    table unicast_table{
        key = {
            hdr.eth.dmac : exact;
        }
        actions = {
            forward;
            drop;
        }
        size = MAX_SERVER_NUM;
        default_action = drop();
    }

    action shift_result(bitmap_t res) 
    {
        hdr.bc.bitmap = res;
    }

    table lshift_table{
        key = {
            hdr.bth.dqpn : ternary;
        }
        actions = {
            shift_result;
        }
        const entries = {
#define ENTRY(i) 32w##i &&& 32w0x1f : shift_result(1<<i)
            ENTRY(0);
            ENTRY(1);
            ENTRY(2);
            ENTRY(3);
            ENTRY(4);
            ENTRY(5);
            ENTRY(6);
            ENTRY(7);
            ENTRY(8);
            ENTRY(9);
            ENTRY(10);
            ENTRY(11);
            ENTRY(12);
            ENTRY(13);
            ENTRY(14);
            ENTRY(15);
            ENTRY(16);
            ENTRY(17);
            ENTRY(18);
            ENTRY(19);
            ENTRY(20);
            ENTRY(21);
            ENTRY(22);
            ENTRY(23);
            ENTRY(24);
            ENTRY(25);
            ENTRY(26);
            ENTRY(27);
            ENTRY(28);
            ENTRY(29);
            ENTRY(30);
            ENTRY(31);
#undef ENTRY 
        }
    }

    Register<bit<16>, group_t>>(1024) reg_bc_num;

    RegisterAction<bit<16>, group_t, bit<16>>(reg_bc_num) reg_set_bc_num = {
        void apply(inout bit<16> reg, out bit<16> bc_num) {
            reg = hdr.control.num;
            bc_num = reg;
        }
    };

    RegisterAction<bit<16>, group_t, bit<16>>(reg_bc_num) reg_get_bc_num = {
        void apply(inout bit<16> reg, out bit<16> bc_num) {
            bc_num = reg;
        }
    };

    action set_bc_num() 
    {
        ig_tm_md.mcast_grp_a = reg_set_bc_num.execute(hdr.bc.gpid);
        ig_tm_md.qid = 1;// Is this useful?
        ig_dps_md.drop_ctl = 0;
    }

    action get_bc_num()
    {
        ig_tm_md.mcast_grp_a = reg_get_bc_num.execute(hdr.bc.gpid);
        ig_dps_md.drop_ctl = 0;
    }

    Register<bit<16>, group_t>(1024) reg_sender_port;

    RegisterAction<bit<16>, group_t, void>(reg_sender_port) reg_set_sender_port = {
        void apply(inout bit<16> reg) {
            reg = md.sender_port;
        }
    }

    RegisterAction<bit<16>, group_t, bit<16>>(reg_sender_port) reg_get_sender_port = {
        void apply(inout bit<16> reg, out bit<16> sender_port) {
            sender_port = reg;
        }
    }

    action set_sender_port() 
    {
        reg_set_sender_port.execute(hdr.bc.gpid);// = ig_intr_md.ingress_port
    }

    action get_sender_port()
    {
        md.sender_port = reg_get_sender_port.execute(hdr.bc.gpid);
    }

    Register<bitmap_t, group_t>(1024) reg_bitmap;

    RegisterAction<bitmap_t, group_t, bitmap_t>(reg_bitmap) reg_set_bitmap_mask = {
        void apply(inout bitmap_t reg, out bitmap_t bitmap) {
            reg = hdr.control.bitmap_mask;
            bitmap = reg;
        }
    }

    RegisterAction<bitmap_t, group_t, bitmap_t>(reg_bitmap) reg_get_bitmap_mask = {
        void apply(inout bitmap_t reg, out bitmap_t bitmap) {
            bitmap = reg;
        }
    }

    action set_bitmap_mask()
    {
        hdr.bc.bitmap_mask = reg_set_bitmap_mask.execute(hdr.bc.gpid);
    }

    action get_bitmap_mask()
    {
        hdr.bc.bitmap_mask = reg_get_bitmap_mask.execute(hdr.bc.gpid);
    }

    apply {
        hdr.bc.setValid();
        hdr.bc.gpid = hdr.bth.dqpn[20:5];
        hdr.bc.rank = 3w0 ++ hdr.bth.dqpn[4:0];
        hdr.bc.vrqpn = 11w0 + hdr.bth.dqpn[20:5] + 5w0;
        hdr.bc.vsqpn = 11w0 ++ hdr.bth.dqpn[20:0];
        lshift_table.apply(); // hdr.bc.bitmap = 1<<rank
        md.sender_port = 0;
        md.sender_port[8:0] = ig_intr_md.ingress_port;

        if(hdr.bridge.pass_type == INC_PASS) {
            switch(metadata_table.apply().action_run) {
                get_broadcast_forward_metadata : {
                    switch(control_filter_table.apply().action_run) {
                        get_control_metadata: hdr.bridge.pass_type = BROADCAST_CONTROL_PASS;
                        default: hdr.bridge.pass_type = BROADCAST_FORWARD_PASS;
                    }
                }
                get_broadcast_backward_metadata : {
                    hdr.bridge.pass_type = BROADCAST_BACKWARD_PASS;
                }
                set_otherpass: {
                    hdr.bridge.pass_type = OTHER_PASS;
                }
            }
        }   
        
        if(hdr.bridge.pass_type == BROADCAST_CONTROL_PASS) {
            set_bc_num();
            set_sender_port();
            set_bitmap_mask();
        }
        else if(hdr.bridge.pass_type == BROADCAST_FORWARD_PASS){
            get_bc_num();
        }
        else if(hdr.bridge.pass_type == BROADCAST_BACKWARD_PASS) {
            get_sender_port();
            forward(md.sender_port[8:0]);
            get_vsqp_info_bitmap_mask();
        }
        else {
            unicast_table.apply();
        }
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