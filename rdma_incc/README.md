A P4 implementation of INC-allreduce.

Supported RDMA OPs: SEND, WRITE

# Prerequisites

We strongly suggest to use our all-in-one NetCCL docker image. It inherits nvcr/python images and contains many preinstalled software, including pytorch, CUDA, NCCL, cuDNN, boost, gRPC, cmake, etc. Note this image does not contain rdma_incc itself.

See https://github.com/YitaoYuan/netccl for image download and usage.

## NVIDIA driver and CUDA library

For `./nccl-tests`. The driver can be co-installed with CUDA. Download CUDA from https://developer.nvidia.com/cuda-toolkit-archive. We prefer to use runfile package instead of deb package.

## OFED driver

Download OFED from https://network.nvidia.com/products/infiniband-drivers/linux/mlnx_ofed/.

## gcc/g++

Requires gcc-8/g++-8 or a newer version.

Check by

```
g++ -v
```

Install by

```
sudo apt install g++-8 # version >= 8 
sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-8 80 --slave /usr/bin/g++ g++ /usr/bin/g++-8
```

## cmake

You need the newest cmake to install gRPC, so you should add the cmake source for apt first. 

```
# E.g., for Ubuntu 18, add in /etc/apt/sources.list
deb [trusted=yes] https://apt.kitware.com/ubuntu/ bionic main
```

Then, run `sudo apt update && sudo apt install -y cmake`.

## GRPC

### On server

build from source for C++ (https://github.com/grpc/grpc/blob/master/BUILDING.md)

```
git clone -b v1.68.2 https://github.com/grpc/grpc --depth 1  # use depth=1 to reduce transmission volumn
cd grpc
git submodule update --init --depth 1  # does not need to be recursive for grpc
```

### On switch

build for python, typically SDE uses python3.8

```
$SDE_INSTALL/bin/python3.8 -m pip install grpcio-tools
```

# Build

## On server

```
mkdir build && cd build
cmake .. 
cmake --build . -- -j $(nproc --all)
# installation not required
```

## On switch

```
mkdir build && cd build
cmake .. -DP4_NAME=rdma_allreduce 
cmake --build . --target install -- -j $(nproc --all)
# only P4 files will be installed
```

# Run

1. Make sure the environment variables are set on the switch

Check by `env | grep SDE`, and set by `source ./set_sde.bash` under the SDE folder.

2. Make sure the switch driver is loaded

Check by `lsmod | grep bf`, and set by `$SDE_INSTALL/bin/bf_kdrv_mod_load $SDE_INSTALL`.

3. Choose config file on the switch, or you may create a new config file

We need 2 config files, "./common/allreduce.json" and "./common/topo.json".
Use the command below can add a symbol link to existing config files,
however, if you are going to run in a new network, you need to create new config files.

```
./common/choose_config.sh <file_tail>
```

4. Turn off icrc checksum on the servers 

```
# only for mlx5
./icrc/disable-icrc.sh <IB-HCA-NAME>
```

5. Set MAC address of switch's virtual IP on servers

```
sudo arp -s <virtual IP> 02:00:00:00:00:01
# Virtual IP should be in the same subnet of the servers' data plane NICs. 
# It must match the one that set in common/allreduce.json
# E.g., sudo arp -s 192.168.0.254 02:00:00:00:00:01
```

6. Boot switch with 

```
cd switch
./run.sh rdma_allreduce ./allreduce_init.py
$SDE/run_bfshell.sh -b `pwd`/allreduce_grpc.py
```

7. Run benchmark on each server. 

See `./build/server/allreduce_benchmark --help`

# Other independent components

These components provide extra tests. Note that the main program allreduce_benchmark does not need these components.

# nccl-tests

`./nccl-tests` is an independent folder with its own compiling routines, see `nccl-tests/README.md` for more info.

# broadcast_benchmark

This program has the same routine of allreduce_benchmark. But it lacks the RPC implementation for INC rule installation. Instead, it print the required rules on the screen and requires the user to manually install the rules.