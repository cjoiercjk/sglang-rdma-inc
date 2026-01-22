#ifndef __BROADCAST_SENDOUT_P4__
#define __BROADCAST_SENDOUT_P4__
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

    action broadcast_exclude(in MulticastGroupId_t mgid, in ReplicationId_t excl_id) {
        ig_tm_md.mcast_grp_a = mgid; 
        ig_tm_md.level1_exclusion_id = excl_id;
        invalidate(ig_tm_md.ucast_egress_port);//forward(9w0x1ff); // an invalid port
        ig_dps_md.drop_ctl = 0;
    }

    apply {
        if(hdr.bridge.pass_type == BROADCAST_FORWARD_PASS) {
            broadcast_exclude(hdr.conn.group_id, hdr.conn.src_rank);
        }
        else if(hdr.bridge.pass_type == BROADCAST_BACKWARD_PASS) {
            root_unicast_table.apply();
        }
        else {
            unicast_table.apply();
        }
        if(hdr.eth.dmac[47:32] == 0xffff) {
            if(hdr.eth.dmac[31:0] == 0xffffffff) {
                ig_tm_md.level1_exclusion_id = (bit<16>)ig_md.ingress_port;
            }
        }
    }
}

#endif