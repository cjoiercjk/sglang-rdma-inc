#!/usr/bin/bash

./run.sh ./local_run_inc.sh 4
./run.sh ./local_run_no_inc_mtu256.sh 4
./run.sh ./local_run_no_inc.sh 4 # mtu == 1024
./run.sh ./local_run_no_inc_mtu4096.sh 4