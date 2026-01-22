# 1 GPU 1 NIC

## INC (RDMA MTU 256)

```
+ mpirun --allow-run-as-root -c 1 --hostfile ./hostfile --mca pml_ob1_priority 100 --mca btl_tcp_if_include 10.0.0.0/24 ./local_run_inc.sh
# nThread 1 nGpus 1 minBytes 8 maxBytes 1000000000 step: 2(factor) warmup iters: 5 iters: 20 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid  29095 on    worker1 device  0 [0x31] NVIDIA GeForce RTX 3090
#
#                                                              out-of-place                       in-place
#       size         count      type   redop    root     time   algbw   busbw #wrong     time   algbw   busbw #wrong
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)            (us)  (GB/s)  (GB/s)
           8             2     float     sum      -1    24.48    0.00    0.00      0    24.02    0.00    0.00      0
          16             4     float     sum      -1    24.28    0.00    0.00      0    24.41    0.00    0.00      0
          32             8     float     sum      -1    24.15    0.00    0.00      0    24.11    0.00    0.00      0
          64            16     float     sum      -1    24.22    0.00    0.00      0    24.59    0.00    0.00      0
         128            32     float     sum      -1    25.68    0.00    0.00      0    24.39    0.01    0.00      0
         256            64     float     sum      -1    24.31    0.01    0.00      0    24.48    0.01    0.00      0
         512           128     float     sum      -1    24.20    0.02    0.00      0    24.24    0.02    0.00      0
        1024           256     float     sum      -1    32.73    0.03    0.00      0    32.61    0.03    0.00      0
        2048           512     float     sum      -1    50.76    0.04    0.00      0    50.71    0.04    0.00      0
        4096          1024     float     sum      -1    95.31    0.04    0.00      0    90.93    0.05    0.00      0
        8192          2048     float     sum      -1    92.03    0.09    0.00      0    92.17    0.09    0.00      0
       16384          4096     float     sum      -1    94.00    0.17    0.00      0    93.82    0.17    0.00      0
       32768          8192     float     sum      -1    95.93    0.34    0.00      0    94.97    0.35    0.00      0
       65536         16384     float     sum      -1    101.3    0.65    0.00      0    101.6    0.65    0.00      0
      131072         32768     float     sum      -1    101.6    1.29    0.00      0    99.17    1.32    0.00      0
      262144         65536     float     sum      -1    90.82    2.89    0.00      0    89.26    2.94    0.00      0
      524288        131072     float     sum      -1    96.35    5.44    0.00      0    96.06    5.46    0.00      0
     1048576        262144     float     sum      -1    153.3    6.84    0.00      0    152.5    6.88    0.00      0
     2097152        524288     float     sum      -1    270.1    7.77    0.00      0    270.3    7.76    0.00      0
     4194304       1048576     float     sum      -1    527.1    7.96    0.00      0    527.8    7.95    0.00      0
     8388608       2097152     float     sum      -1   1091.9    7.68    0.00      0   1092.2    7.68    0.00      0
    16777216       4194304     float     sum      -1   2247.2    7.47    0.00      0   2244.7    7.47    0.00      0
    33554432       8388608     float     sum      -1   4346.8    7.72    0.00      0   4353.4    7.71    0.00      0
    67108864      16777216     float     sum      -1   8541.2    7.86    0.00      0   8593.5    7.81    0.00      0
   134217728      33554432     float     sum      -1    16930    7.93    0.00      0    16913    7.94    0.00      0
   268435456      67108864     float     sum      -1    33678    7.97    0.00      0    33675    7.97    0.00      0
   536870912     134217728     float     sum      -1    67383    7.97    0.00      0    67192    7.99    0.00      0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0
#
```

## NO INC

```
+ mpirun --allow-run-as-root -c 1 --hostfile ./hostfile --mca pml_ob1_priority 100 --mca btl_tcp_if_include 10.0.0.0/24 ./local_run_no_inc.sh
# nThread 1 nGpus 1 minBytes 8 maxBytes 1000000000 step: 2(factor) warmup iters: 5 iters: 20 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid  30487 on    worker1 device  0 [0x31] NVIDIA GeForce RTX 3090
#
#                                                              out-of-place                       in-place
#       size         count      type   redop    root     time   algbw   busbw #wrong     time   algbw   busbw #wrong
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)            (us)  (GB/s)  (GB/s)
           8             2     float     sum      -1     6.51    0.00    0.00      0     0.25    0.03    0.00      0
          16             4     float     sum      -1     5.68    0.00    0.00      0     0.24    0.07    0.00      0
          32             8     float     sum      -1     5.63    0.01    0.00      0     0.24    0.13    0.00      0
          64            16     float     sum      -1     5.63    0.01    0.00      0     0.23    0.28    0.00      0
         128            32     float     sum      -1     6.97    0.02    0.00      0     0.23    0.56    0.00      0
         256            64     float     sum      -1     5.55    0.05    0.00      0     0.22    1.14    0.00      0
         512           128     float     sum      -1     5.51    0.09    0.00      0     0.23    2.27    0.00      0
        1024           256     float     sum      -1     5.47    0.19    0.00      0     0.23    4.55    0.00      0
        2048           512     float     sum      -1     5.43    0.38    0.00      0     0.23    8.90    0.00      0
        4096          1024     float     sum      -1     5.46    0.75    0.00      0     0.22   18.33    0.00      0
        8192          2048     float     sum      -1     5.43    1.51    0.00      0     0.23   36.02    0.00      0
       16384          4096     float     sum      -1     5.47    3.00    0.00      0     0.22   72.87    0.00      0
       32768          8192     float     sum      -1     5.49    5.97    0.00      0     0.23  145.60    0.00      0
       65536         16384     float     sum      -1     5.47   11.99    0.00      0     0.24  278.34    0.00      0
      131072         32768     float     sum      -1     5.48   23.94    0.00      0     0.23  579.96    0.00      0
      262144         65536     float     sum      -1     5.52   47.51    0.00      0     0.23  1123.88    0.00      0
      524288        131072     float     sum      -1     5.60   93.61    0.00      0     0.23  2287.97    0.00      0
     1048576        262144     float     sum      -1     5.59  187.73    0.00      0     0.24  4300.09    0.00      0
     2097152        524288     float     sum      -1     8.00  262.30    0.00      0     0.23  9104.20    0.00      0
     4194304       1048576     float     sum      -1    13.20  317.83    0.00      0     0.23  18396.07    0.00      0
     8388608       2097152     float     sum      -1    23.68  354.19    0.00      0     0.22  37365.74    0.00      0
    16777216       4194304     float     sum      -1    43.52  385.53    0.00      0     0.23  71943.46    0.00      0
    33554432       8388608     float     sum      -1    83.03  404.13    0.00      0     0.23  146557.90    0.00      0
    67108864      16777216     float     sum      -1    162.7  412.39    0.00      0     0.22  299192.44    0.00      0
   134217728      33554432     float     sum      -1    322.0  416.79    0.00      0     0.22  610080.58    0.00      0
   268435456      67108864     float     sum      -1    640.5  419.09    0.00      0     0.22  1195171.22    0.00      0
   536870912     134217728     float     sum      -1   1279.7  419.53    0.00      0     0.23  2375009.56    0.00      0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0
#
```

