import json 
import sys
import os

config_dir = "../common"
with open(config_dir + "/topo.json") as f: # symbolic link
    topo = json.load(f)

port = topo["port"]
MAC = topo["MAC"]

for worker in range(len(port)):
    bfrt.selfloop.pipe.Ingress.l2_forward_table.add_with_l2_forward(MAC[worker], port[worker])

bfrt.selfloop.pipe.Egress.red_ecn.wred.add(0, 0, 125, 2500, 0.01)
# DCQCN
# 0 ~ 10KB, 0 
# 10 ~ 200KB, 0 ~ 0.01
# 200KB ~, 1

for p in port:
    bfrt.port.port.add(p, 'BF_SPEED_100G', 'BF_FEC_TYP_NONE', 4, True, 'PM_AN_FORCE_DISABLE')