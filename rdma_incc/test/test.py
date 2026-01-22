import os
import sys
import subprocess
import re
import argparse

collectives = ['allreduce', 'reduce', 'broadcast', 'reducescatter', 'allgather', 'alltoall', 'barrier']
backends = ['inc', 'nccl']

message_sizes = [2**i for i in range(12, 31)]
max_total_size_for_each_message_size = 2**30
max_round = 1000
max_round_alltoall = 100

program = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'build', 'server', 'main'))

workers = ["worker1", "worker2", "worker3", "worker4"] # use ssh target, e.g., root@10.0.0.1
world_size = len(workers)

bind_ips = ["192.168.1.1", "192.168.1.2", "192.168.1.3", "192.168.1.4"]
rank0_ip = "10.0.0.1"
controller = "10.0.0.100:50051"

switch_ip = "192.168.1.254"
switch_mac = "02:00:00:00:00:00"

reduce_max_message_size = 32768

def run_test(collective, message_size):
    global args
    if collective in ['allreduce', 'reduce', 'reducescatter'] and args.backend == 'inc':
        sub_size = min(reduce_max_message_size, message_size)
    else:
        sub_size = message_size
    n = message_size//sub_size
    round = min(max_total_size_for_each_message_size//message_size, max_round)
    if collective == 'alltoall':
        round = min(round, max_round_alltoall)
    message_config = f"-s {sub_size} -n {n} -r {round}"

    procs = []
    for rank, worker in enumerate(workers):
        bind_ip = bind_ips[rank]
        remote_cmd = f"{program} {world_size} {rank} {bind_ip} {rank0_ip} {collective} --controller {controller} {message_config}"
        if args.backend != 'inc':
            remote_cmd += f" --backend {args.backend}"
        cmd = f"ssh -o BatchMode=yes -o StrictHostKeyChecking=no -q -T {worker} '{remote_cmd}'"
        if args.debug:
            print(cmd)
        proc = subprocess.Popen(
            cmd, 
            shell=True,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        procs.append(proc)
    
    pattern = re.compile(r'(\d+(?:\.\d+)?)\s*Gbps')
    for proc in procs:
        proc.wait()
        if proc.returncode != 0:
            print(proc.stdout)
            print(proc.stderr)
            raise Exception(f"Error running {cmd}")
    
    m = pattern.search(procs[0].stdout.read())
    if not m:
        raise Exception(f"Error parsing {procs[0].stdout}")
    alg_bw_str = m.group(1)
    alg_bw = float(alg_bw_str)
    return alg_bw


parser = argparse.ArgumentParser()
parser.add_argument("collective", type=str, choices=collectives)
parser.add_argument("--debug", action="store_true")
parser.add_argument("--backend", type=str, choices=backends, default="inc")
args = parser.parse_args()
collective = args.collective
if collective not in collectives:
    raise Exception(f"Invalid collective: {collective}")

for worker in workers:
    remote_cmd = f"sudo arp -s {switch_ip} {switch_mac}"
    cmd = f"ssh -o BatchMode=yes -o StrictHostKeyChecking=no -q -T {worker} '{remote_cmd}'"
    print(cmd)
    os.system(cmd)

if collective == 'barrier':
    alg_bw = run_test(collective, 256)
    print(f"Message size: {256}, Algorithmic bandwidth: {alg_bw} Gbps")
    exit(0)

for message_size in message_sizes:
    alg_bw = run_test(collective, message_size)
    print(f"Message size: {message_size}, Algorithmic bandwidth: {alg_bw} Gbps")
