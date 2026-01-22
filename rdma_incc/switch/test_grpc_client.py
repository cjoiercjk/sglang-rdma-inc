# client.py
import sys
import grpc
import allreduce_pb2
import allreduce_pb2_grpc

def main():
    if len(sys.argv) != 2:
        print("Usage: python client.py <server_address:port>")
        sys.exit(1)

    server_addr = sys.argv[1]
    print(">> client will connect to:", server_addr)
    # 1) 建立不安全的 channel
    channel = grpc.insecure_channel(server_addr, options=[('grpc.enable_http_proxy', 0)])

    # 2) 等待 channel 就绪（可选），超时 3s
    try:
        grpc.channel_ready_future(channel).result(timeout=3)
        print(f"[OK] 已连接到 {server_addr}")
    except grpc.FutureTimeoutError:
        print(f"[ERROR] 无法连接到 {server_addr}")
        sys.exit(1)

    # 3) 创建 stub
    stub = allreduce_pb2_grpc.INCStub(channel)

    # 4) 调用 CreateGroup，参数随便填一点，主要测试 RPC 是否通
    req = allreduce_pb2.CreateGroupRequest(
        MemorySize=1024,
        Member=[
            allreduce_pb2.MemberInfo(IP=0x7F000001, QPN=1234, RKEY=5678)
        ]
    )

    try:
        reply = stub.CreateGroup(req, timeout=5)
        print("[OK] CreateGroup 调用成功，返回：", reply)
    except grpc.RpcError as err:
        print("[ERROR] CreateGroup RPC 失败：", err)

    # 5) 也可以再测试一次 DestroyGroup
    try:
        dest_req = allreduce_pb2.DestroyGroupRequest(GroupID=reply.GroupID if 'reply' in locals() else 0)
        dest_reply = stub.DestroyGroup(dest_req, timeout=5)
        print("[OK] DestroyGroup 调用成功")
    except grpc.RpcError as err:
        print("[ERROR] DestroyGroup RPC 失败：", err)

    channel.close()

if __name__ == "__main__":
    main()
