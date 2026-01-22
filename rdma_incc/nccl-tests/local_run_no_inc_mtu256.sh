#!/usr/bin/bash

ifconfig ens10f1np1 mtu 511
./local_run_raw.sh
ifconfig ens10f1np1 mtu 1500