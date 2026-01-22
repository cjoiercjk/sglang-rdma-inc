/* -*- P4_16 -*- */
#include <core.p4>
#if __TARGET_TOFINO__ == 2
#include <t2na.p4>
#else
#include <tna.p4>
#endif

#include "std_header.p4"

/*************************************************************************
*********************** H E A D E R S  ***********************************
*************************************************************************/

struct headers {
    eth_t eth;
    ip_t ip;
    udp_t udp;
    bth_t bth;
}

struct port_metadata_t {
    bit<16> unused; 
}

struct metadata {
    port_metadata_t port_metadata;
}

/*************************************************************************
*********************** P A R S E R  ***********************************
*************************************************************************/

parser IngressParser(packet_in packet,
               out headers hdr,
               out metadata meta,
               out ingress_intrinsic_metadata_t ig_intr_md) {

    state start {
        packet.extract(ig_intr_md);
        transition select(ig_intr_md.resubmit_flag) {
            0 : parse_port_metadata;
        }
    }

    state parse_port_metadata {
        meta.port_metadata = port_metadata_unpack<port_metadata_t>(packet);
        transition parse_ethernet;
    }

    state parse_ethernet {
        packet.extract(hdr.eth);
        transition select(hdr.eth.protocol) {
            IP_PROTOCOL : parse_ip;
            default     : accept;
        }
    }

    state parse_ip {
        packet.extract(hdr.ip);
        transition select(hdr.ip.protocol) {
            UDP_PROTOCOL : parse_udp;
            default      : accept;
        }
    }

    state parse_udp {
        packet.extract(hdr.udp);
        transition select(hdr.udp.dport) {
            RDMA_DPORT : parse_bth;
            default    : accept;
        }
    }

    state parse_bth {
        packet.extract(hdr.bth);
        transition accept;
    }
}


/*************************************************************************
**************  I N G R E S S   P R O C E S S I N G   *******************
*************************************************************************/

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
        inout metadata meta,
        in ingress_intrinsic_metadata_t ig_intr_md,
        in ingress_intrinsic_metadata_from_parser_t ig_intr_prsr_md,
        inout ingress_intrinsic_metadata_for_deparser_t ig_intr_dprs_md,
        inout ingress_intrinsic_metadata_for_tm_t ig_intr_tm_md) {
    
    action drop() {
        ig_intr_dprs_md.drop_ctl = 0x1;
    }

    action l2_forward(bit<9> port) {
        ig_intr_tm_md.ucast_egress_port = port;
    }

    table l2_forward_table{
        key = {
            hdr.eth.dmac: exact;
        }
        actions = {
            l2_forward;
            drop;
        }
        size = 32;
        default_action = drop();
    }

    apply {
        if(hdr.bth.isValid()) swap_addr.apply(hdr);
        l2_forward_table.apply();
    }
}

/*************************************************************************
****************  E G R E S S   P R O C E S S I N G   *******************
*************************************************************************/

control IngressDeparser(
        packet_out packet,
        inout headers hdr,
        in metadata meta,
        in ingress_intrinsic_metadata_for_deparser_t ig_intr_dprsr_md) {

    apply{
        packet.emit(hdr);
    }
}

parser EgressParser(packet_in packet,
               out headers hdr,
               out metadata meta,
               out egress_intrinsic_metadata_t eg_intr_md) {
    state start {
        packet.extract(eg_intr_md);
        transition parse_ethernet;
    }

    state parse_ethernet {
        packet.extract(hdr.eth);
        transition select(hdr.eth.protocol) {
            0x0800: parse_ip;
            default: accept;
        }
    }

    state parse_ip{
        packet.extract(hdr.ip);
        transition accept;
    }
}

control red_ecn(
    inout headers hdr,
    in egress_intrinsic_metadata_t eg_intr_md) {

    Wred<bit<19>, bit<32>>(32w1, 8w1, 8w0) wred;
    apply {
        if(hdr.ip.isValid()) {
            if(hdr.ip.dscp_ecn[1:0] == 0) { // Using "!=" and "&&" sometimes causes BUG
            }
            else {
                bit<8> drop_flag = wred.execute(eg_intr_md.deq_qdepth, 0);
                if(drop_flag == 1) hdr.ip.dscp_ecn[1:0] = 3;
            }
        }
    }
}

control Egress(
        inout headers hdr,
        inout metadata meta,
        in egress_intrinsic_metadata_t eg_intr_md,
        in egress_intrinsic_metadata_from_parser_t eg_intr_prsr_md,
        inout egress_intrinsic_metadata_for_deparser_t ig_intr_dprs_md,
        inout egress_intrinsic_metadata_for_output_port_t eg_intr_oport_md) {
    apply { 
        red_ecn.apply(hdr, eg_intr_md);
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
                  in metadata meta,
                  in egress_intrinsic_metadata_for_deparser_t ig_intr_dprs_md) {
    
    apply { 
        EgressChecksum.apply(hdr);
        packet.emit(hdr);
    }
}

Pipeline(IngressParser(), Ingress(), IngressDeparser(), EgressParser(), Egress(), EgressDeparser()) pipe;

Switch(pipe) main;
