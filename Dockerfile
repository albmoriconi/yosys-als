FROM ubuntu:focal

# Install prerequisites
RUN apt-get update
RUN DEBIAN_FRONTEND="noninteractive" apt-get -y install tzdata
RUN apt-get install -y build-essential clang bison flex \
        libreadline-dev gawk tcl-dev libffi-dev git \
        graphviz xdot pkg-config python3 libboost-system-dev \
        libboost-python-dev libboost-filesystem-dev zlib1g-dev
RUN apt-get install -y cmake libboost-serialization-dev \
        libboost-graph-dev libboost-dev libsqlite3-dev curl

WORKDIR /

# Clone repositories
RUN git clone https://github.com/YosysHQ/yosys
RUN git clone https://github.com/Boolector/boolector.git
RUN git clone https://github.com/albmoriconi/yosys-als.git

# Compile
WORKDIR /yosys
RUN git checkout yosys-0.9
RUN make config-gcc
RUN make -j `nproc`
RUN make install

WORKDIR /boolector
RUN git checkout 3.2.0
RUN ./contrib/setup-lingeling.sh
RUN ./contrib/setup-btor2tools.sh
RUN ./configure.sh --shared
WORKDIR /boolector/build
RUN make -j `nproc`
RUN make install

WORKDIR /yosys-als
RUN mkdir -p /yosys-als/build
WORKDIR /yosys-als/build
RUN cmake ..
RUN make -j `nproc`

WORKDIR /
