#
# Copyright (c) 2015-2022, NVIDIA CORPORATION. All rights reserved.
#
# See LICENSE.txt for license information
#

CUDA_HOME ?= /usr/local/cuda
PREFIX ?= /usr/local
VERBOSE ?= 0
DEBUG ?= 0

CUDA_LIB ?= $(CUDA_HOME)/lib64
CUDA_INC ?= $(CUDA_HOME)/include
NVCC ?= $(CUDA_HOME)/bin/nvcc
CUDARTLIB ?= cudart

CUDA_VERSION = $(strip $(shell which $(NVCC) >/dev/null && $(NVCC) --version | grep release | sed 's/.*release //' | sed 's/\,.*//'))
CUDA_MAJOR = $(shell echo $(CUDA_VERSION) | cut -d "." -f 1)

# Better define NVCC_GENCODE in your environment to the minimal set
# of archs to reduce compile time.
ifeq ($(shell test "0$(CUDA_MAJOR)" -ge 11; echo $$?),0)
NVCC_GENCODE ?= -gencode=arch=compute_60,code=sm_60 \
                -gencode=arch=compute_61,code=sm_61 \
                -gencode=arch=compute_70,code=sm_70 \
                -gencode=arch=compute_80,code=sm_80 \
                -gencode=arch=compute_80,code=compute_80
else
NVCC_GENCODE ?= -gencode=arch=compute_35,code=sm_35 \
                -gencode=arch=compute_50,code=sm_50 \
                -gencode=arch=compute_60,code=sm_60 \
                -gencode=arch=compute_61,code=sm_61 \
                -gencode=arch=compute_70,code=sm_70 \
                -gencode=arch=compute_70,code=compute_70
endif

NVCUFLAGS  := -ccbin $(CXX) $(NVCC_GENCODE) -std=c++17 # change 11 -> 14
CXXFLAGS   := -std=c++17

LDFLAGS    := -L${CUDA_LIB} -lcudart -lrt
NVLDFLAGS  := -L${CUDA_LIB} -l${CUDARTLIB} -lrt

ifeq ($(DEBUG), 0)
NVCUFLAGS += -O3 -g
CXXFLAGS  += -O3 -g
else
NVCUFLAGS += -O0 -G -g
CXXFLAGS  += -O0 -g -ggdb3
endif

ifneq ($(VERBOSE), 0)
NVCUFLAGS += -Xcompiler -Wall,-Wextra,-Wno-unused-parameter
else
.SILENT:
endif

.PHONY: build clean

BUILDDIR ?= ../build
ifneq ($(NCCL_HOME), "")
NVCUFLAGS += -I$(NCCL_HOME)/include/
NVLDFLAGS += -L$(NCCL_HOME)/lib
endif

ifeq ($(MPI), 1)
NVCUFLAGS += -DMPI_SUPPORT -I$(MPI_HOME)/include
NVLDFLAGS += -L$(MPI_HOME)/lib -L$(MPI_HOME)/lib64 -lmpi
endif
ifeq ($(MPI_IBM),1)
NVCUFLAGS += -DMPI_SUPPORT
NVLDFLAGS += -lmpi_ibm
endif
LIBRARIES += nccl
NVLDFLAGS += $(LIBRARIES:%=-l%)

DST_DIR := $(BUILDDIR)
SRC_FILES := $(wildcard *.cu)
OBJ_FILES := $(SRC_FILES:%.cu=${DST_DIR}/%.o)
BIN_FILES_LIST := all_reduce #all_gather broadcast reduce_scatter reduce alltoall scatter gather sendrecv hypercube
BIN_FILES := $(BIN_FILES_LIST:%=${DST_DIR}/%_perf)

build: check ${BIN_FILES}

clean:
	rm -rf ${DST_DIR}
	rm -f *.o *.pb.cc *.pb.h

check_exit := 0

ifneq ($(MPI),1)
$(info MUST SET MPI=1)
check_exit := 1
endif
ifeq ($(MPI_HOME),)
$(info MUST SET MPI_HOME)
check_exit := 1
endif
check_file_exists := $(wildcard $(MPI_HOME)/include/mpi.h)
ifeq ($(check_file_exists),)
$(info $(MPI_HOME)/include/mpi.h does not exist)
check_exit := 1
endif
ret := $(shell pkg-config --libs grpc++ > /dev/null 2>&1; echo $$?)
ifneq ($(ret),0)
$(info GRPC not found by pkg-config)
check_exit := 1
endif

check:
	@if [ $(check_exit) -eq 1 ]; then \
		echo "No MPI or GRPC support, exit 1"; \
		exit 1; \
	fi
	
	

TEST_VERIFIABLE_SRCDIR := ../verifiable
TEST_VERIFIABLE_BUILDDIR := $(BUILDDIR)/verifiable
include ../verifiable/verifiable.mk

GRPC_SRCS := allreduce.grpc.pb.cc allreduce.pb.cc
GRPC_HDRS := allreduce.grpc.pb.h allreduce.pb.h
GRPC_FILES := ${GRPC_SRCS} ${GRPC_HDRS}

