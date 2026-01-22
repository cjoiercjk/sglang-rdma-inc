#!/bin/bash
set -x
# refer to README
./run.sh rdma_allreduce ./allreduce_init.py
$SDE/run_bfshell.sh -b `pwd`/allreduce_grpc.py