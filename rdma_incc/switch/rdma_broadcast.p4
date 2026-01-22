/* -*- P4_16 -*- */
#include <core.p4>
#if __TARGET_TOFINO__ == 2
#include <t2na.p4>
#else
#include <tna.p4>
#endif

//#define TEST

// you can edit these fields as long as you could pass compilation
#define MAX_GROUP_NUM 4096
#define MAX_SERVER_NUM 4096
#define MAX_QP_NUM 4096

#include "broadcast_ingress.p4"
#include "broadcast_egress.p4"

Pipeline(IngressParser(), Ingress(), IngressDeparser(), EgressParser(), Egress(), EgressDeparser()) pipe;

Switch(pipe) main;