# 2 GPU 2 NIC

## INC (RDMA MTU 256)

```
+ mpirun --allow-run-as-root -c 2 --hostfile ./hostfile --mca pml_ob1_priority 100 --mca btl_tcp_if_include 10.0.0.0/24 ./local_run_inc.sh
# nThread 1 nGpus 1 minBytes 8 maxBytes 1000000000 step: 2(factor) warmup iters: 5 iters: 20 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid  29443 on    worker1 device  0 [0x31] NVIDIA GeForce RTX 3090
#  Rank  1 Group  0 Pid  12020 on    worker2 device  0 [0x31] NVIDIA GeForce RTX 3090
#
#                                                              out-of-place                       in-place
#       size         count      type   redop    root     time   algbw   busbw #wrong     time   algbw   busbw #wrong
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)            (us)  (GB/s)  (GB/s)
           8             2     float     sum      -1    21.33    0.00    0.00      4    20.55    0.00    0.00      4
          16             4     float     sum      -1    20.53    0.00    0.00      8    20.74    0.00    0.00      8
          32             8     float     sum      -1    21.47    0.00    0.00     16    20.84    0.00    0.00     16
          64            16     float     sum      -1    20.71    0.00    0.00     32    21.11    0.00    0.00     32
         128            32     float     sum      -1    20.73    0.01    0.01     64    21.09    0.01    0.01     64
         256            64     float     sum      -1    20.78    0.01    0.01    128    20.88    0.01    0.01    128
         512           128     float     sum      -1    20.97    0.02    0.02    256    20.85    0.02    0.02    256
        1024           256     float     sum      -1    29.98    0.03    0.03    512    32.25    0.03    0.03    512
        2048           512     float     sum      -1    41.38    0.05    0.05   1024    41.59    0.05    0.05   1024
        4096          1024     float     sum      -1    76.47    0.05    0.05   2048    76.37    0.05    0.05   2048
        8192          2048     float     sum      -1    77.22    0.11    0.11   4096    77.20    0.11    0.11   4096
       16384          4096     float     sum      -1    77.28    0.21    0.21   8192    77.30    0.21    0.21   8192
       32768          8192     float     sum      -1    78.19    0.42    0.42  16384    79.51    0.41    0.41  16384
       65536         16384     float     sum      -1    83.21    0.79    0.79  32768    83.57    0.78    0.78  32768
      131072         32768     float     sum      -1    82.64    1.59    1.59  65536    82.84    1.58    1.58  65536
      262144         65536     float     sum      -1    77.25    3.39    3.39  131072    76.44    3.43    3.43  131072
      524288        131072     float     sum      -1    92.84    5.65    5.65  262144    91.64    5.72    5.72  262144
     1048576        262144     float     sum      -1    192.5    5.45    5.45  524288    233.4    4.49    4.49  524288
     2097152        524288     float     sum      -1    311.5    6.73    6.73  1.04858e+06    311.3    6.74    6.74  1.04858e+06
     4194304       1048576     float     sum      -1    619.0    6.78    6.78  2.09715e+06    619.0    6.78    6.78  2.09715e+06
     8388608       2097152     float     sum      -1   1248.3    6.72    6.72  4.1943e+06   1189.1    7.05    7.05  4.1943e+06
    16777216       4194304     float     sum      -1   2410.1    6.96    6.96  8.38861e+06   2402.5    6.98    6.98  8.38861e+06
    33554432       8388608     float     sum      -1   4633.1    7.24    7.24  1.67772e+07   4585.1    7.32    7.32  1.67772e+07
    67108864      16777216     float     sum      -1   8948.6    7.50    7.50  3.35544e+07   8782.3    7.64    7.64  3.35544e+07
   134217728      33554432     float     sum      -1    18679    7.19    7.19  6.71089e+07    17885    7.50    7.50  6.71089e+07
   268435456      67108864     float     sum      -1    36662    7.32    7.32  1.34218e+08    35659    7.53    7.53  1.34218e+08
   536870912     134217728     float     sum      -1    72488    7.41    7.41  2.68435e+08    73092    7.35    7.35  2.68435e+08
# Out of bounds values : 108 FAILED
# Avg bus bandwidth    : 3.02663
#
```

## NO INC (RDMA MTU 256)

