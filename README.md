# libvma implementation for TLT
This is an official Github repository for the Eurosys '21 paper "[Towards timeout-less transport in commodity datacenter networks.](https://dl.acm.org/doi/10.1145/3447786.3456227)".
* For the simulator, please visit [https://github.com/kaist-ina/ns3-tlt-tcp-public](https://github.com/kaist-ina/ns3-tlt-tcp-public).


This repository provides a TLT impilementation (TCP version) on top of libvma ([Mellanox's Messaging Accelerator](https://github.com/Mellanox/libvma)).
This implementation also includes DCTCP, SACK on LwIP stack inside libvma.

If you find this repository useful in your research, please consider citing:
```
@inproceedings{10.1145/3447786.3456227,
author = {Lim, Hwijoon and Bai, Wei and Zhu, Yibo and Jung, Youngmok and Han, Dongsu},
title = {Towards Timeout-Less Transport in Commodity Datacenter Networks},
year = {2021},
isbn = {9781450383349},
publisher = {Association for Computing Machinery},
address = {New York, NY, USA},
url = {https://doi.org/10.1145/3447786.3456227},
doi = {10.1145/3447786.3456227}
}
```

Note this implementation has not been tested thoroughly and thus not stable. Please use for evaluation purposes only.

## Guide
### 0. Prerequisites
Please refer to [wiki](https://github.com/Mellanox/libvma/wiki) of the original repository for the prerequisites.
We tested the implementation on Ubuntu 18.04.

### 1. Setup
We placed all required kernel configuration on `oneclick.sh`.
```
source ./oneclick.sh
```

### 2. Build
`Makefile` should build `libvma.so`.

### 3. Run your application
Please use `LD_PRELOAD` to run your application with `libvma.so`.

### Additional Information
* Refer to the libvma [README.txt](https://github.com/Mellanox/libvma/blob/master/README.txt)
* Main VMA page on Mellanox.com: http://www.mellanox.com/vma/
* Check out the rest of the Wiki pages in this project
