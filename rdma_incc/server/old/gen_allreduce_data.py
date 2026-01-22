#!/usr/bin/python3
# encoding: utf-8

import os, sys

file_name = sys.argv[1]
file_size = int(sys.argv[2])

assert(file_size % 4 == 0)
nint = file_size // 4

f = open(file_name, "wb")
f.write(b''.join(x.to_bytes(4, 'big') for x in range(nint)))
