#!/bin/bash

# Dependencies
source /opt/spack/share/spack/setup-env.sh
spack load gcc@8.4.0
spack load cuda@10.2.89
spack load python@3.8.6
spack load py-pip@20.2
spack load cudnn@8.0.4.30-10.2-linux-x64
spack load cmake@3.18.4%gcc@7.5.0
spack load intel-mkl@2020.3.279

# julia
export PATH=/home/pldi22_ae/julia-1.6.3/bin:$PATH
export TVM_HOME=/home/pldi22_ae/tvm-211104/

# cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DWITH_MKL=/mnt/ssd/spack/opt/spack/linux-ubuntu16.04-haswell/gcc-7.3.0/intel-mkl-2020.3.279-ejepo2yk7ihi5tcmg7s4fqne7l7u4fgk/mkl ..