```
+ mpirun --allow-run-as-root -c 2 --hostfile ./hostfile --mca pml_ob1_priority 100 --mca btl_tcp_if_include 10.0.0.0/24 ./local_run_no_inc_mtu256.sh
# nThread 1 nGpus 1 minBytes 8 maxBytes 1000000000 step: 2(factor) warmup iters: 5 iters: 20 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid  32014 on    worker1 device  0 [0x31] NVIDIA GeForce RTX 3090
#  Rank  1 Group  0 Pid   1835 on    worker2 device  0 [0x31] NVIDIA GeForce RTX 3090
#
#                                                              out-of-place                       in-place
#       size         count      type   redop    root     time   algbw   busbw #wrong     time   algbw   busbw #wrong
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)            (us)  (GB/s)  (GB/s)
           8             2     float     sum      -1    13.97    0.00    0.00      0    14.89    0.00    0.00      0
          16             4     float     sum      -1    14.28    0.00    0.00      0    15.30    0.00    0.00      0
          32             8     float     sum      -1    15.14    0.00    0.00      0    14.07    0.00    0.00      0
          64            16     float     sum      -1    14.77    0.00    0.00      0    14.66    0.00    0.00      0
         128            32     float     sum      -1    15.15    0.01    0.01      0    14.77    0.01    0.01      0
         256            64     float     sum      -1    15.13    0.02    0.02      0    15.00    0.02    0.02      0
         512           128     float     sum      -1    16.59    0.03    0.03      0    15.44    0.03    0.03      0
        1024           256     float     sum      -1    16.62    0.06    0.06      0    15.46    0.07    0.07      0
        2048           512     float     sum      -1    15.93    0.13    0.13      0    15.94    0.13    0.13      0
        4096          1024     float     sum      -1    16.93    0.24    0.24      0    16.72    0.24    0.24      0
        8192          2048     float     sum      -1    18.82    0.44    0.44      0    19.02    0.43    0.43      0
       16384          4096     float     sum      -1    21.73    0.75    0.75      0    21.54    0.76    0.76      0
       32768          8192     float     sum      -1    27.33    1.20    1.20      0    27.13    1.21    1.21      0
       65536         16384     float     sum      -1    37.23    1.76    1.76      0    38.77    1.69    1.69      0
      131072         32768     float     sum      -1    63.64    2.06    2.06      0    63.13    2.08    2.08      0
      262144         65536     float     sum      -1    64.94    4.04    4.04      0    64.76    4.05    4.05      0
      524288        131072     float     sum      -1    97.14    5.40    5.40      0    96.01    5.46    5.46      0
     1048576        262144     float     sum      -1    165.4    6.34    6.34      0    163.8    6.40    6.40      0
     2097152        524288     float     sum      -1    298.0    7.04    7.04      0    302.8    6.93    6.93      0
     4194304       1048576     float     sum      -1    578.0    7.26    7.26      0    574.1    7.31    7.31      0
     8388608       2097152     float     sum      -1   1187.9    7.06    7.06      0   1186.0    7.07    7.07      0
    16777216       4194304     float     sum      -1   2318.4    7.24    7.24      0   2317.3    7.24    7.24      0
    33554432       8388608     float     sum      -1   4457.6    7.53    7.53      0   4446.1    7.55    7.55      0
    67108864      16777216     float     sum      -1   8550.6    7.85    7.85      0   8568.6    7.83    7.83      0
   134217728      33554432     float     sum      -1    16723    8.03    8.03      0    16690    8.04    8.04      0
   268435456      67108864     float     sum      -1    32903    8.16    8.16      0    32996    8.14    8.14      0
   536870912     134217728     float     sum      -1    65319    8.22    8.22      0    65396    8.21    8.21      0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 3.36556
#
```

## NO INC (RDMA MTU 1024)

```
+ mpirun --allow-run-as-root -c 2 --hostfile ./hostfile --mca pml_ob1_priority 100 --mca btl_tcp_if_include 10.0.0.0/24 ./local_run_no_inc.sh
# nThread 1 nGpus 1 minBytes 8 maxBytes 1000000000 step: 2(factor) warmup iters: 5 iters: 20 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid  30541 on    worker1 device  0 [0x31] NVIDIA GeForce RTX 3090
#  Rank  1 Group  0 Pid  12685 on    worker2 device  0 [0x31] NVIDIA GeForce RTX 3090
#
#                                                              out-of-place                       in-place
#       size         count      type   redop    root     time   algbw   busbw #wrong     time   algbw   busbw #wrong
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)            (us)  (GB/s)  (GB/s)
           8             2     float     sum      -1    13.96    0.00    0.00      0    14.02    0.00    0.00      0
          16             4     float     sum      -1    14.38    0.00    0.00      0    13.95    0.00    0.00      0
          32             8     float     sum      -1    14.75    0.00    0.00      0    14.99    0.00    0.00      0
          64            16     float     sum      -1    14.99    0.00    0.00      0    14.84    0.00    0.00      0
         128            32     float     sum      -1    15.84    0.01    0.01      0    14.77    0.01    0.01      0
         256            64     float     sum      -1    16.14    0.02    0.02      0    15.33    0.02    0.02      0
         512           128     float     sum      -1    16.50    0.03    0.03      0    15.43    0.03    0.03      0
        1024           256     float     sum      -1    16.80    0.06    0.06      0    16.14    0.06    0.06      0
        2048           512     float     sum      -1    17.08    0.12    0.12      0    16.66    0.12    0.12      0
        4096          1024     float     sum      -1    17.60    0.23    0.23      0    17.69    0.23    0.23      0
        8192          2048     float     sum      -1    18.92    0.43    0.43      0    19.20    0.43    0.43      0
       16384          4096     float     sum      -1    22.25    0.74    0.74      0    21.59    0.76    0.76      0
       32768          8192     float     sum      -1    28.28    1.16    1.16      0    27.73    1.18    1.18      0
       65536         16384     float     sum      -1    36.86    1.78    1.78      0    37.82    1.73    1.73      0
      131072         32768     float     sum      -1    57.22    2.29    2.29      0    58.12    2.26    2.26      0
      262144         65536     float     sum      -1    58.26    4.50    4.50      0    59.26    4.42    4.42      0
      524288        131072     float     sum      -1    85.72    6.12    6.12      0    85.30    6.15    6.15      0
     1048576        262144     float     sum      -1    146.7    7.15    7.15      0    142.7    7.35    7.35      0
     2097152        524288     float     sum      -1    246.8    8.50    8.50      0    247.4    8.48    8.48      0
     4194304       1048576     float     sum      -1    478.0    8.77    8.77      0    475.5    8.82    8.82      0
     8388608       2097152     float     sum      -1   1004.3    8.35    8.35      0    987.2    8.50    8.50      0
    16777216       4194304     float     sum      -1   1897.3    8.84    8.84      0   1892.0    8.87    8.87      0
    33554432       8388608     float     sum      -1   3658.4    9.17    9.17      0   3636.2    9.23    9.23      0
    67108864      16777216     float     sum      -1   7075.1    9.49    9.49      0   7065.9    9.50    9.50      0
   134217728      33554432     float     sum      -1    13961    9.61    9.61      0    13955    9.62    9.62      0
   268435456      67108864     float     sum      -1    27736    9.68    9.68      0    27681    9.70    9.70      0
   536870912     134217728     float     sum      -1    54959    9.77    9.77      0    55029    9.76    9.76      0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 3.96373
#
```

