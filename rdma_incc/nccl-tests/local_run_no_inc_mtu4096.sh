#!/usr/bin/bash

ifconfig ens10f1np1 mtu 5000
./local_run_raw.sh
ifconfig ens10f1np1 mtu 1500