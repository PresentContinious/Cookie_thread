# Cookie Thread Mesh

Firmware tree for the Cookie nRF V2.00 sensor-node Thread mesh demonstrator (UPM TFM, Dmytro Kysil).

## Layout

```
cookie-thread-mesh/
├── apps/
│   ├── sensor_node/   # AUTO + SED profiles, builds for Cookie and Dongle
│   └── gateway/       # CoAP server + USB-CDC raw passthrough, Dongle only
├── boards/cei/cookie_nrf_v200/   # HWMv2 board definition
├── libs/
│   ├── cookie_proto/   # frame schema + JSON codec + CoAP push client
│   ├── cookie_sensors/ # SHTC3 wrapper (ICM-20648 added in next session)
│   └── cookie_power/   # populated by sub-project #3 (INA333 ADC pipeline)
└── scripts/build_all.py
```

## Build

Initial setup (once):

```
west init -l cookie-thread-mesh
west update
```

Build all four variants in one go (run from the workspace root):

```
python cookie-thread-mesh/scripts/build_all.py
```

Or one at a time with `west` directly. From the workspace root:

| Variant | Command |
|---------|---------|
| `cookie_node_auto`  | `west build -b cookie_nrf_v200/nrf52840 -d build/cookie_node_auto cookie-thread-mesh/apps/sensor_node -- -DEXTRA_CONF_FILE=overlays/profile_auto.conf` |
| `dongle_gateway`    | `west build -b nrf52840dongle/nrf52840 -d build/dongle_gateway   cookie-thread-mesh/apps/gateway` |
| `dongle_node_auto`  | `west build -b nrf52840dongle/nrf52840 -d build/dongle_node_auto cookie-thread-mesh/apps/sensor_node -- -DEXTRA_CONF_FILE=overlays/profile_auto.conf` |
| `dongle_node_sed`   | `west build -b nrf52840dongle/nrf52840 -d build/dongle_node_sed  cookie-thread-mesh/apps/sensor_node -- -DEXTRA_CONF_FILE=overlays/profile_sed.conf` |

Add `-p` (pristine) on the first build of a variant or after changing Kconfig.

## Flash

### Cookie (when hardware arrives)

Use the nRF52-DK's J-Link via TC2050 SWD. From the workspace root:

```
west flash -d build/cookie_node_auto --runner jlink
```

### Dongle (now)

The Dongle uses the in-built nRF Util / nRF52 USB DFU bootloader. After putting the Dongle into bootloader mode (push the small reset button until the red LED pulses):

```
nrfutil pkg generate --hw-version 52 --sd-req=0x00 \
    --application build/dongle_gateway/zephyr/zephyr.hex \
    --application-version 1 dongle_gateway.zip
nrfutil dfu usb-serial -pkg dongle_gateway.zip -p COMx
```

(Use the COM port of the Dongle in bootloader mode.)

## Test recipes (three Dongles in hand)

### Setup A — minimal end-to-end

1. Flash Dongle #1 with `dongle_gateway`. After enumeration it appears on the host as a CDC ACM serial port (`COMx` on Windows).
2. Flash Dongle #2 with `dongle_node_auto`.
3. Open the gateway's COM port at any baud (USB-CDC ignores the rate).
4. Within ~10 s a JSON line arrives every 5 s:

```
{"ts":12345,"src":"a1b2","role":"CHILD","rssi":-37,"hops":1}
```

(`temp_c` and `humid_pct` are absent — the Dongle has no SHTC3.)

### Setup B — role election

1. Same as Setup A, then flash Dongle #3 with `dongle_node_auto` as well.
2. Both nodes appear in the stream with distinct `src` values.
3. After a few seconds, Thread elects roles. The `role` field on at least one node may transition (CHILD → REED → ROUTER, depending on topology).

### Setup C — SED behavior

1. Flash Dongle #1 with `dongle_gateway`.
2. Flash Dongle #2 with `dongle_node_auto` (acts as a parent for the SED).
3. Flash Dongle #3 with `dongle_node_sed`.
4. SED frames arrive every 30 s with `t_active_ms` < 200.

### Setup D — gateway recovery

1. While Setup A is running, power-cycle the gateway Dongle (unplug + replug).
2. Sensor nodes' CON unicast retransmits exhaust within ~30 s, the cached gateway is invalidated, and the next push rediscovers via multicast.
3. Stream resumes within ~10 s of the gateway re-attaching to the mesh.

## Specs

- System architecture: `code_documentation/specs/2026-05-02-thread-system-architecture-design.md`
- Repo skeleton (sub-project #1, done): `code_documentation/specs/2026-05-02-repo-skeleton-design.md`
- Sensor-node firmware (this work): `code_documentation/specs/2026-05-03-sensor-node-firmware-design.md`
