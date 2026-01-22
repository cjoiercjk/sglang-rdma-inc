import json 
import sys
import os
from concurrent import futures
import logging
import warnings
import signal

import grpc # 这里有个BUG，只有第一个import grpc的bfrt能成功，后续的bfrt跑到这里都会报错

# grpc_file_path = os.path.abspath(os.path.join(os.path.dirname(sys.argv[0]), "..", "build", "common"))

# print(grpc_file_path)

# sys.path.append(grpc_file_path)

import allreduce_pb2, allreduce_pb2_grpc

class INCServicerImpl(allreduce_pb2_grpc.INCServicer):
    def __init__(self):
        super().__init__()

    def CreateGroup(self, request, context):
        return allreduce_pb2.CreateGroupReply(GroupID=123)
    
    def DestroyGroup(self, request, context):
        return allreduce_pb2.DestroyGroupReply()

port = "50051"
server = grpc.server(futures.ThreadPoolExecutor(max_workers=1))
allreduce_pb2_grpc.add_INCServicer_to_server(INCServicerImpl(), server)
server.add_insecure_port("[::]:" + port)

def handle_signal(signum, frame):
    print("Received Ctrl+C, shutting down gracefully...")
    server.stop(grace=5)  # 设置一个宽限期，确保在终止之前完成清理操作
    print("Server stopped.")
    exit(0)

server.start()
print("Server started, listening on " + port)
server.wait_for_termination()