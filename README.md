# Cookie Thread Mesh

Thread mesh firmware for the Cookie nRF V2.00 board and nRF52840 Dongle.
Built with nRF Connect SDK v3.3 / Zephyr RTOS.

## Structure

- `apps/sensor_node` — sensor node, AUTO and SED profiles
- `apps/gateway` — CoAP gateway with USB-CDC output
- `boards/cei/cookie_nrf_v200` — board definition for Cookie V2.00
- `libs/cookie_proto` — frame format and CoAP client
- `libs/cookie_sensors` — SHTC3 and ICM-20648 drivers

## Build

```
west init -l cookie-thread-mesh
west update
python cookie-thread-mesh/scripts/build_all.py
```

## Flash

Cookie (SWD via nRF52-DK):
```
west flash -d build/cookie_node_auto --runner jlink
```

Dongle (USB DFU):
```
nrfutil pkg generate --hw-version 52 --sd-req=0x00 \
    --application build/dongle_gateway/zephyr/zephyr.hex \
    --application-version 1 out.zip
nrfutil dfu usb-serial -pkg out.zip -p COMx
```
