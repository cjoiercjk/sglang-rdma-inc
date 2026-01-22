# =====================================================================
# ./server/CMakeLists.txt
# =====================================================================

# 项目名称
project(RINCC_server)

# 1. 设置 RDMA 接口库
add_library(RDMA_IF INTERFACE)
find_package(PkgConfig)
if(PkgConfig_FOUND)
    pkg_search_module(RDMA libibverbs librdmacm IMPORTED_TARGET)
endif()

if(PkgConfig_FOUND AND TARGET PkgConfig::RDMA)
    message("PC files found")
    target_link_libraries(RDMA_IF INTERFACE PkgConfig::RDMA)
else()
    message("PC files NOT found")
    target_link_libraries(RDMA_IF INTERFACE ibverbs INTERFACE rdmacm)
endif()
target_link_libraries(RDMA_IF INTERFACE pthread)

# 2. 基础依赖：jsoncpp, CUDA, NCCL
add_library(jsoncpp dep/jsoncpp/jsoncpp.cpp)

find_package(CUDAToolkit REQUIRED)

# 查找 NCCL
find_path(NCCL_INCLUDE_DIR nccl.h
    HINTS $ENV{NCCL_ROOT}/include $ENV{CUDA_HOME}/include /usr/local/cuda/include /usr/include)

find_library(NCCL_LIBRARY nccl
    HINTS $ENV{NCCL_ROOT}/lib $ENV{NCCL_ROOT}/lib64 $ENV{CUDA_HOME}/lib64 /usr/local/cuda/lib64 /usr/lib/x86_64-linux-gnu)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(NCCL DEFAULT_MSG NCCL_INCLUDE_DIR NCCL_LIBRARY)

if(NCCL_FOUND AND NOT TARGET NCCL::NCCL)
    add_library(NCCL::NCCL UNKNOWN IMPORTED)
    set_target_properties(NCCL::NCCL PROPERTIES
        IMPORTED_LOCATION "${NCCL_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${NCCL_INCLUDE_DIR}")
endif()

# =====================================================================
# 3. 新增：编译动态库 (rdma_group.so)
# =====================================================================
# 库包含 RDMAGroup 实现以及其依赖的底层 RDMA 操作
add_library(rdma_group SHARED 
    rdma_group.cpp 
    rdma.cpp
)

# 链接依赖：
# RDMA_IF: 提供 verbs 支持
# grpc_cpp: 提供 common 目录下生成的 RPC 通信能力
target_link_libraries(rdma_group 
    PRIVATE 
    RDMA_IF 
    grpc_cpp
)

add_executable(test_rdma_group test_rdma_group.cpp)
target_link_libraries(test_rdma_group rdma_group CUDA::cudart)

# =====================================================================
# 4. 保留：编译 benchmark 可执行文件
# =====================================================================
set(targets main) 

foreach(target ${targets})
    add_executable(${target} main.cpp rdma.cpp nccl.cpp)
    # 链接 benchmark 所需的所有组件
    target_link_libraries(${target} 
        jsoncpp 
        RDMA_IF 
        grpc_cpp 
        CUDA::cudart 
        NCCL::NCCL
    )
endforeach()
