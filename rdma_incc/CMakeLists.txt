cmake_minimum_required(VERSION 3.7)

project(RINCC) # RDMA In-Net Collective Communications

if(CMAKE_BUILD_TYPE STREQUAL "")
    set(CMAKE_BUILD_TYPE "Release")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS_DEBUG "-Wall -Wextra -Wshadow -g3 -ggdb")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

if(DEFINED ENV{SDE})
    list(APPEND CMAKE_PREFIX_PATH ENV{SDE_INSTALL})
endif()

foreach(path ${CMAKE_PREFIX_PATH}) 
# CMAKE_PREFIX_PATH is used for find binaries and cmake files, not for include dirs and link dirs,
# so we add support here
    link_directories(${path}/lib)
    include_directories(${path}/include)
endforeach()

add_subdirectory(common)
if(DEFINED ENV{SDE})
    message("SDE found. Run cmake for switchd.")
    set(CMAKE_INSTALL_PREFIX $ENV{SDE_INSTALL}) # set install prefix for P4
    add_subdirectory(switch)
else()
    message("SDE not found. Skip cmake for switchd.")
    add_subdirectory(server)
endif()