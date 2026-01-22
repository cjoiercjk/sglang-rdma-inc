def NameSpace():
    import json 
    import sys
    import os

    program_name = bfrt.p4_programs_list[0]
    program = eval('bfrt.' + program_name)

    # NOTE: In bfrt, argv[0]=="/"
    config_dir = "../common"
    with open(config_dir + "/topo.json") as f: # symbolic link
        topo = json.load(f)

    port = topo["port"]
    MAC = topo["MAC"]
    IP = topo["IP"]
    FEC = topo.get("FEC")
    switch_MAC = topo["switch_MAC"]
    switch_IP = topo["switch_IP"]
    recir_port = topo["recir_port"] # can add more

    # unicast
    for worker in range(len(port)):
        macs = MAC[worker]
        if isinstance(macs, str):
            macs = [macs]
        for mac in macs:
            program.pipe.Ingress.sendout.unicast_table.add_with_forward(mac, port[worker])

    # program.pipe.Ingress.sendout.recirculate_table.add_with_forward(1, 172)

    # port metadata
    for worker in range(len(port)):
        program.pipe.IngressParser.PORT_METADATA.add(port[worker], 0)
    for p in recir_port:
        program.pipe.IngressParser.PORT_METADATA.add(p, 1) # may use ether_type instead

    # bfrt.pre.node.add(123, 123, None, [port[2]]) # for debug, send to worker3
    # bfrt.pre.mgid.add(2, [123], [False], [0])

    # restore mac
    for worker in range(len(port)):
        ips = IP[worker]
        macs = MAC[worker]
        if isinstance(macs, str):
            ips = [ips]
            macs = [macs]
        for ip, mac in zip(ips, macs):
            program.pipe.Egress.restore_dmac_table.add_with_retore_dmac(ip, mac)    

    program.pipe.Egress.restore_smac_table.add_with_retore_smac(switch_IP, switch_MAC)

    # dcqcn
    program.pipe.Egress.dcqcn.wred.add(0, 0, 125, 2500, 0.01)
    # 0 ~ 10KB, 0 
    # 10 ~ 200KB, 0 ~ 0.01
    # 200KB ~, 1

    for port_idx, p in enumerate(port):
        if int(p) != 192: # pcie port
            # bfrt.port.port.add(p, 'BF_SPEED_100G', 'BF_FEC_TYP_RS', 4, True, 'PM_AN_FORCE_DISABLE')
            fec_type = FEC[port_idx] if FEC else "BF_FEC_TYP_NONE"
            bfrt.port.port.add(p, 'BF_SPEED_100G', fec_type, 4, True, 'PM_AN_FORCE_DISABLE')
    #        TX_PAUSE_FRAME_EN=1, RX_PAUSE_FRAME_EN=1, TX_PFC_EN_MAP=0xff, RX_PFC_EN_MAP=0xff)
    for p in recir_port:
        bfrt.port.port.add(p, 'BF_SPEED_100G', 'BF_FEC_TYP_NONE', 4, True, 'PM_AN_FORCE_DISABLE', 'BF_LPBK_MAC_NEAR')
    #        TX_PAUSE_FRAME_EN=1, RX_PAUSE_FRAME_EN=1, TX_PFC_EN_MAP=0xff, RX_PFC_EN_MAP=0xff)

    mcast_node_id = 2 ** 32 - 1
    mcast_node_list = []
    for p in port:
        bfrt.pre.node.add(mcast_node_id, 0, None, [p])
        mcast_node_list.append(mcast_node_id)
        mcast_node_id -= 1
    mgid = 2 ** 16 - 1
    bfrt.pre.mgid.add(mgid, mcast_node_list, [True] * len(mcast_node_list), port) 
    program.pipe.Ingress.sendout.unicast_table.add_with_multicast(0xffffffffffff, mgid)

NameSpace()