PROTOBUF_ABSL_DEPS := absl_absl_check absl_absl_log absl_algorithm absl_base absl_bind_front absl_bits absl_btree absl_cleanup absl_cord absl_core_headers absl_debugging absl_die_if_null absl_dynamic_annotations absl_flags absl_flat_hash_map absl_flat_hash_set absl_function_ref absl_hash absl_layout absl_log_initialize absl_log_severity absl_memory absl_node_hash_map absl_node_hash_set absl_optional absl_span absl_status absl_statusor absl_strings absl_synchronization absl_time absl_type_traits absl_utility absl_variant
PROTOBUF_UTF8_RANGE_LINK_LIBS := -lutf8_validity

# refer to https://github.com/grpc/grpc/blob/master/examples/cpp/helloworld/Makefile
GRPC_CFLAGS := $(shell pkg-config --cflags protobuf grpc absl_flags absl_flags_parse) 
GRPC_LDFLAGS := -L/usr/local/lib $(shell pkg-config --libs --static protobuf grpc++ absl_flags absl_flags_parse $(PROTOBUF_ABSL_DEPS)) $(PROTOBUF_UTF8_RANGE_LINK_LIBS) -pthread -Xlinker --no-as-needed -lgrpc++_reflection -Xlinker --as-needed -ldl

RDMA_CFLAGS := $(shell pkg-config --cflags libibverbs)
RDMA_LDFLAGS := $(shell pkg-config --libs libibverbs)

EXTRA_CFLAGS := ${RDMA_CFLAGS} ${GRPC_CFLAGS}
EXTRA_LDFLAGS := ${RDMA_LDFLAGS} ${GRPC_LDFLAGS}

# LDFLAGS += -L/usr/local/lib `pkg-config --libs --static protobuf grpc++ absl_flags absl_flags_parse $(PROTOBUF_ABSL_DEPS)`\
#            $(PROTOBUF_UTF8_RANGE_LINK_LIBS) \
#            -pthread\
#            -Wl,--no-as-needed -lgrpc++_reflection -Wl,--as-needed\
#            -ldl

EXTRA_LDFLAGS := $(EXTRA_LDFLAGS:pthread=lpthread)

# $(info EXTRA_LDFLAGS: $(EXTRA_LDFLAGS))

%.pb.h %.pb.cc: %.proto
	@printf "Compiling  %-35s > %s\n" $< $@
	protoc -I . --cpp_out . $< 

%.grpc.pb.h %.grpc.pb.cc: %.proto
	@printf "Compiling  %-35s > %s\n" $< $@
	protoc -I . --grpc_out . --plugin=protoc-gen-grpc=`which grpc_cpp_plugin` $< 

${DST_DIR}/%.o: %.cu common.h $(TEST_VERIFIABLE_HDRS)
	@printf "Compiling  %-35s > %s\n" $< $@
	@mkdir -p ${DST_DIR}
	$(NVCC) -o $@ $(NVCUFLAGS) -c $<

${DST_DIR}/timer.o: timer.cc timer.h
	@printf "Compiling  %-35s > %s\n" $< $@
	@mkdir -p ${DST_DIR}
	$(CXX) $(CXXFLAGS) -o $@ -c timer.cc

${DST_DIR}/%.pb.o: %.pb.cc ${GRPC_HDRS}
	@printf "Compiling  %-35s > %s\n" $< $@
	@mkdir -p ${DST_DIR}
	$(CXX) $(CXXFLAGS) $(EXTRA_CFLAGS) -o $@ -c $< 

# ${DST_DIR}/allreduce.pb.o: allreduce.pb.cc ${GRPC_HDRS}
# 	@printf "Compiling  %-35s > %s\n" $< $@
# 	@mkdir -p ${DST_DIR}
# 	$(CXX) $(CXXFLAGS) $(EXTRA_CFLAGS) -o $@ -c $< 
 
${DST_DIR}/inc_grpc.o: inc_grpc.cc ${GRPC_HDRS}
	@printf "Compiling  %-35s > %s\n" $< $@
	@mkdir -p ${DST_DIR}
	$(CXX) $(CXXFLAGS) $(EXTRA_CFLAGS) -o $@ -c $< 

${DST_DIR}/inc_all_reduce.o: inc_all_reduce.cu net_utils.hpp yyt_error.h ${GRPC_HDRS} common.h $(TEST_VERIFIABLE_HDRS)
	@printf "Compiling  %-35s > %s\n" $< $@
	@mkdir -p ${DST_DIR}
	$(NVCC) -o $@ $(NVCUFLAGS) -c $<

${DST_DIR}/%_perf:${DST_DIR}/%.o ${DST_DIR}/common.o ${DST_DIR}/timer.o ${DST_DIR}/inc_all_reduce.o ${DST_DIR}/inc_grpc.o ${DST_DIR}/allreduce.grpc.pb.o ${DST_DIR}/allreduce.pb.o $(TEST_VERIFIABLE_OBJS)
	@printf "Linking  %-35s > %s\n" $< $@
	@mkdir -p ${DST_DIR}
	$(NVCC) -o $@ $(NVCUFLAGS) $^ ${NVLDFLAGS} $(EXTRA_LDFLAGS) $(EXTRA_LDFLAGS)

