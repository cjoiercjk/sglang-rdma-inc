#ifndef __CUSTOM_HEADER_STRUCT_P4__
#define __CUSTOM_HEADER_STRUCT_P4__

#include "std_header.p4"

typedef bit<32> bitmap_t;
typedef bit<32> agg_addr_t;
typedef int<32> agg_t;

header bridge_t {// any ingress -> egress
    bit<8> pass_type;
    @padding bit<8> _pad;
}

header recir_t {// first pass ingress -> first pass egress -> second pass ingress -> second pass egress
    bit<1> enable_multicast;
    @padding bit<15> _pad;
}

header conn_t {// second pass ingress -> second pass egress
    MulticastGroupId_t group_id;
    ReplicationId_t src_rank;
    ReplicationId_t dst_rank; // only used in broadcast for now
    bitmap_t ackmap; // deprecated, has been used for record ACK-require bit in header
}

header ack_bitmap_t {
    bit<32> bitmap;
    bit<32> bitmap_mask;
}

header payload_t {
    agg_t data00;
    agg_t data01;
    agg_t data02;
    agg_t data03;
    agg_t data04;
    agg_t data05;
    agg_t data06;
    agg_t data07;
    agg_t data08;
    agg_t data09;
    agg_t data0a;
    agg_t data0b;
    agg_t data0c;
    agg_t data0d;
    agg_t data0e;
    agg_t data0f;
    agg_t data10;
    agg_t data11;
    agg_t data12;
    agg_t data13;
    agg_t data14;
    agg_t data15;
    agg_t data16;
    agg_t data17;
    agg_t data18;
    agg_t data19;
    agg_t data1a;
    agg_t data1b;
    agg_t data1c;
    agg_t data1d;
    agg_t data1e;
    agg_t data1f;
    agg_t data20;
    agg_t data21;
    agg_t data22;
    agg_t data23;
    agg_t data24;
    agg_t data25;
    agg_t data26;
    agg_t data27;
    agg_t data28;
    agg_t data29;
    agg_t data2a;
    agg_t data2b;
    agg_t data2c;
    agg_t data2d;
    agg_t data2e;
    agg_t data2f;
    agg_t data30;
    agg_t data31;
    agg_t data32;
    agg_t data33;
    agg_t data34;
    agg_t data35;
    agg_t data36;
    agg_t data37;
    agg_t data38;
    agg_t data39;
    agg_t data3a;
    agg_t data3b;
    agg_t data3c;
    agg_t data3d;
    agg_t data3e;
    agg_t data3f;
}

struct agg_pair_t{
    agg_t agg0;
    agg_t agg1;
}

struct headers {// Make sure you have a right order in parser, or BUGs may occur.
    bridge_t bridge;
    recir_t recir;
    ack_bitmap_t ack_bitmap; // only for broadcast
    conn_t conn;
    eth_t eth;
    ip_t ip;
    udp_t udp;
    bth_t bth;
    cnp_t cnp;
    aeth_t aeth;
    reth_t reth;
    imm_t imm;
    payload_t payload;
    icrc_t icrc;
}

struct port_metadata_t {
    bit<1> is_recirculate_port;
    @padding bit<15> _pad; 
}

#define ALLREDUCE_FIRST_PASS 8w1
#define ALLREDUCE_SECOND_PASS 8w2
// #define ALLREDUCE_BACKWARD_PASS 8w3
#define BROADCAST_FORWARD_PASS 8w4
#define BROADCAST_BACKWARD_PASS 8w5
#define REDUCE_BACKWARD_PASS 8w6
#define INC_PASS 8w254
#define OTHER_PASS 8w255

#define IS_TRUE 1
#define IS_FALSE 2
#define IS_OTHER 3

struct ingress_metadata {
    // allreduce
    port_metadata_t port_metadata;
    
    bit<2> is_forward;
    bit<1> increase;
    bit<1> full;

    bitmap_t bitmap;
    bitmap_t bitmap_mask;
    bitmap_t bitmap_old;
    bitmap_t bitmap_flip;
    //bitmap_t bit_old;
    // bitmap_t ackmap;
    // bitmap_t ackmap_new;

    agg_addr_t agg_addr;
    agg_addr_t agg_offset;
    agg_addr_t seq_num;
}

struct egress_metadata {
    // allreduce

    // broadcast
    bit<32> new_bitmap;
    bit<32> psn;
    bit<32> ackreq_rsv;
    bit<32> msn;
    bit<32> syndrome;
    bit<32> syndrome_opcode;
}

#endif