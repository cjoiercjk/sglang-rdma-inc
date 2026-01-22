#include <thread>
#include <grpcpp/grpcpp.h>
#include <bits/stdc++.h>
#include "allreduce.grpc.pb.h"

using namespace inc;

int main(int argc, char **argv)
{
    // inc::INC
    INC::Stub stub(*INC::NewStub(grpc::CreateChannel(argv[1], grpc::InsecureChannelCredentials())));
    grpc::ClientContext ctx;
    CreateGroupRequest req;
    CreateGroupReply rep;

    srand(time(0));

    MemberInfo *mem;
    mem = req.add_member();
    mem->set_ip(0xC0A80001);
    mem->set_qpn((rand()&0xffffff)/100*100+1);
    mem = req.add_member();
    mem->set_ip(0xC0A80002);
    mem->set_qpn((rand()&0xffffff)/100*100+2);
    
    grpc::Status status = stub.CreateGroup(&ctx, req, &rep);
    if(status.ok()) {
        printf("OK\n");
        printf("%d", rep.groupid());
    }
    else {
        printf("Error\n");
    }
    return 0;
}