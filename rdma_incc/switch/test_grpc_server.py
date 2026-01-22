# server.py
import time
from concurrent import futures

import grpc
import allreduce_pb2
import allreduce_pb2_grpc

class INCServicer(allreduce_pb2_grpc.INCServicer):
    def CreateGroup(self, request, context):
        print("Received CreateGroupRequest:")
        print(request)
        # 随便返回一个 GroupID=42，并把原来的 Member 列表回传
        reply = allreduce_pb2.CreateGroupReply(
            GroupID=42,
            Member=request.Member
        )
        return reply

    def DestroyGroup(self, request, context):
        print("Received DestroyGroupRequest:")
        print(request)
        # DestroyGroupReply 是空的
        return allreduce_pb2.DestroyGroupReply()

def serve():
    server = grpc.server(futures.ThreadPoolExecutor(max_workers=1))
    allreduce_pb2_grpc.add_INCServicer_to_server(INCServicer(), server)
    listen_addr = '[::]:50051'
    server.add_insecure_port(listen_addr)
    server.start()
    print(f"gRPC server started, listening on {listen_addr}")
    try:
        while True:
            time.sleep(60*60*24)
    except KeyboardInterrupt:
        print("Shutting down server")
        server.stop(0)

if __name__ == '__main__':
    serve()