# 3 GPU 3 NIC 

## INC (RDMA MTU 256)

```
+ mpirun --allow-run-as-root -c 3 --hostfile ./hostfile --mca pml_ob1_priority 100 --mca btl_tcp_if_include 10.0.0.0/24 ./local_run_inc.sh
# nThread 1 nGpus 1 minBytes 8 maxBytes 1000000000 step: 2(factor) warmup iters: 5 iters: 20 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid  30331 on    worker1 device  0 [0x31] NVIDIA GeForce RTX 3090
#  Rank  1 Group  0 Pid  12583 on    worker2 device  0 [0x31] NVIDIA GeForce RTX 3090
#  Rank  2 Group  0 Pid   2635 on    worker3 device  0 [0x31] NVIDIA GeForce RTX 3090
#
#                                                              out-of-place                       in-place
#       size         count      type   redop    root     time   algbw   busbw #wrong     time   algbw   busbw #wrong
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)            (us)  (GB/s)  (GB/s)
Connect QP
           8             2     float     sum      -1    22.84    0.00    0.00      6    22.38    0.00    0.00      6
          16             4     float     sum      -1    22.98    0.00    0.00     12    23.92    0.00    0.00     12
          32             8     float     sum      -1    21.92    0.00    0.00     24    22.79    0.00    0.00     24
          64            16     float     sum      -1    22.70    0.00    0.00     48    22.56    0.00    0.00     48
         128            32     float     sum      -1    22.88    0.01    0.01     96    21.94    0.01    0.01     96
         256            64     float     sum      -1    22.88    0.01    0.01    192    23.27    0.01    0.01    192
         512           128     float     sum      -1    22.78    0.02    0.03    384    22.83    0.02    0.03    384
        1024           256     float     sum      -1    31.21    0.03    0.04    768    31.68    0.03    0.04    768
        2048           512     float     sum      -1    43.67    0.05    0.06   1536    44.24    0.05    0.06   1536
        4096          1024     float     sum      -1    80.51    0.05    0.07   3072    80.74    0.05    0.07   3072
        8192          2048     float     sum      -1    82.65    0.10    0.13   6144    82.19    0.10    0.13   6144
       16384          4096     float     sum      -1    83.32    0.20    0.26  12288    82.66    0.20    0.26  12288
       32768          8192     float     sum      -1    85.31    0.38    0.51  24576    86.21    0.38    0.51  24576
       65536         16384     float     sum      -1    89.35    0.73    0.98  49152    89.71    0.73    0.97  49152
      131072         32768     float     sum      -1    92.10    1.42    1.90  98304    98.05    1.34    1.78  98304
      262144         65536     float     sum      -1    83.71    3.13    4.18  196608    84.23    3.11    4.15  196608
      524288        131072     float     sum      -1    96.78    5.42    7.22  393216    97.37    5.38    7.18  393216
     1048576        262144     float     sum      -1    160.7    6.53    8.70  786432    158.4    6.62    8.83  786432
     2097152        524288     float     sum      -1    295.4    7.10    9.47  1.57286e+06    328.4    6.39    8.52  1.57286e+06
     4194304       1048576     float     sum      -1    636.9    6.59    8.78  3.14573e+06    629.4    6.66    8.89  3.14573e+06
     8388608       2097152     float     sum      -1   1270.8    6.60    8.80  6.29146e+06   1316.4    6.37    8.50  6.29146e+06
    16777216       4194304     float     sum      -1   2448.9    6.85    9.13  1.25829e+07   2445.4    6.86    9.15  1.25829e+07
    33554432       8388608     float     sum      -1   4739.5    7.08    9.44  2.51658e+07   4745.6    7.07    9.43  2.51658e+07
    67108864      16777216     float     sum      -1   9219.6    7.28    9.71  5.03316e+07   8960.3    7.49    9.99  5.03316e+07
   134217728      33554432     float     sum      -1    18807    7.14    9.52  1.00663e+08    18644    7.20    9.60  1.00663e+08
   268435456      67108864     float     sum      -1    36675    7.32    9.76  2.01327e+08    36653    7.32    9.77  2.01327e+08
   536870912     134217728     float     sum      -1    72761    7.38    9.84  4.02653e+08    73330    7.32    9.76  4.02653e+08
# Out of bounds values : 162 FAILED
# Avg bus bandwidth    : 4.00344
#
```

