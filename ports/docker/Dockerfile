# Reproducible Linux build environment for the RIOT and Contiki-NG ports of
# the Cookie sensor node. Mirrors .github/workflows/ci-os-ports.yml so a local
# Docker build produces the same .hex artifacts as CI, without WSL setup.
#
# Base is 22.04 on purpose: it ships gcc-arm-none-eabi ~10.3, the toolchain
# Contiki-NG release/v4.9 was tested against. Newer gcc-arm (13+, on 24.04)
# turns Contiki's link warnings fatal. RIOT 2024.10 builds fine on 10.3 too,
# so one image covers both OSes.
#
# Note: Ubuntu's gcc-arm-none-eabi ships ONLY the compiler. The newlib C
# library (stdio.h, inttypes.h, libc.a, libm.a) lives in separate packages
# (libnewlib-arm-none-eabi, libstdc++-arm-none-eabi-newlib) — without them
# every build dies on a missing standard header.
#
# Build:  docker build -t cookie-ports ports/docker
# Run:    docker run --rm -v "E:\projects\cookie-thread-mesh:/work" cookie-ports
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# Layer 1: just enough to clone, so the slow source-tree layers below stay
# cached when the toolchain package set is later adjusted.
RUN apt-get update && apt-get install -y --no-install-recommends \
        git ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Layer 2/3: pinned source trees, baked in so a build needs no network.
# RIOT 2024.10 and Contiki-NG release/v4.9 match the CI workflow pins.
RUN git clone --depth 1 --branch 2024.10 \
        https://github.com/RIOT-OS/RIOT.git /opt/RIOT
RUN git clone --recurse-submodules --depth 1 --branch release/v4.9 \
        https://github.com/contiki-ng/contiki-ng.git /opt/contiki-ng

# Layer 4: the full ARM bare-metal toolchain (compiler + newlib C library) and
# the host build tools both OSes need.
RUN apt-get update && apt-get install -y --no-install-recommends \
        gcc-arm-none-eabi \
        binutils-arm-none-eabi \
        libnewlib-arm-none-eabi \
        libstdc++-arm-none-eabi-newlib \
        make \
        unzip \
        srecord \
        python3 \
    && rm -rf /var/lib/apt/lists/*

ENV RIOTBASE=/opt/RIOT
ENV CONTIKI=/opt/contiki-ng

WORKDIR /work
CMD ["bash", "/work/ports/docker/build-ports.sh"]
