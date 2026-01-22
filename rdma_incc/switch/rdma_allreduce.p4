/* -*- P4_16 -*- */
#include <core.p4>
#if __TARGET_TOFINO__ == 2
#include <t2na.p4>
#else
#include <tna.p4>
#endif

//#define TEST

#define REGISTER_LEN ((agg_addr_t)16384) // 4MB, 16k entries, 14bit address
// 20480 OK
// 24576 NOT OK
// 30720 NOT OK
// 45056 in theory at most

// you can edit these fields as long as you could pass compilation
#define MAX_GROUP_NUM 256
#define MAX_SERVER_NUM 256
#define MAX_QP_NUM 256

#include "allreduce_ingress.p4"
#include "allreduce_egress.p4"

Pipeline(IngressParser(), Ingress(), IngressDeparser(), EgressParser(), Egress(), EgressDeparser()) pipe;

Switch(pipe) main;
