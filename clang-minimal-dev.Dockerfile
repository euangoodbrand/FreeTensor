FROM ubuntu:22.04

RUN apt-get update
RUN DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    g++ python3 python3-dev python3-pip python3-venv cmake make ninja-build \
    autoconf automake libtool openjdk-11-jdk libgmp-dev wget xz-utils libtinfo5

# Install Clang 16
WORKDIR /utils/clang
RUN wget https://github.com/llvm/llvm-project/releases/download/llvmorg-16.0.0/clang+llvm-16.0.0-x86_64-linux-gnu-ubuntu-18.04.tar.xz \
    && tar -xf clang+llvm-16.0.0-x86_64-linux-gnu-ubuntu-18.04.tar.xz
ENV PATH=/utils/clang/clang+llvm-16.0.0-x86_64-linux-gnu-ubuntu-18.04/bin/:$PATH
ENV LD_LIBRARY_PATH=/utils/clang/clang+llvm-16.0.0-x86_64-linux-gnu-ubuntu-18.04/lib:$LD_LIBRARY_PATH
ENV CC=clang
ENV CXX=clang++

WORKDIR /opt/freetensor
COPY . .
RUN PY_BUILD_CMAKE_VERBOSE=1 pip3 install -i https://pypi.tuna.tsinghua.edu.cn/simple -v -e .

WORKDIR /workspace