## NO INC (RDMA MTU 256)
```
+ mpirun --allow-run-as-root -c 3 --hostfile ./hostfile --mca pml_ob1_priority 100 --mca btl_tcp_if_include 10.0.0.0/24 ./local_run_no_inc_mtu256.sh
Warning: Permanently added '[10.0.0.1]:60022' (ED25519) to the list of known hosts.
Warning: Permanently added '[10.0.0.2]:60022' (ED25519) to the list of known hosts.
Warning: Permanently added '[10.0.0.4]:60022' (ED25519) to the list of known hosts.
Warning: Permanently added '[10.0.0.3]:60022' (ED25519) to the list of known hosts.
[1719130414.920772] [worker3:3099 :0]     ucp_context.c:1774 UCX  WARN  UCP version is incompatible, required: 1.15, actual: 1.12 (release 1)
[1719130416.334685] [worker2:1873 :0]     ucp_context.c:1774 UCX  WARN  UCP version is incompatible, required: 1.15, actual: 1.12 (release 1)
[1719130421.557325] [worker3:3099 :0]     ucp_context.c:1774 UCX  WARN  UCP version is incompatible, required: 1.15, actual: 1.12 (release 1)
[1719130420.529199] [worker2:1873 :0]     ucp_context.c:1774 UCX  WARN  UCP version is incompatible, required: 1.15, actual: 1.12 (release 1)
# nThread 1 nGpus 1 minBytes 8 maxBytes 1000000000 step: 2(factor) warmup iters: 5 iters: 20 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid  32071 on    worker1 device  0 [0x31] NVIDIA GeForce RTX 3090
#  Rank  1 Group  0 Pid   1873 on    worker2 device  0 [0x31] NVIDIA GeForce RTX 3090
#  Rank  2 Group  0 Pid   3099 on    worker3 device  0 [0x31] NVIDIA GeForce RTX 3090
#
#                                                              out-of-place                       in-place
#       size         count      type   redop    root     time   algbw   busbw #wrong     time   algbw   busbw #wrong
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)            (us)  (GB/s)  (GB/s)
           8             2     float     sum      -1    27.97    0.00    0.00      0    25.35    0.00    0.00      0
          16             4     float     sum      -1    24.96    0.00    0.00      0    25.38    0.00    0.00      0
          32             8     float     sum      -1    25.40    0.00    0.00      0    25.49    0.00    0.00      0
          64            16     float     sum      -1    26.52    0.00    0.00      0    25.97    0.00    0.00      0
         128            32     float     sum      -1    26.88    0.00    0.01      0    26.29    0.00    0.01      0
         256            64     float     sum      -1    27.76    0.01    0.01      0    26.93    0.01    0.01      0
         512           128     float     sum      -1    28.88    0.02    0.02      0    27.66    0.02    0.02      0
        1024           256     float     sum      -1    29.69    0.03    0.05      0    28.64    0.04    0.05      0
        2048           512     float     sum      -1    31.54    0.06    0.09      0    31.19    0.07    0.09      0
        4096          1024     float     sum      -1    35.53    0.12    0.15      0    34.97    0.12    0.16      0
        8192          2048     float     sum      -1    31.04    0.26    0.35      0    30.35    0.27    0.36      0
       16384          4096     float     sum      -1    38.26    0.43    0.57      0    37.60    0.44    0.58      0
       32768          8192     float     sum      -1    45.67    0.72    0.96      0    44.61    0.73    0.98      0
       65536         16384     float     sum      -1    53.67    1.22    1.63      0    54.57    1.20    1.60      0
      131072         32768     float     sum      -1    83.00    1.58    2.11      0    85.48    1.53    2.04      0
      262144         65536     float     sum      -1    93.82    2.79    3.73      0    94.39    2.78    3.70      0
      524288        131072     float     sum      -1    121.6    4.31    5.75      0    121.4    4.32    5.76      0
     1048576        262144     float     sum      -1    202.1    5.19    6.92      0    199.2    5.27    7.02      0
     2097152        524288     float     sum      -1    361.0    5.81    7.75      0    360.3    5.82    7.76      0
     4194304       1048576     float     sum      -1    702.7    5.97    7.96      0    700.2    5.99    7.99      0
     8388608       2097152     float     sum      -1   1463.0    5.73    7.65      0   1461.0    5.74    7.66      0
    16777216       4194304     float     sum      -1   2943.1    5.70    7.60      0   2930.2    5.73    7.63      0
    33554432       8388608     float     sum      -1   5870.6    5.72    7.62      0   5825.8    5.76    7.68      0
    67108864      16777216     float     sum      -1    11440    5.87    7.82      0    11457    5.86    7.81      0
   134217728      33554432     float     sum      -1    22580    5.94    7.93      0    22617    5.93    7.91      0
   268435456      67108864     float     sum      -1    44976    5.97    7.96      0    44915    5.98    7.97      0
   536870912     134217728     float     sum      -1    89511    6.00    8.00      0    89602    5.99    7.99      0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 3.43326
#
```

## NO INC (RDMA MTU 1024)

