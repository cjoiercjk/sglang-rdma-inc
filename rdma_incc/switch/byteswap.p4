#ifndef __BYTESWAP_P4__
#define __BYTESWAP_P4__

#include "custom_header.p4"

control ByteSwap(
        inout int<32> word) {


    bit<32> x;
    bit<32> y;

    action action1()
    {
        x = x & 0x00ff00ff;
        y = y & 0xff00ff00;
    }

    apply {
        // word = (int<32>)(word[7:0] ++ word[15:8] ++ word[23:16] ++ word[31:24]);
        // funnel_shift_right(word, word, word, 16);
        // x = (bit<32>)word >> 8;
        // y = (bit<32>)word << 8;
        // action1();
        // word = (int<32>)(x ^ y);
    }
}

control ByteSwapAll(
        inout headers hdr) {

    ByteSwap() BS00;
    ByteSwap() BS01;
    ByteSwap() BS02;
    ByteSwap() BS03;
    ByteSwap() BS04;
    ByteSwap() BS05;
    ByteSwap() BS06;
    ByteSwap() BS07;
    ByteSwap() BS08;
    ByteSwap() BS09;
    ByteSwap() BS0a;
    ByteSwap() BS0b;
    ByteSwap() BS0c;
    ByteSwap() BS0d;
    ByteSwap() BS0e;
    ByteSwap() BS0f;
    ByteSwap() BS10;
    ByteSwap() BS11;
    ByteSwap() BS12;
    ByteSwap() BS13;
    ByteSwap() BS14;
    ByteSwap() BS15;
    ByteSwap() BS16;
    ByteSwap() BS17;
    ByteSwap() BS18;
    ByteSwap() BS19;
    ByteSwap() BS1a;
    ByteSwap() BS1b;
    ByteSwap() BS1c;
    ByteSwap() BS1d;
    ByteSwap() BS1e;
    ByteSwap() BS1f;
    ByteSwap() BS20;
    ByteSwap() BS21;
    ByteSwap() BS22;
    ByteSwap() BS23;
    ByteSwap() BS24;
    ByteSwap() BS25;
    ByteSwap() BS26;
    ByteSwap() BS27;
    ByteSwap() BS28;
    ByteSwap() BS29;
    ByteSwap() BS2a;
    ByteSwap() BS2b;
    ByteSwap() BS2c;
    ByteSwap() BS2d;
    ByteSwap() BS2e;
    ByteSwap() BS2f;
    ByteSwap() BS30;
    ByteSwap() BS31;
    ByteSwap() BS32;
    ByteSwap() BS33;
    ByteSwap() BS34;
    ByteSwap() BS35;
    ByteSwap() BS36;
    ByteSwap() BS37;
    ByteSwap() BS38;
    ByteSwap() BS39;
    ByteSwap() BS3a;
    ByteSwap() BS3b;
    ByteSwap() BS3c;
    ByteSwap() BS3d;
    ByteSwap() BS3e;
    ByteSwap() BS3f;

    apply {
        BS00.apply(hdr.payload.data00);
        BS01.apply(hdr.payload.data01);
        BS02.apply(hdr.payload.data02);
        BS03.apply(hdr.payload.data03);
        BS04.apply(hdr.payload.data04);
        BS05.apply(hdr.payload.data05);
        BS06.apply(hdr.payload.data06);
        BS07.apply(hdr.payload.data07);
        BS08.apply(hdr.payload.data08);
        BS09.apply(hdr.payload.data09);
        BS0a.apply(hdr.payload.data0a);
        BS0b.apply(hdr.payload.data0b);
        BS0c.apply(hdr.payload.data0c);
        BS0d.apply(hdr.payload.data0d);
        BS0e.apply(hdr.payload.data0e);
        BS0f.apply(hdr.payload.data0f);
        BS10.apply(hdr.payload.data10);
        BS11.apply(hdr.payload.data11);
        BS12.apply(hdr.payload.data12);
        BS13.apply(hdr.payload.data13);
        BS14.apply(hdr.payload.data14);
        BS15.apply(hdr.payload.data15);
        BS16.apply(hdr.payload.data16);
        BS17.apply(hdr.payload.data17);
        BS18.apply(hdr.payload.data18);
        BS19.apply(hdr.payload.data19);
        BS1a.apply(hdr.payload.data1a);
        BS1b.apply(hdr.payload.data1b);
        BS1c.apply(hdr.payload.data1c);
        BS1d.apply(hdr.payload.data1d);
        BS1e.apply(hdr.payload.data1e);
        BS1f.apply(hdr.payload.data1f);
        BS20.apply(hdr.payload.data20);
        BS21.apply(hdr.payload.data21);
        BS22.apply(hdr.payload.data22);
        BS23.apply(hdr.payload.data23);
        BS24.apply(hdr.payload.data24);
        BS25.apply(hdr.payload.data25);
        BS26.apply(hdr.payload.data26);
        BS27.apply(hdr.payload.data27);
        BS28.apply(hdr.payload.data28);
        BS29.apply(hdr.payload.data29);
        BS2a.apply(hdr.payload.data2a);
        BS2b.apply(hdr.payload.data2b);
        BS2c.apply(hdr.payload.data2c);
        BS2d.apply(hdr.payload.data2d);
        BS2e.apply(hdr.payload.data2e);
        BS2f.apply(hdr.payload.data2f);
        BS30.apply(hdr.payload.data30);
        BS31.apply(hdr.payload.data31);
        BS32.apply(hdr.payload.data32);
        BS33.apply(hdr.payload.data33);
        BS34.apply(hdr.payload.data34);
        BS35.apply(hdr.payload.data35);
        BS36.apply(hdr.payload.data36);
        BS37.apply(hdr.payload.data37);
        BS38.apply(hdr.payload.data38);
        BS39.apply(hdr.payload.data39);
        BS3a.apply(hdr.payload.data3a);
        BS3b.apply(hdr.payload.data3b);
        BS3c.apply(hdr.payload.data3c);
        BS3d.apply(hdr.payload.data3d);
        BS3e.apply(hdr.payload.data3e);
        BS3f.apply(hdr.payload.data3f);
    }
}

#endif