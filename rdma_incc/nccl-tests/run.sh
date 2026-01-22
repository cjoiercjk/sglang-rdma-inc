#!/usr/bin/bash
set -x

# e.g., `./run.sh ./local_run_inc.sh`

mpirun --allow-run-as-root -c $2 --hostfile ./hostfile --mca pml_ob1_priority 100 --mca btl_tcp_if_include 10.0.0.0/24 $1
# -x UCX_NET_DEVICES=mlx5_1:1