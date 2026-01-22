#ifndef __AGGREGATOR_P4__
#define __AGGREGATOR_P4__

#include "custom_header.p4"

control AggregatorAccess(
        inout agg_t packet_agg0,
        inout agg_t packet_agg1, 
        in bit<8> pass_type,
        in ingress_metadata md) {

    Register<agg_pair_t, agg_addr_t>(REGISTER_LEN) aggregator_array;

    RegisterAction<agg_pair_t, agg_addr_t, void>(aggregator_array) reg_overwrite = {
        void apply(inout agg_pair_t agg_pair) {
            agg_pair.agg0 = packet_agg0;
            agg_pair.agg1 = packet_agg1;
        }
    };
    RegisterAction<agg_pair_t, agg_addr_t, agg_t>(aggregator_array) reg_aggregate = {
        void apply(inout agg_pair_t agg_pair, out agg_t ret) {
            // Although there is no overflow in our workload,
            // we still use "|+|" instead of "+".
            agg_pair.agg0 = agg_pair.agg0 |+| packet_agg0;
            agg_pair.agg1 = agg_pair.agg1 |+| packet_agg1;
            ret = agg_pair.agg0;
        }
    };
    RegisterAction<agg_pair_t, agg_addr_t, agg_t>(aggregator_array) reg_read0 = {
        void apply(inout agg_pair_t agg_pair, out agg_t ret) {
            ret = agg_pair.agg0;
        }
    };
    RegisterAction<agg_pair_t, agg_addr_t, agg_t>(aggregator_array) reg_read1 = {
        void apply(inout agg_pair_t agg_pair, out agg_t ret) {
            ret = agg_pair.agg1;
        }
    };
    action overwrite() {
        reg_overwrite.execute(md.agg_addr);
    }
    action aggregate() {
        packet_agg0 = reg_aggregate.execute(md.agg_addr);
    }
    action read0() {
        packet_agg0 = reg_read0.execute(md.agg_addr);
    }
    action read1() {
        packet_agg1 = reg_read1.execute(md.agg_addr);
    }
    table agg_action {
        key = {
            pass_type    : ternary;
            md.bitmap_old   : ternary;
            md.increase     : ternary;
        }
        actions = {
            overwrite;
            aggregate;
            read0;
            read1;
        }
        const entries = {
            // Fields can be "0x123", "_" or "0x020 &&& 0x0f0".
            // "()" is not necessary if there is only one key
            // "()" can also be replaced by "{}".
            // "_" equals "0 &&& 0".
            // "_" can also be used to replace "(_, _, _, ...)".
            // The priority of const entries in ternary table is as the order they specified.
            // Eg. if the first entry matches, the second will not be considered.
            (ALLREDUCE_SECOND_PASS, _, _) : read1();
            (_, 0, _) : overwrite();
            (_, _, 1) : aggregate();
            (_, _, _) : read0();
        }
        size = 4;
    }

    apply {
        agg_action.apply();
    }
}

