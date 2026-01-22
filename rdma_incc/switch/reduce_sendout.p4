#ifndef __REDUCE_SENDOUT_P4__
#define __REDUCE_SENDOUT_P4__
#include "std_header.p4"
#include "custom_header.p4"


control sendout(
        inout headers hdr,
        inout ingress_metadata md,
        in ingress_intrinsic_metadata_t ig_md,
        inout ingress_intrinsic_metadata_for_deparser_t ig_dps_md,
        inout ingress_intrinsic_metadata_for_tm_t ig_tm_md) {

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

    table root_unicast_table{
        key = {
            hdr.conn.group_id : exact;
        }
        actions = {
            forward;
            drop;
        }
        size = MAX_GROUP_NUM;
        default_action = drop();
    }

    table recirculate_table{
        key = {
            hdr.conn.group_id : exact;
        }
        actions = {
            forward;
            drop;
        }
        size = MAX_GROUP_NUM;
        default_action = drop();
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
        default_action = drop();//forward(148);
    }

    apply {
        // using || or && may cause BUGs
        if(md.bitmap_old == md.bitmap_flip) md.full = 1; 
        else if(md.bitmap_old == md.bitmap_mask) md.full = 1;
        else md.full = 0;

        // configure multicast
        if(hdr.bridge.pass_type == ALLREDUCE_SECOND_PASS) {// 
            root_unicast_table.apply(); 
        }
        else if(hdr.bridge.pass_type == ALLREDUCE_FIRST_PASS) {// configure unicast
            // drop on md.full == 0
            if(md.full == 0) {
                drop();
            }
            else {
                recirculate_table.apply();
            }
        }
        else if(hdr.bridge.pass_type == REDUCE_BACKWARD_PASS) {
            ig_tm_md.mcast_grp_a = hdr.conn.group_id; 
            invalidate(ig_tm_md.ucast_egress_port);//forward(9w0x1ff); // drop without set drop_ctl=0x1
            ig_dps_md.drop_ctl = 0;
        }
        else { // otherpass || (secondpass && is_multicast==0)
            unicast_table.apply();
        }
        if(hdr.eth.dmac[47:32] == 0xffff) {
            if(hdr.eth.dmac[31:0] == 0xffffffff) {
                ig_tm_md.level1_exclusion_id = (bit<16>)ig_md.ingress_port;
            }
        }

        // add conn header
        if(hdr.bridge.pass_type == ALLREDUCE_SECOND_PASS || hdr.bridge.pass_type == REDUCE_BACKWARD_PASS) {
            // hdr.conn.setValid();
            // hdr.conn.dst_rank = 0;

            // hdr.conn.ackmap = md.ackmap_new;
        }
        else {
            hdr.conn.setInvalid();
        }

        // change recir header
        if(hdr.bridge.pass_type == ALLREDUCE_FIRST_PASS && md.full == 1) {
            hdr.recir.setValid();
        }
//bfrt.pre.mgid.add(mgid, node_id, [True]*len(node_id), node_id)
    }
}

#endif