```
+ mpirun --allow-run-as-root -c 3 --hostfile ./hostfile --mca pml_ob1_priority 100 --mca btl_tcp_if_include 10.0.0.0/24 ./local_run_no_inc.sh
# nThread 1 nGpus 1 minBytes 8 maxBytes 1000000000 step: 2(factor) warmup iters: 5 iters: 20 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid  30753 on    worker1 device  0 [0x31] NVIDIA GeForce RTX 3090
#  Rank  1 Group  0 Pid  12825 on    worker2 device  0 [0x31] NVIDIA GeForce RTX 3090
#  Rank  2 Group  0 Pid   2839 on    worker3 device  0 [0x31] NVIDIA GeForce RTX 3090
#
#                                                              out-of-place                       in-place
#       size         count      type   redop    root     time   algbw   busbw #wrong     time   algbw   busbw #wrong
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)            (us)  (GB/s)  (GB/s)
           8             2     float     sum      -1    25.23    0.00    0.00      0    24.17    0.00    0.00      0
          16             4     float     sum      -1    25.64    0.00    0.00      0    24.99    0.00    0.00      0
          32             8     float     sum      -1    25.71    0.00    0.00      0    25.60    0.00    0.00      0
          64            16     float     sum      -1    26.71    0.00    0.00      0    26.20    0.00    0.00      0
         128            32     float     sum      -1    26.82    0.00    0.01      0    26.78    0.00    0.01      0
         256            64     float     sum      -1    33.32    0.01    0.01      0    28.18    0.01    0.01      0
         512           128     float     sum      -1    30.65    0.02    0.02      0    31.27    0.02    0.02      0
        1024           256     float     sum      -1    30.82    0.03    0.04      0    30.41    0.03    0.04      0
        2048           512     float     sum      -1    33.51    0.06    0.08      0    32.89    0.06    0.08      0
        4096          1024     float     sum      -1    36.93    0.11    0.15      0    37.13    0.11    0.15      0
        8192          2048     float     sum      -1    32.86    0.25    0.33      0    32.24    0.25    0.34      0
       16384          4096     float     sum      -1    38.88    0.42    0.56      0    39.44    0.42    0.55      0
       32768          8192     float     sum      -1    46.96    0.70    0.93      0    49.40    0.66    0.88      0
       65536         16384     float     sum      -1    56.89    1.15    1.54      0    57.23    1.15    1.53      0
      131072         32768     float     sum      -1    85.54    1.53    2.04      0    88.33    1.48    1.98      0
      262144         65536     float     sum      -1    87.06    3.01    4.01      0    87.15    3.01    4.01      0
      524288        131072     float     sum      -1    111.3    4.71    6.28      0    109.8    4.77    6.37      0
     1048576        262144     float     sum      -1    173.6    6.04    8.05      0    171.4    6.12    8.16      0
     2097152        524288     float     sum      -1    305.1    6.87    9.16      0    302.9    6.92    9.23      0
     4194304       1048576     float     sum      -1    573.9    7.31    9.74      0    572.0    7.33    9.78      0
     8388608       2097152     float     sum      -1   1157.2    7.25    9.66      0   1153.4    7.27    9.70      0
    16777216       4194304     float     sum      -1   2353.4    7.13    9.51      0   2338.6    7.17    9.57      0
    33554432       8388608     float     sum      -1   4680.8    7.17    9.56      0   4674.5    7.18    9.57      0
    67108864      16777216     float     sum      -1   9180.4    7.31    9.75      0   9231.9    7.27    9.69      0
   134217728      33554432     float     sum      -1    17928    7.49    9.98      0    17975    7.47    9.96      0
   268435456      67108864     float     sum      -1    35492    7.56   10.08      0    35522    7.56   10.08      0
   536870912     134217728     float     sum      -1    70587    7.61   10.14      0    70613    7.60   10.14      0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 4.13891
#
```

# 4 GPU 4 NIC

## INC (RDMA MTU 256)

```
+ mpirun --allow-run-as-root -c 4 --hostfile ./hostfile --mca pml_ob1_priority 100 --mca btl_tcp_if_include 10.0.0.0/24 ./local_run_inc.sh
Warning: Permanently added '[10.0.0.1]:60022' (ED25519) to the list of known hosts.
Warning: Permanently added '[10.0.0.2]:60022' (ED25519) to the list of known hosts.
Warning: Permanently added '[10.0.0.3]:60022' (ED25519) to the list of known hosts.
Warning: Permanently added '[10.0.0.4]:60022' (ED25519) to the list of known hosts.
[1719127610.064172] [worker3:2670 :0]     ucp_context.c:1774 UCX  WARN  UCP version is incompatible, required: 1.15, actual: 1.12 (release 1)
[1719127609.106938] [worker4:2432 :0]     ucp_context.c:1774 UCX  WARN  UCP version is incompatible, required: 1.15, actual: 1.12 (release 1)
[1719127625.180465] [worker4:2432 :0]     ucp_context.c:1774 UCX  WARN  UCP version is incompatible, required: 1.15, actual: 1.12 (release 1)
[1719127626.190872] [worker3:2670 :0]     ucp_context.c:1774 UCX  WARN  UCP version is incompatible, required: 1.15, actual: 1.12 (release 1)
# nThread 1 nGpus 1 minBytes 8 maxBytes 1000000000 step: 2(factor) warmup iters: 5 iters: 20 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid  30407 on    worker1 device  0 [0x31] NVIDIA GeForce RTX 3090
#  Rank  1 Group  0 Pid  12623 on    worker2 device  0 [0x31] NVIDIA GeForce RTX 3090
#  Rank  2 Group  0 Pid   2670 on    worker3 device  0 [0x31] NVIDIA GeForce RTX 3090
#  Rank  3 Group  0 Pid   2432 on    worker4 device  0 [0x31] NVIDIA GeForce RTX 3090
#
#                                                              out-of-place                       in-place
#       size         count      type   redop    root     time   algbw   busbw #wrong     time   algbw   busbw #wrong
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)            (us)  (GB/s)  (GB/s)
           8             2     float     sum      -1    22.57    0.00    0.00      8    21.87    0.00    0.00      8
          16             4     float     sum      -1    23.33    0.00    0.00     16    22.77    0.00    0.00     16
          32             8     float     sum      -1    23.10    0.00    0.00     32    22.17    0.00    0.00     32
          64            16     float     sum      -1    22.91    0.00    0.00     64    22.96    0.00    0.00     64
         128            32     float     sum      -1    23.29    0.01    0.01    128    23.18    0.01    0.01    128
         256            64     float     sum      -1    23.47    0.01    0.02    256    23.47    0.01    0.02    256
         512           128     float     sum      -1    23.16    0.02    0.03    512    23.25    0.02    0.03    512
        1024           256     float     sum      -1    29.09    0.04    0.05   1024    29.48    0.03    0.05   1024
        2048           512     float     sum      -1    44.57    0.05    0.07   2048    44.82    0.05    0.07   2048
        4096          1024     float     sum      -1    81.08    0.05    0.08   4096    80.60    0.05    0.08   4096
        8192          2048     float     sum      -1    82.87    0.10    0.15   8192    83.16    0.10    0.15   8192
       16384          4096     float     sum      -1    83.71    0.20    0.29  16384    83.19    0.20    0.30  16384
       32768          8192     float     sum      -1    87.34    0.38    0.56  32768    85.87    0.38    0.57  32768
       65536         16384     float     sum      -1    89.39    0.73    1.10  65536    91.34    0.72    1.08  65536
      131072         32768     float     sum      -1    98.14    1.34    2.00  131072    100.9    1.30    1.95  131072
      262144         65536     float     sum      -1    87.03    3.01    4.52  262144    87.70    2.99    4.48  262144
      524288        131072     float     sum      -1    99.06    5.29    7.94  524288    100.2    5.23    7.85  524288
     1048576        262144     float     sum      -1    187.7    5.59    8.38  1.04858e+06    189.3    5.54    8.31  1.04858e+06
     2097152        524288     float     sum      -1    339.8    6.17    9.26  2.09715e+06    340.9    6.15    9.23  2.09715e+06
     4194304       1048576     float     sum      -1    685.4    6.12    9.18  4.1943e+06    683.4    6.14    9.21  4.1943e+06
     8388608       2097152     float     sum      -1   1265.5    6.63    9.94  8.38861e+06   1211.5    6.92   10.39  8.38861e+06
    16777216       4194304     float     sum      -1   2435.8    6.89   10.33  1.67772e+07   2488.4    6.74   10.11  1.67772e+07
    33554432       8388608     float     sum      -1   4660.7    7.20   10.80  3.35544e+07   4620.9    7.26   10.89  3.35544e+07
    67108864      16777216     float     sum      -1   9118.5    7.36   11.04  6.71089e+07   9487.9    7.07   10.61  6.71089e+07
   134217728      33554432     float     sum      -1    18470    7.27   10.90  1.34218e+08    18954    7.08   10.62  1.34218e+08
   268435456      67108864     float     sum      -1    36477    7.36   11.04  2.68435e+08    37661    7.13   10.69  2.68435e+08
   536870912     134217728     float     sum      -1    72335    7.42   11.13  5.36871e+08    73198    7.33   11.00  5.36871e+08
# Out of bounds values : 216 FAILED
# Avg bus bandwidth    : 4.38015
#
```

