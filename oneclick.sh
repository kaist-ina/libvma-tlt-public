#!/bin/bash

echo options ib_uverbs disable_raw_qp_enforcement=1 | sudo tee /etc/modprobe.d/ib_uverbs.conf
sudo /etc/init.d/openibd restart

echo 1000000000 | sudo tee /proc/sys/kernel/shmmax
echo 800 | sudo tee /proc/sys/vm/nr_hugepages
ulimit -l unlimited