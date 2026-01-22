#!/usr/bin/python3
import json 
import sys
import os
import ipaddress
import macaddress

config_dir = os.path.dirname(sys.argv[0]) + "/../common"
with open(config_dir + "/allreduce.json") as f: # symbolic link
    allreduce = json.load(f)
    switch_IP = ipaddress.ip_address(int(allreduce["switch_IP"], base=16))
    switch_MAC = str(macaddress.EUI48(int(allreduce["switch_MAC"], base=16))).replace('-', ':')
    cmd = f"sudo arp -s {switch_IP} {switch_MAC}"
    print(cmd)
    os.system(cmd)