## NO INC (RDMA MTU 256)
```
+ mpirun --allow-run-as-root -c 4 --hostfile ./hostfile --mca pml_ob1_priority 100 --mca btl_tcp_if_include 10.0.0.0/24 ./local_run_no_inc_mtu256.sh
# nThread 1 nGpus 1 minBytes 8 maxBytes 1000000000 step: 2(factor) warmup iters: 5 iters: 20 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid  32128 on    worker1 device  0 [0x31] NVIDIA GeForce RTX 3090
#  Rank  1 Group  0 Pid   1911 on    worker2 device  0 [0x31] NVIDIA GeForce RTX 3090
#  Rank  2 Group  0 Pid   3137 on    worker3 device  0 [0x31] NVIDIA GeForce RTX 3090
#  Rank  3 Group  0 Pid   2846 on    worker4 device  0 [0x31] NVIDIA GeForce RTX 3090
#
#                                                              out-of-place                       in-place
#       size         count      type   redop    root     time   algbw   busbw #wrong     time   algbw   busbw #wrong
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)            (us)  (GB/s)  (GB/s)
           8             2     float     sum      -1    26.72    0.00    0.00      0    24.64    0.00    0.00      0
          16             4     float     sum      -1    25.49    0.00    0.00      0    27.13    0.00    0.00      0
          32             8     float     sum      -1    27.05    0.00    0.00      0    27.17    0.00    0.00      0
          64            16     float     sum      -1    28.35    0.00    0.00      0    27.81    0.00    0.00      0
         128            32     float     sum      -1    28.69    0.00    0.01      0    30.38    0.00    0.01      0
         256            64     float     sum      -1    28.74    0.01    0.01      0    28.25    0.01    0.01      0
         512           128     float     sum      -1    29.42    0.02    0.03      0    28.99    0.02    0.03      0
        1024           256     float     sum      -1    31.07    0.03    0.05      0    30.88    0.03    0.05      0
        2048           512     float     sum      -1    34.38    0.06    0.09      0    34.36    0.06    0.09      0
        4096          1024     float     sum      -1    35.89    0.11    0.17      0    36.45    0.11    0.17      0
        8192          2048     float     sum      -1    38.59    0.21    0.32      0    38.78    0.21    0.32      0
       16384          4096     float     sum      -1    42.85    0.38    0.57      0    41.54    0.39    0.59      0
       32768          8192     float     sum      -1    50.45    0.65    0.97      0    48.01    0.68    1.02      0
       65536         16384     float     sum      -1    63.29    1.04    1.55      0    62.41    1.05    1.58      0
      131072         32768     float     sum      -1    89.24    1.47    2.20      0    98.50    1.33    2.00      0
      262144         65536     float     sum      -1    168.3    1.56    2.34      0    195.9    1.34    2.01      0
      524288        131072     float     sum      -1    138.9    3.77    5.66      0    139.6    3.76    5.63      0
     1048576        262144     float     sum      -1    223.3    4.69    7.04      0    217.5    4.82    7.23      0
     2097152        524288     float     sum      -1    399.0    5.26    7.88      0    397.8    5.27    7.91      0
     4194304       1048576     float     sum      -1    760.5    5.51    8.27      0    759.9    5.52    8.28      0
     8388608       2097152     float     sum      -1   1549.5    5.41    8.12      0   1547.0    5.42    8.13      0
    16777216       4194304     float     sum      -1   3373.8    4.97    7.46      0   3367.5    4.98    7.47      0
    33554432       8388608     float     sum      -1   6649.4    5.05    7.57      0   6685.0    5.02    7.53      0
    67108864      16777216     float     sum      -1    13217    5.08    7.62      0    13127    5.11    7.67      0
   134217728      33554432     float     sum      -1    26057    5.15    7.73      0    26049    5.15    7.73      0
   268435456      67108864     float     sum      -1    51801    5.18    7.77      0    51731    5.19    7.78      0
   536870912     134217728     float     sum      -1   103349    5.19    7.79      0   103535    5.19    7.78      0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 3.37512
#
```

