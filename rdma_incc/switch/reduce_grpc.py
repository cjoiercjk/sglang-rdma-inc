def NameSpace(): # run all the code in a function, because this file is executed through exec() which will cause some namespace problems
    import json 
    import sys
    import os
    from concurrent import futures
    import logging
    import warnings
    import signal

    import grpc # 这里有个BUG，只有第一个import grpc的bfrt能成功，后续的bfrt跑到这里都会报错

    grpc_file_path = os.path.abspath(os.path.join(os.path.dirname(sys.argv[0]), "..", "build", "common"))

    print(grpc_file_path)

    sys.path.append(grpc_file_path)

    import allreduce_pb2, allreduce_pb2_grpc

    program_name = bfrt.p4_programs_list[0]
    program = eval('bfrt.' + program_name)

    config_dir = "../common"
    with open(config_dir + "/topo.json") as f: # symbolic link
        topo = json.load(f)

    port = topo["port"]
    MAC = topo["MAC"]
    IP = topo["IP"]
    switch_MAC = topo["switch_MAC"]
    switch_IP = topo["switch_IP"]
    recir_port = topo["recir_port"] # can add more

    ADDR_BW = 14 # 16K
    MTU_BW = 8 # 256
    MTU_SIZE = 1<<MTU_BW # 256
    SWITCH_MEM_SIZE = 1<<(ADDR_BW+MTU_BW) # 4MB

    MAX_GROUP_NUM = 256 # min(SWITCH_MEM_SIZE//per_qp_switch_win_size, 256)
    MAX_SERVER_NUM = 256
    MAX_QP_NUM = 256

    ip2port_dict = {int(ip,0):port[ips_idx] for ips_idx, ips in enumerate(IP) for ip in (ips if isinstance(ips, list) else [ips])} # ip is a string

    def ip2port(ip):
        port = ip2port_dict.get(ip)
        if port is None:
            raise Exception("port not found for the given IP")
        return port
        
    def ispower2(x):
        return x > 0 and (x & (x-1)) == 0

    def upperalign(x, y):
        return (x + y - 1)//y*y

    def addr_in_seg(seg, size): 
        # assert size % MTU == 2**k
        addr = upperalign(seg[0], size)
        if addr + size <= seg[1]: 
            return addr
        return None

    class INCServicerImpl(allreduce_pb2_grpc.INCServicer):
        def __init__(self):
            super().__init__()
            self.free_group = [i for i in range(MAX_GROUP_NUM) if i > 0]
            self.free_node = [i for i in range(MAX_QP_NUM) if i > 0]
            self.recir_port_cnt = [0 for _ in range(len(recir_port))]
            self.undo = {}
            self.memseg = [[0, SWITCH_MEM_SIZE]] # [[ptr_l, ptr_r], ...]
            self.program = program

        def debug_exec(self, str):
            print(str)
            return exec(str)

        def select_recir_port_idx(self):
            return self.recir_port_cnt.index(min(self.recir_port_cnt))

        def fit_seg(self, size):
            if size % MTU_SIZE != 0 or not ispower2(size//MTU_SIZE):
                print("Invalid size")
                return 
            for index, seg in enumerate(self.memseg):
                addr = addr_in_seg(seg, size)
                if addr != None:
                    return (index, addr)
            return (None, None)

        def alloc_seg(self, index, addr_l, addr_r):
            seg = self.memseg[index]
            lseg = None
            rseg = None
            if seg[0] != addr_l:
                lseg = [seg[0], addr_l]
            if seg[1] != addr_r:
                rseg = [addr_r, seg[1]]
            del self.memseg[index]
            if rseg:
                self.memseg.insert(index, rseg)
            if lseg:
                self.memseg.insert(index, lseg)
            
        def free_seg(self, addr_l, addr_r):
            r_index = len(self.memseg)
            for index, seg in enumerate(self.memseg):
                if seg[1] <= addr_l:
                    continue
                if seg[0] < addr_r:
                    print("Invalid free")
                    exit(1)
                r_index = index
                break
            l_index = r_index - 1
            index = r_index
            if l_index >= 0 and self.memseg[l_index][1] == addr_l:
                addr_l = self.memseg[l_index][0]
                index = l_index
                del self.memseg[l_index]
            if r_index < len(self.memseg) and self.memseg[r_index][0] == addr_r:
                addr_r = self.memseg[r_index][1]
                del self.memseg[r_index]
            self.memseg.insert(index, [addr_l, addr_r])

        def CreateGroup(self, request, context):
            group_size = len(request.Member)
            seg_size = request.MemorySize
            seg_index, seg_addr = self.fit_seg(request.MemorySize)
            if len(self.free_group) < 1 or len(self.free_node) < group_size or seg_index == None:
                print("No resource")
                return allreduce_pb2.CreateGroupReply() # GroupID == 0 (NULL group)

            agg_addr = seg_addr // MTU_SIZE
            agg_addr_len = seg_size // MTU_SIZE

            undo_funcs = []

            self.debug_exec(f"self.alloc_seg({seg_index}, addr_l={seg_addr}, addr_r={seg_addr + seg_size})")
            undo_funcs.append(f"self.free_seg(addr_l={seg_addr}, addr_r={seg_addr + seg_size})")

            groupid = self.free_group[0]
            self.debug_exec(f"self.free_group = self.free_group[1:]")
            undo_funcs.append(f"self.free_group.append({groupid})")
            

            node = self.free_node[0:group_size]
            self.debug_exec(f"self.free_node = self.free_node[{group_size}:]")
            undo_funcs.append(f"self.free_node += {node}")
            

            recir_port_idx = self.select_recir_port_idx()
            self.debug_exec(f"self.recir_port_cnt[{recir_port_idx}] += 1")
            undo_funcs.append(f"self.recir_port_cnt[{recir_port_idx}] -= 1")

            self.debug_exec(f"self.program.pipe.Ingress.sendout.recirculate_table.add_with_forward({groupid}, {recir_port[recir_port_idx]})")
            undo_funcs.append(f"self.program.pipe.Ingress.sendout.recirculate_table.delete({groupid})")

            self.debug_exec(f"self.program.pipe.Ingress.sendout.root_unicast_table.add_with_forward({groupid}, {ip2port(request.Member[request.RootRank].IP)})")
            undo_funcs.append(f"self.program.pipe.Ingress.sendout.root_unicast_table.delete({groupid})")

            for i in range(group_size):
                self.debug_exec(f"self.program.pipe.Ingress.metadata_table.add_with_get_metadata\
    (sip={request.Member[i].IP:#x}, dip={switch_IP}, dqpn={request.Member[i].QPN}, group_id={groupid}, src_rank={i}, root_rank={request.RootRank}, \
    bitmap={1<<i:#x}, bitmap_mask={((1<<group_size)-1)^(1<<request.RootRank):#x}, agg_addr={agg_addr:#x}, \
    agg_addr_offset_mask={agg_addr_len-1:#x})")
                undo_funcs.append(f"self.program.pipe.Ingress.metadata_table.delete\
    (sip={request.Member[i].IP:#x}, dip={switch_IP}, dqpn={request.Member[i].QPN})")

                self.debug_exec(f"self.program.pipe.Egress.restore_table.add_with_restore_fields_with_reth\
    (group_id={groupid}, dst_rank={i}, valid={1}, sip={switch_IP}, dip={request.Member[i].IP:#x}, dqpn={request.Member[i].QPN:#x}, rkey={request.Member[i].RKEY:#x})")
                self.debug_exec(f"self.program.pipe.Egress.restore_table.add_with_restore_fields\
    (group_id={groupid}, dst_rank={i}, valid={0}, sip={switch_IP}, dip={request.Member[i].IP:#x}, dqpn={request.Member[i].QPN:#x})")
                undo_funcs.append(f"self.program.pipe.Egress.restore_table.delete\
    (group_id={groupid}, dst_rank={i}, valid={1})")
                undo_funcs.append(f"self.program.pipe.Egress.restore_table.delete\
    (group_id={groupid}, dst_rank={i}, valid={0})")

                if i != request.RootRank:
                    self.debug_exec(f"bfrt.pre.node.add({node[i]}, {i}, None, [{ip2port(request.Member[i].IP)}])")
                    undo_funcs.append(f"bfrt.pre.node.delete({node[i]})")

            self.debug_exec(f"bfrt.pre.mgid.add({groupid}, {node[0:request.RootRank]+node[request.RootRank+1:]}, {[False]*(group_size-1)}, {[0]*(group_size-1)})")
            undo_funcs.append(f"bfrt.pre.mgid.delete({groupid})")

            self.debug_exec(f"for i in range({agg_addr}, {agg_addr + agg_addr_len}):\n    self.program.pipe.Ingress.reg_bitmap.mod(i, 0, 0)")

            self.undo[groupid] = undo_funcs

            ret = allreduce_pb2.CreateGroupReply()
            ret.GroupID = groupid
            for member in request.Member:
                # int(X, base=0) : determine base by the prefix 
                ret.Member.append(allreduce_pb2.MemberInfo(IP=int(switch_IP, 0), QPN=member.QPN, RKEY=member.RKEY)) # VQP uses the same QPN of QP
            return ret
        
        def DestroyGroup(self, request, context):
            groupid = request.GroupID
            if not groupid in self.undo:
                return allreduce_pb2.DestroyGroupReply()
            print(f"Destroy group {groupid}")
            for func in self.undo[groupid][::-1]: # reverse
                self.debug_exec(func)
            del self.undo[groupid]
            return allreduce_pb2.DestroyGroupReply()

    def serve(): # should import grpc here
        port = "50051"
        server = grpc.server(futures.ThreadPoolExecutor(max_workers=1))
        allreduce_pb2_grpc.add_INCServicer_to_server(INCServicerImpl(), server)
        server.add_insecure_port("[::]:" + port)

        def handle_signal(signum, frame):
            print("Received Ctrl+C, shutting down gracefully...")
            server.stop(grace=5)  # 设置一个宽限期，确保在终止之前完成清理操作
            print("Server stopped.")
            exit(0)
        
        signal.signal(signal.SIGINT, handle_signal)
        signal.signal(signal.SIGTERM, handle_signal)

        server.start()
        print("Server started, listening on " + port)
        server.wait_for_termination()

    logging.basicConfig()
    serve()

NameSpace()

# unique_ptr<int[]>group_id(new int[qp_num]);

#         for(int qp_idx = 0; qp_idx < qp_num; qp_idx++) {
#             group_id[qp_idx] = rand() % MAX_GROUP_NUM;

#             printf("self.program.pipe.Ingress.sendout.recirculate_table.add_with_forward(%d, %d)\n", 
#                 group_id[qp_idx], recir_port);

#             for(int i = 0; i < group_size; i++) {
#                 printf("self.program.pipe.Ingress.metadata_table.add_with_get_allreduce_metadata\\\n\
#     (sip=%#x, dip=%#x, dqpn=%#x, group_id=%d, src_rank=%d, bitmap=%#x, bitmap_mask=%#x, agg_addr=%#x, agg_addr_offset_mask=%#x)\n",
#                 addr_list[i].ip, switch_addr.ip, addr_list[i].qpn[qp_idx], group_id[qp_idx], i, 1<<i, (1<<group_size)-1, 
#                 qp_idx*per_qp_switch_win_size/MTU_SIZE, per_qp_switch_win_size/MTU_SIZE-1);
#     //             printf("self.program.pipe.Ingress.metadata_table.add_with_get_allreduce_backward_metadata\\\n\
#     // (sip=%#x, dip=%#x, dqpn=%#x, is_forward=0)\n", addr_list[i].ip, switch_addr.ip, addr_list[i].qpn[qp_idx]);
#                 printf("self.program.pipe.Egress.restore_table.add_with_restore_fields\\\n\
#     (group_id=%d, src_rank=%d, sip=%#x, dip=%#x, dqpn=%#x)\n",
#                 group_id[qp_idx], i, switch_addr.ip, addr_list[i].ip, addr_list[i].qpn[qp_idx]);
#             }
            
#             unique_ptr<int[]>node_id(new int[group_size]);
#             string node_ids;
#             map<uint32_t, int>p4_port;
#             for(auto i = 0; i < topo_json["port"].size(); i++) 
#                 p4_port[(uint32_t)stoul(topo_json["IP"][i].asString(), NULL, 16)] = (int)stoi(topo_json["port"][i].asString());
                

#             printf("# NOTE: rid == src_rank in allreduce\n");

#             for(int i = 0; i < group_size; i++) {
#                 node_id[i] = rand();
#                 printf("bfrt.pre.node.add(%d, %d, None, [%d])\n", node_id[i], i, p4_port[addr_list[i].ip]);// go back to sender
#                 if(i) node_ids += ", ";
#                 node_ids += to_string(node_id[i]);
#             }
#             printf("bfrt.pre.mgid.add(%d, [%s], [False]*%d, [0]*%d)\n",
#                 group_id[qp_idx], node_ids.c_str(), group_size, group_size);
#         }
#         printf("self.program.pipe.Ingress.reg_bitmap.clear()\n");