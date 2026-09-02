# Cookie Thread Mesh

Firmware for my master's thesis. A small Thread (802.15.4) mesh on the nRF52840:
a Cookie sensor board plus a couple of nRF52840 dongles. Each node reads its
sensors and pushes the data over CoAP to a gateway dongle, which prints it as
JSON over USB. A Python tool shows it live and logs it for the plots in the
thesis.

Built with nRF Connect SDK v3.3 / Zephyr.

## Build

From the west workspace root (next to `nrf/` and `zephyr/`):

    python cookie-thread-mesh/scripts/build_all.py

Outputs go to `build/`.

## Flash

Cookie board, over SWD through the nRF52-DK:

    west flash -d build/cookie_node_auto --runner jlink

Dongle, over USB DFU (hold reset while plugging it in):

    nrfutil nrf5sdk-tools pkg generate --hw-version 52 --sd-req 0x00 \
        --application build/dongle_gateway/gateway/zephyr/zephyr.hex \
        --application-version 1 gw.zip
    nrfutil nrf5sdk-tools dfu usb-serial -pkg gw.zip -p COMx

## Cookie over USB (Open Bootloader)

Flash the Open Bootloader onto the Cookie once over SWD (see `ports/bootloader/`).
After that the board takes updates over its own USB-C and the nRF52-DK stays in
the drawer. The bootloader keeps the MBR at 0x0 and the top of flash, so the
application has to be a DFU build, linked at 0x1000:

    python cookie-thread-mesh/scripts/build_all.py --only cookie_node_sed_dfu
    docker run --rm -v "E:\projects\cookie-thread-mesh:/work" -e COOKIE_ONLY=1 \
        cookie-ports bash /work/ports/docker/build-ports.sh

The first line builds the Zephyr image (`cookie_gateway_dfu`,
`cookie_node_auto_dfu`, `cookie_node_sed_dfu`); the second builds the RIOT and
Contiki-NG images, which the ports pick up from `COOKIE_DFU=1`.

Package whatever has been built into `build/dfu/`:

    powershell -File cookie-thread-mesh/scripts/make_dfu_zips.ps1

Press SW2 to reset the board into DFU, then send the package:

    nrfutil nrf5sdk-tools dfu usb-serial -pkg build/dfu/zephyr_cookie_node_sed.zip -p COMx

`tools/nrfutil/nrfutil.exe` (6.1.7) takes the same line without `nrf5sdk-tools`.

## Monitor

    cd tools/pc_tool
    pip install -e .
    cookie-pctool live --port COMx --log run.jsonl

`COMx` is the gateway dongle's serial port.

## Layout

    apps/      sensor_node and gateway firmware
    boards/    Cookie nRF V2.00 board definition
    libs/      sensor, power and protocol helpers
    tools/     the Python monitor
