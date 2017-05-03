FROM ubuntu:16.04

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        build-essential python ca-certificates wget bzip2 \
        python3 python3-matplotlib \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /opt
RUN wget -q https://www.nsnam.org/release/ns-allinone-3.26.tar.bz2 \
    && tar xjf ns-allinone-3.26.tar.bz2 \
    && rm ns-allinone-3.26.tar.bz2

WORKDIR /opt/ns-allinone-3.26/ns-3.26
RUN ./waf configure -d optimized --disable-python --disable-tests --disable-examples \
    && ./waf build -j"$(nproc)"

ENV NS3_DIR=/opt/ns-allinone-3.26/ns-3.26
