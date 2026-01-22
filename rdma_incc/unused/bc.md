+ 在先前的工作中，RDMA组播需要对每种拓扑建立一个组播组。
组播组之所以不能被不同的发送-接收拓扑复用，是因为它依赖序列号PSN和消息号MSN同步进行ACK聚合。
序列号同步要求包组播时保持序列号不变，从而使每个接收方的QP保持序列号同步。
切换组播拓扑会引入持有不同序列号的QP，破坏同步性。
这种设计使得，对于n个通信方，广播组数量可达O(n\*2^n)
由于每个组播组都会占用交换机上的一定资源，因此这种方案可拓展性差。
+ 同时，对于RDMA_WRITE，它的解决方案是将内存地址和rkey安装到交换机规则中，在组播包转发时读取。
这使得每一次内存地址不同的RDMA_WRITE都要与控制器交互，引入较高的延迟。
因此，这种方案不适用于延迟敏感的短消息。
+ 我们的设计解决了上面的问题，并做出了以下贡献：
    + 交换机维护psn, 实现了组播拓扑切换，同时无需重建连接, scalable，广播组数量可为O(1)
    + 不依赖交换机的multicast功能，而是使用多轮mirror实现数据平面的组播，后者不需要控制平面的参与。
    + 而是通过数据平面更新组播组拓扑/write地址等信息，低延迟
    + 一级支持至少32个接收方，scalable and fast


+ 通过DIP区分正常流量/组播流量
+ 将服务器上的QP分为SQP（发送）和RQP（接收），交换机上有VSQP和VRQP
    + s * SQP -> VRQP，一个VRQP可以被多个SQP共享，时分复用
    + VRQP -> n * VSQP，这个映射可以运行时通过数据平面修改
    + VSQP -> RQP，VSQP为RQP独占，一对一映射

+ 建立初始集合可以用控制平面，但是其子集进行bcast不需要
+ 一个初始集合内部最多同时进行一个bcast，其他的需要等待（其实allreduce也有相同的限制，比如sharp）

+ shared connection (时分复用) -> runtime mapping (由数据平面更新) -> plain connection (固定的连接)

+ shared connection和runtime mapping都会由数据平面更新，这部分其实可以称为“发起bc连接”

+ SEND中存在message level flow control，我们要么在交换机上将ACK credit设为0b11111以关闭此功能，要么配置RQ(没找到方法)

+ 发送方通过额外的一次rdma_write修改对应的registers，包括
    + （对于超过epsn的包，回复NAK）
    + （维护一个md.epsn_match）
    + 对应VRQP的信息：
        + bc_num (ingress)
        + vsqpn[0..n-1] 
        + sip (acquire)
        + sqpn (acquire)
        + bitmap_mask
    + 对应每个VSQP的信息：mirror轮询
        // + vrqpn (vrqpn == vsqpn & 0xffff00)
        // + rank (rank == vsqpn & 0x0000ff)
        + dip (fixed)
        + dqpn (fixed)
        + mem_addr, rkey
        + psn_in (data pkt: update on increase; control pkt: overwrite)
        + psn_out (data pkt: update on increase)
        + msn_in (data pkt: update on psn_in increase & last; control pkt: overwrite)
        + msn_out (data pkt: update on psn_in increase & last;)

        control packet format: [psn, msn, n_link, lockid
            [vsqpn[0], ..., vsqpn[n_vsqp-1]]]
        一个组播组只应该有1个vrqp，和n个vsqp，以及1个锁
        流程：获取锁-|->([control]-|->write/send)*n
        除开第一个control，中间这一段psn_offset是不会变的
        仅当VSQP和VRQP的version匹配时，ACK可以通过
        ACK通过vsqpn[vrqpn][rank] == vrqpn && rank < n_vsqp

    control:
        if locked == 0:
            locked = 1
            md.src_update = 1

        if md.src_update == 1:
            sip = pkt.sip
            sqpn = pkt.sqpn
            md.src_match = 1
        else if sip == pkt.sip && sqpn == pkt.sqpn:
            md.src_match = 1
        
        if md.src_match: 
            goto loop

        loop:
        if pkt.n_unlink:   

            pkt.n_unlink --;
        else:

            if pkt.n_link == 1:
                drop
            else:
                pkt.n_link --;
        goto loop

    forward ->
    backward ->
