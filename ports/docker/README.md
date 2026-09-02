# Local build environment for the RIOT and Contiki-NG ports

These ports are Linux-first (Make + POSIX) and do not build on native Windows.
This directory packages the whole Linux toolchain as a Docker image, so the
`.hex` can be built on Windows without WSL setup and then flashed with
`nrfjprog`. The image mirrors `.github/workflows/ci-os-ports.yml`: same pinned
RIOT (`2024.10`) and Contiki-NG (`release/v4.9`), same Ubuntu 22.04 base (which
carries the older gcc-arm ~10.3 that Contiki-NG v4.9 needs).

## One-time: build the image

    docker build -t cookie-ports ports/docker

This bakes in the ARM toolchain and both source trees (~4 GB). It needs the
Docker daemon running (Docker Desktop, Linux containers).

## Build all four binaries

    docker run --rm -v "E:\projects\cookie-thread-mesh:/work" cookie-ports

Runs `build-ports.sh`: RIOT sensor+gateway (boards `nrf52840dk` and
`cookie_nrf_v200`) and Contiki-NG sensor+gateway (boards `dongle` and
`cookie`), then `objcopy`s every ELF to a flashable `.hex`. Each target runs
even if an earlier one fails, and a summary table is printed at the end.

Outputs land in the mounted tree:

| Port | HEX |
|------|-----|
| RIOT sensor (DK / Cookie) | `ports/riot/sensor_node/bin/<board>/cookie_sensor_node_riot.hex` |
| RIOT gateway (DK / Cookie) | `ports/riot/gateway/bin/<board>/cookie_gateway_riot.hex` |
| Contiki sensor (dongle / cookie) | `ports/contiki-ng/sensor_node/build/nrf52840/<board>/cookie-sensor-node.hex` |
| Contiki gateway (dongle / cookie) | `ports/contiki-ng/gateway/build/nrf52840/<board>/cookie-gateway.hex` |

## Build one target only

    docker run --rm -v "E:\projects\cookie-thread-mesh:/work" cookie-ports \
        make -C /work/ports/riot/sensor_node BOARD=cookie_nrf_v200 RIOTBASE=/opt/RIOT

(`RIOTBASE` and `CONTIKI` are already set in the image.)

## Flash from Windows

The board sits on the nRF52-DK; flash over SWD with the nRF Command Line Tools:

    nrfjprog --program ports\riot\sensor_node\bin\cookie_nrf_v200\cookie_sensor_node_riot.hex --chiperase --verify -r

## Why a dependency beyond gcc-arm-none-eabi

Ubuntu's `gcc-arm-none-eabi` package is the compiler only. The newlib C library
(`stdio.h`, `inttypes.h`, `libc.a`, `libm.a`) is split into
`libnewlib-arm-none-eabi` and `libstdc++-arm-none-eabi-newlib`. Without them
every build fails on a missing standard header. Both the Dockerfile and the CI
workflow install them.
