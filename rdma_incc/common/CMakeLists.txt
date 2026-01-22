include(common.cmake)

set(grpc_cpp_srcs 
${CMAKE_CURRENT_BINARY_DIR}/allreduce.grpc.pb.cc
${CMAKE_CURRENT_BINARY_DIR}/allreduce.pb.cc
)

set(grpc_cpp_hdrs 
${CMAKE_CURRENT_BINARY_DIR}/allreduce.grpc.pb.h
${CMAKE_CURRENT_BINARY_DIR}/allreduce.pb.h
)

set(grpc_cpp_all ${grpc_cpp_srcs} ${grpc_cpp_hdrs})

set(grpc_py_srcs
${CMAKE_CURRENT_SOURCE_DIR}/../switch/allreduce_pb2_grpc.py
${CMAKE_CURRENT_SOURCE_DIR}/../switch/allreduce_pb2.py
${CMAKE_CURRENT_SOURCE_DIR}/../switch/allreduce_pb2.pyi
)

set(proto_srcs
allreduce.proto
)

if(DEFINED ENV{SDE})
    message("Assert SDE use python3.8")
    find_program(PYTHON python3.8 ENV{SDE_INSTALL}/bin) 
    if(NOT PYTHON)
        message(FATAL_ERROR "python not found in SDE_INSTALL")
    endif()

    add_custom_command(
        OUTPUT ${grpc_py_srcs}
        COMMAND ${PYTHON}
        ARGS
        -m grpc_tools.protoc
        -I ${CMAKE_CURRENT_SOURCE_DIR}
        --python_out ${CMAKE_CURRENT_SOURCE_DIR}/../switch
        --pyi_out ${CMAKE_CURRENT_SOURCE_DIR}/../switch # not support in old protoc
        --grpc_python_out ${CMAKE_CURRENT_SOURCE_DIR}/../switch
        ${proto_srcs}
        DEPENDS ${proto_srcs}
    )

    # add_library(grpc_py INTERFACE ${grpc_py_srcs}) # this is similar to add_custom_target()
    add_custom_target(grpc_py ALL SOURCES ${grpc_py_srcs}) # add an empty target in "all" to make sure the files in "grpc_py_srcs" is generated
else()
    find_program(PROTOC protoc)
    find_program(CPP_PLUGIN grpc_cpp_plugin)
    if(NOT (PROTOC AND CPP_PLUGIN))
        message(FATAL_ERROR "gRPC not found, may use -DCMAKE_PREFIX_PATH=<GRPC_INSTALL_PATH>")
    endif()
    message("find protoc at: ${PROTOC}")
    
    add_custom_command(
        OUTPUT ${grpc_cpp_all}
        COMMAND ${PROTOC}
        ARGS
        -I ${CMAKE_CURRENT_SOURCE_DIR}
        --cpp_out ${CMAKE_CURRENT_BINARY_DIR}
        --grpc_out ${CMAKE_CURRENT_BINARY_DIR}
        --plugin=protoc-gen-grpc=${CPP_PLUGIN}
        ${proto_srcs}
        DEPENDS ${proto_srcs}
    )

    add_library(grpc_cpp STATIC ${grpc_cpp_srcs}) # generate ".a" file
    set_target_properties(grpc_cpp PROPERTIES POSITION_INDEPENDENT_CODE ON) # required by our library

    target_link_libraries(grpc_cpp PUBLIC ${_GRPC_GRPCPP} ${_REFLECTION} ${_PROTOBUF_LIBPROTOBUF})
    target_include_directories(grpc_cpp INTERFACE ${CMAKE_CURRENT_BINARY_DIR})
    target_link_directories(grpc_cpp INTERFACE ${CMAKE_CURRENT_BINARY_DIR})
endif()
