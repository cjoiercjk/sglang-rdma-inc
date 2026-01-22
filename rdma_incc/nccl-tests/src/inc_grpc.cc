#include <string>
#include <grpcpp/grpcpp.h>
#include "allreduce.grpc.pb.h"

using std::string;

std::unique_ptr<inc::INC::Stub> stub;

uint32_t inc_create_group(uint32_t *ip, int *qpn, uint32_t *rkey, int qp_count, int switch_memory_size, string controller_addr) 
{
    if(controller_addr.empty()) {
        printf("No controller_addr given\n");
    }
    stub = std::move(inc::INC::NewStub(grpc::CreateChannel(controller_addr, grpc::InsecureChannelCredentials())));
    // inc::INC
    grpc::ClientContext ctx;
    inc::CreateGroupRequest req;
    inc::CreateGroupReply rep;

    for(int i = 0; i < qp_count; i++) {
        inc::MemberInfo *mem = req.add_member();
        mem->set_ip(ip[i]);
        mem->set_qpn(qpn[i]);
        mem->set_rkey(rkey[i]);
    }
    req.set_memorysize(switch_memory_size);
    grpc::Status status = stub->CreateGroup(&ctx, req, &rep);
    if(!status.ok()) {
        fprintf(stderr, "CreateGroup Error: %d\n", status.error_code());
    }
    assert(rep.member_size() == qp_count);
    for(int i = 0; i < qp_count; i++) {
        ip[i] = rep.member(i).ip();
        qpn[i] = rep.member(i).qpn();
        rkey[i] = rep.member(i).rkey();
    }
    return rep.groupid();
}

// return true on success
bool inc_destroy_group(uint32_t group_id) 
{
    grpc::ClientContext ctx;
    inc::DestroyGroupRequest req;
    inc::DestroyGroupReply rep;

    req.set_groupid(group_id);
    grpc::Status status = stub->DestroyGroup(&ctx, req, &rep);
    return true;
}