control AllAggregatorAccess(
        inout headers hdr,
        inout ingress_metadata md) {

    AggregatorAccess() AA00;
    AggregatorAccess() AA01;
    AggregatorAccess() AA02;
    AggregatorAccess() AA03;
    AggregatorAccess() AA04;
    AggregatorAccess() AA05;
    AggregatorAccess() AA06;
    AggregatorAccess() AA07;
    AggregatorAccess() AA08;
    AggregatorAccess() AA09;
    AggregatorAccess() AA0a;
    AggregatorAccess() AA0b;
    AggregatorAccess() AA0c;
    AggregatorAccess() AA0d;
    AggregatorAccess() AA0e;
    AggregatorAccess() AA0f;
    AggregatorAccess() AA10;
    AggregatorAccess() AA11;
    AggregatorAccess() AA12;
    AggregatorAccess() AA13;
    AggregatorAccess() AA14;
    AggregatorAccess() AA15;
    AggregatorAccess() AA16;
    AggregatorAccess() AA17;
    AggregatorAccess() AA18;
    AggregatorAccess() AA19;
    AggregatorAccess() AA1a;
    AggregatorAccess() AA1b;
    AggregatorAccess() AA1c;
    AggregatorAccess() AA1d;
    AggregatorAccess() AA1e;
    AggregatorAccess() AA1f;

    apply {
        AA00.apply(hdr.payload.data00, hdr.payload.data01, hdr.bridge.pass_type, md);
        AA01.apply(hdr.payload.data02, hdr.payload.data03, hdr.bridge.pass_type, md);
        AA02.apply(hdr.payload.data04, hdr.payload.data05, hdr.bridge.pass_type, md);
        AA03.apply(hdr.payload.data06, hdr.payload.data07, hdr.bridge.pass_type, md);
        AA04.apply(hdr.payload.data08, hdr.payload.data09, hdr.bridge.pass_type, md);
        AA05.apply(hdr.payload.data0a, hdr.payload.data0b, hdr.bridge.pass_type, md);
        AA06.apply(hdr.payload.data0c, hdr.payload.data0d, hdr.bridge.pass_type, md);
        AA07.apply(hdr.payload.data0e, hdr.payload.data0f, hdr.bridge.pass_type, md);
        AA08.apply(hdr.payload.data10, hdr.payload.data11, hdr.bridge.pass_type, md);
        AA09.apply(hdr.payload.data12, hdr.payload.data13, hdr.bridge.pass_type, md);
        AA0a.apply(hdr.payload.data14, hdr.payload.data15, hdr.bridge.pass_type, md);
        AA0b.apply(hdr.payload.data16, hdr.payload.data17, hdr.bridge.pass_type, md);
        AA0c.apply(hdr.payload.data18, hdr.payload.data19, hdr.bridge.pass_type, md);
        AA0d.apply(hdr.payload.data1a, hdr.payload.data1b, hdr.bridge.pass_type, md);
        AA0e.apply(hdr.payload.data1c, hdr.payload.data1d, hdr.bridge.pass_type, md);
        AA0f.apply(hdr.payload.data1e, hdr.payload.data1f, hdr.bridge.pass_type, md);
        AA10.apply(hdr.payload.data20, hdr.payload.data21, hdr.bridge.pass_type, md);
        AA11.apply(hdr.payload.data22, hdr.payload.data23, hdr.bridge.pass_type, md);
        AA12.apply(hdr.payload.data24, hdr.payload.data25, hdr.bridge.pass_type, md);
        AA13.apply(hdr.payload.data26, hdr.payload.data27, hdr.bridge.pass_type, md);
        AA14.apply(hdr.payload.data28, hdr.payload.data29, hdr.bridge.pass_type, md);
        AA15.apply(hdr.payload.data2a, hdr.payload.data2b, hdr.bridge.pass_type, md);
        AA16.apply(hdr.payload.data2c, hdr.payload.data2d, hdr.bridge.pass_type, md);
        AA17.apply(hdr.payload.data2e, hdr.payload.data2f, hdr.bridge.pass_type, md);
        AA18.apply(hdr.payload.data30, hdr.payload.data31, hdr.bridge.pass_type, md);
        AA19.apply(hdr.payload.data32, hdr.payload.data33, hdr.bridge.pass_type, md);
        AA1a.apply(hdr.payload.data34, hdr.payload.data35, hdr.bridge.pass_type, md);
        AA1b.apply(hdr.payload.data36, hdr.payload.data37, hdr.bridge.pass_type, md);
        AA1c.apply(hdr.payload.data38, hdr.payload.data39, hdr.bridge.pass_type, md);
        AA1d.apply(hdr.payload.data3a, hdr.payload.data3b, hdr.bridge.pass_type, md);
        AA1e.apply(hdr.payload.data3c, hdr.payload.data3d, hdr.bridge.pass_type, md);
        AA1f.apply(hdr.payload.data3e, hdr.payload.data3f, hdr.bridge.pass_type, md);
    }
}

#endif