export CUDA_VISIBLE_DEVICES=0            # 先用第 0 张 GPU
export NCCL_IB_HCA=mlx5_1               # 只让 NCCL 用 mlx5_1 这块 HCA
# 如果 IP 是绑在某个 IPoIB 接口，比如 ib1，就用它
export NCCL_SOCKET_IFNAME=ens10f1np1
export GLOO_SOCKET_IFNAME=ens10f1np1
export NCCL_DEBUG=INFO
export NCCL_DEBUG_SUBSYS=INIT,COLL
export SGLANG_LOG_ALLREDUCE_BACKEND=true