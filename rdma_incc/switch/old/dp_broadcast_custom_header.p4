#ifndef __CUSTOM_HEADER_STRUCT_P4__
#define __CUSTOM_HEADER_STRUCT_P4__

#include "std_header.p4"

typedef bit<32> bitmap_t;
typedef bit<16> group_t;
typedef bit<8> rank_t;// only 0~31 is valid

header bridge_t {// any ingress -> egress
    bit<8> pass_type;
    @padding bit<8> _pad;
}

header bc_t {
    group_t gpid;
    rank_t rank;// for backward packet
    @padding bit<8> _pad; 
    bit<32> vrqpn;
    bit<32> vsqpn;// for backward packet
    bitmap_t bitmap;
    bitmap_t bitmap_mask;
}

header control_t {// receiver side is managed by controller
    bit<32> num;
    bitmap_t bitmap_mask;
    bit<32> sip;
    bit<32> sqpn;
    bit<32> psn;
    bit<32> msn;
}

header vsqpn_t {// 14/16B * 32
    bit<16> qpn;
    @padding bit<16> _pad; 
    bit<32> rkey;
    bit<64> mem_addr;
}

struct headers {// Make sure you have a right order in parser, or BUGs may occur.
    bridge_t bridge;
    bc_t bc;
    eth_t eth;
    ip_t ip;
    udp_t udp;
    bth_t bth;
    cnp_t cnp;
    aeth_t aeth;
    reth_t reth;
    imm_t imm;
    control_t control;
    
    vsqpn_t[32] vsqpn;
}

struct port_metadata_t {
    bit<1> is_recirculate_port;
    @padding bit<15> _pad; 
}

#define INC_PASS 8w1
#define BROADCAST_FORWARD_PASS 8w2
#define BROADCAST_BACKWARD_PASS 8w4
#define BROADCAST_CONTROL_PASS 8w6
#define OTHER_PASS 8w7

#define IS_TRUE 1
#define IS_FALSE 2

struct ingress_metadata {
    bit<16> sender_port;
}

struct egress_metadata {
    rank_t bc_order;
}

#endif