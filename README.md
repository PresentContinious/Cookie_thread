# cookie-thread-mesh

Firmware for the Cookie nRF V2.00 sensor node — Thread mesh demonstrator.
UPM TFM, Dmytro Kysil, CEI ETSI Industriales.

## Repository layout

```
cookie-thread-mesh/
├── apps/
│   ├── sensor_node/   # AUTO and SED profiles, builds for Cookie and Dongle
│   └── gateway/       # CoAP server + USB-CDC passthrough, Dongle only
├── boards/cei/cookie_nrf_v200/   # HWMv2 board definition
├── libs/
│   ├── cookie_proto/   # frame schema, JSON codec, CoAP push client
│   ├── cookie_sensors/ # SHTC3 and ICM-20648 wrappers
│   └── cookie_power/   # INA333 ADC pipeline
└── scripts/build_all.py
```

## Requirements

- nRF Connect SDK v3.3.0
- Zephyr RTOS (pulled by west)
- west >= 1.2

## Setup

```
west init -l cookie-thread-mesh
west update
```

## Build

All four variants in one command (from the workspace root):

```
python cookie-thread-mesh/scripts/build_all.py
```

Or individually with west:

| Variant | Command |
|---------|---------|
| `cookie_node_auto`  | `west build -b cookie_nrf_v200/nrf52840 -d build/cookie_node_auto cookie-thread-mesh/apps/sensor_node -- -DEXTRA_CONF_FILE=overlays/profile_auto.conf` |
| `dongle_gateway`    | `west build -b nrf52840dongle/nrf52840 -d build/dongle_gateway cookie-thread-mesh/apps/gateway` |
| `dongle_node_auto`  | `west build -b nrf52840dongle/nrf52840 -d build/dongle_node_auto cookie-thread-mesh/apps/sensor_node -- -DEXTRA_CONF_FILE=overlays/profile_auto.conf` |
| `dongle_node_sed`   | `west build -b nrf52840dongle/nrf52840 -d build/dongle_node_sed cookie-thread-mesh/apps/sensor_node -- -DEXTRA_CONF_FILE=overlays/profile_sed.conf` |

Add `-p` on the first build or after changing Kconfig.

## Flash

### Cookie (TC2050 SWD via nRF52-DK J-Link)

```
west flash -d build/cookie_node_auto --runner jlink
```

### Dongle (USB DFU bootloader)

Put the Dongle into bootloader mode (press reset until the red LED pulses), then:

```
nrfutil pkg generate --hw-version 52 --sd-req=0x00 \
    --application build/dongle_gateway/zephyr/zephyr.hex \
    --application-version 1 dongle_gateway.zip
nrfutil dfu usb-serial -pkg dongle_gateway.zip -p COMx
```

## Test setup (three Dongles)

**Minimal end-to-end:** flash Dongle #1 as `dongle_gateway`, Dongle #2 as `dongle_node_auto`. Open the gateway's COM port — JSON frames arrive every 5 s:

```json
{"ts":12345,"src":"a1b2","role":"CHILD","rssi":-37,"hops":1}
```

**SED behavior:** flash Dongle #3 as `dongle_node_sed`. Frames arrive every 30 s with `t_active_ms` < 200.

**Gateway recovery:** power-cycle the gateway while nodes are running. Nodes rediscover via multicast within ~10 s of the gateway reattaching.
