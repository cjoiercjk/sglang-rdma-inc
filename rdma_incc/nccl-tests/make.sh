#!/usr/bin/bash
set -x
make -j $(nproc) MPI=1 MPI_HOME=/usr/lib/x86_64-linux-gnu/openmpi # options (-j) must come first