## NO INC (RDMA MTU 1024)

```
+ mpirun --allow-run-as-root -c 4 --hostfile ./hostfile --mca pml_ob1_priority 100 --mca btl_tcp_if_include 10.0.0.0/24 ./local_run_no_inc.sh
Warning: Permanently added '[10.0.0.4]:60022' (ED25519) to the list of known hosts.
Warning: Permanently added '[10.0.0.2]:60022' (ED25519) to the list of known hosts.
Warning: Permanently added '[10.0.0.1]:60022' (ED25519) to the list of known hosts.
Warning: Permanently added '[10.0.0.3]:60022' (ED25519) to the list of known hosts.
[1719128362.047259] [worker3:2907 :0]     ucp_context.c:1774 UCX  WARN  UCP version is incompatible, required: 1.15, actual: 1.12 (release 1)
[1719128361.083993] [worker4:2633 :0]     ucp_context.c:1774 UCX  WARN  UCP version is incompatible, required: 1.15, actual: 1.12 (release 1)
[1719128365.612051] [worker3:2907 :0]     ucp_context.c:1774 UCX  WARN  UCP version is incompatible, required: 1.15, actual: 1.12 (release 1)
[1719128364.602869] [worker4:2633 :0]     ucp_context.c:1774 UCX  WARN  UCP version is incompatible, required: 1.15, actual: 1.12 (release 1)
# nThread 1 nGpus 1 minBytes 8 maxBytes 1000000000 step: 2(factor) warmup iters: 5 iters: 20 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid  30859 on    worker1 device  0 [0x31] NVIDIA GeForce RTX 3090
#  Rank  1 Group  0 Pid  12895 on    worker2 device  0 [0x31] NVIDIA GeForce RTX 3090
#  Rank  2 Group  0 Pid   2907 on    worker3 device  0 [0x31] NVIDIA GeForce RTX 3090
#  Rank  3 Group  0 Pid   2633 on    worker4 device  0 [0x31] NVIDIA GeForce RTX 3090
#
#                                                              out-of-place                       in-place
#       size         count      type   redop    root     time   algbw   busbw #wrong     time   algbw   busbw #wrong
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)            (us)  (GB/s)  (GB/s)
           8             2     float     sum      -1    25.74    0.00    0.00      0    24.87    0.00    0.00      0
          16             4     float     sum      -1    26.74    0.00    0.00      0    26.00    0.00    0.00      0
          32             8     float     sum      -1    26.28    0.00    0.00      0    26.34    0.00    0.00      0
          64            16     float     sum      -1    26.02    0.00    0.00      0    26.86    0.00    0.00      0
         128            32     float     sum      -1    28.01    0.00    0.01      0    27.88    0.00    0.01      0
         256            64     float     sum      -1    28.39    0.01    0.01      0    28.51    0.01    0.01      0
         512           128     float     sum      -1    29.77    0.02    0.03      0    29.42    0.02    0.03      0
        1024           256     float     sum      -1    31.46    0.03    0.05      0    31.57    0.03    0.05      0
        2048           512     float     sum      -1    34.00    0.06    0.09      0    34.80    0.06    0.09      0
        4096          1024     float     sum      -1    36.46    0.11    0.17      0    35.05    0.12    0.18      0
        8192          2048     float     sum      -1    37.88    0.22    0.32      0    38.87    0.21    0.32      0
       16384          4096     float     sum      -1    41.85    0.39    0.59      0    42.99    0.38    0.57      0
       32768          8192     float     sum      -1    50.75    0.65    0.97      0    47.71    0.69    1.03      0
       65536         16384     float     sum      -1    65.39    1.00    1.50      0    66.38    0.99    1.48      0
      131072         32768     float     sum      -1    94.54    1.39    2.08      0    94.19    1.39    2.09      0
      262144         65536     float     sum      -1    159.9    1.64    2.46      0    174.6    1.50    2.25      0
      524288        131072     float     sum      -1    127.1    4.13    6.19      0    126.4    4.15    6.22      0
     1048576        262144     float     sum      -1    187.5    5.59    8.39      0    187.1    5.60    8.41      0
     2097152        524288     float     sum      -1    336.0    6.24    9.36      0    336.0    6.24    9.36      0
     4194304       1048576     float     sum      -1    624.3    6.72   10.08      0    624.8    6.71   10.07      0
     8388608       2097152     float     sum      -1   1225.9    6.84   10.26      0   1224.1    6.85   10.28      0
    16777216       4194304     float     sum      -1   2800.5    5.99    8.99      0   2832.4    5.92    8.89      0
    33554432       8388608     float     sum      -1   5531.6    6.07    9.10      0   5563.6    6.03    9.05      0
    67108864      16777216     float     sum      -1    10607    6.33    9.49      0    10594    6.33    9.50      0
   134217728      33554432     float     sum      -1    20830    6.44    9.67      0    20811    6.45    9.67      0
   268435456      67108864     float     sum      -1    41414    6.48    9.72      0    41290    6.50    9.75      0
   536870912     134217728     float     sum      -1    82135    6.54    9.80      0    82383    6.52    9.78      0
```