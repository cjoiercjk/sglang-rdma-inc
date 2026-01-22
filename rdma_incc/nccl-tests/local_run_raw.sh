#!/usr/bin/bash

NCCL_IB_HCA=mlx5_1 CUDA_VISIBLE_DEVICES=1,0 ./build/all_reduce_perf -t 1 -g 1 -b 8 -e 2000000000 -f 2 