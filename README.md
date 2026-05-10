# Cookie Thread Mesh

Thread-mesh sensor node firmware for the Cookie nRF V2.00 board and the
nRF52840 USB Dongle. Built with nRF Connect SDK v3.3 / Zephyr RTOS.

This repository carries the firmware that supports the thesis demonstration
"Design and Implementation of Energy Management Techniques in Smart Embedded
IoT Devices" — a self-organising 802.15.4 mesh that pushes per-node sensor
frames over CoAP to a single gateway, which streams them as JSON-lines to a
desktop tool (Rich TUI live + Plotly post-mortem).

## Topology

The headline demonstration uses a single hardware platform end-to-end:

| Role        | Hardware                | Image                | Notes                                           |
|-------------|-------------------------|----------------------|-------------------------------------------------|
| Gateway     | Cookie nRF V2.00 #1     | `cookie_gateway`     | FTD; Leader-by-default; UART console for the PC |
| Sensor node | Cookie nRF V2.00 #2,#3  | `cookie_node_auto`   | FTD, AUTO profile; full SHTC3 + ICM-20648 + INA333 telemetry |
| Sensor node | Cookie nRF V2.00 (any)  | `cookie_node_sed`    | MTD-SED, sleep cycle; headline graph for Chapter 6 |
| Sensor node | nRF52840 Dongle x3      | `dongle_node_sed`    | MTD-SED; mesh-scale demo; no on-board sensors   |

The Dongle gateway image (`dongle_gateway`) is kept buildable for fallback
testing but is not part of the demo: a known SYS_INIT clash between OpenThread
and USB-CDC ACM blocks bring-up. The Cookie's hardware UART avoids the issue.

## Build

From the west workspace root (the directory that contains `nrf/`, `zephyr/`,
`cookie-thread-mesh/`):

```
python cookie-thread-mesh/scripts/build_all.py            # incremental
python cookie-thread-mesh/scripts/build_all.py --pristine # clean rebuild
python cookie-thread-mesh/scripts/build_all.py --only cookie_gateway
```

Six output directories appear under `build/`:

```
build/cookie_gateway     build/cookie_node_auto    build/cookie_node_sed
build/dongle_gateway     build/dongle_node_auto    build/dongle_node_sed
```

Each contains `merged.hex` (mcuboot + application slot 0) and
`<app>/zephyr/zephyr.elf` for SWD use.

## Flash

### Cookie nRF V2.00 (TC2050 SWD via nRF52-DK as J-Link probe)

Wire the TC2050 probe between the nRF52-DK's debug-out header and the Cookie's
J1 connector, then:

```
west flash -d build/cookie_gateway   --runner jlink     # gateway / Leader
west flash -d build/cookie_node_auto --runner jlink     # AUTO sensor node
west flash -d build/cookie_node_sed  --runner jlink     # SED sensor node
```

The first flash also writes mcuboot. Subsequent application updates can use
the same command (the `merged.hex` always re-flashes mcuboot too — slow but
deterministic).

### nRF52840 Dongle (USB DFU)

Hold the Dongle's RESET button while plugging it in to enter Open Bootloader,
then:

```
nrfutil pkg generate --hw-version 52 --sd-req=0x00 \
    --application build/dongle_node_sed/sensor_node/zephyr/zephyr.hex \
    --application-version 1 dongle_node_sed.zip

nrfutil dfu usb-serial -pkg dongle_node_sed.zip -p COMx
```

Replace `COMx` with the Dongle's bootloader COM port (visible in Device
Manager as "Open DFU Bootloader").

## PC tool

```
cd tools/pc_tool
python -m venv .venv && .venv\Scripts\activate
pip install -e .[dev]
pytest                                     # 20 tests, ~3 s
```

Live monitor against the Cookie gateway's UART (whatever USB-to-serial bridge
is wired to UART0 — typically the nRF52-DK's VCOM if J-Link stays attached):

```
cookie-pctool live --port COM7 --log run.jsonl
```

Press `a`-`z` while the TUI is up to insert a timestamped marker in the log;
the marker appears as a vertical line on every Plotly subplot at export time.

Render a self-contained HTML report from any captured log:

```
cookie-pctool export run.jsonl --out chapter6.html
```

Hardware-free development uses the synthetic generator:

```
cookie-pctool stub --duration 30 --fast > demo.jsonl
cookie-pctool live --replay demo.jsonl --realtime
```

## Frame schema

Frames are JSON objects, one per line on the gateway's console. The schema is
additive: unknown fields are preserved by the PC-tool parser, missing optional
fields are treated as absent.

```json
{ "ts": 1234567, "src": "a1b2", "role": "LEADER",
  "rssi": -67, "hops": 2,
  "temp_c": 24.31, "humid_pct": 41.2,
  "t_active_ms": 87,
  "accel_g":  [0.012, -0.018, 0.998],
  "gyro_dps": [0.31, -0.12, 0.04],
  "i_avg_ma": 6.40, "i_pk_ma": 51.0, "vbat_mv": 2940 }
```

| Field         | Producer             | Notes                                       |
|---------------|----------------------|---------------------------------------------|
| `ts`          | `k_uptime_get_32()`  | node-side milliseconds                      |
| `src`         | EUI-64 last 4 hex    | stable per-flash node identity              |
| `role`        | `otThreadGetDeviceRole` | or `"SED"` on MTD-SED builds              |
| `rssi`        | parent link RSSI     | 0 if Leader / no parent                     |
| `hops`        | derived from role    | 0 Leader, 1 Router, 2 Child                 |
| `temp_c`      | SHTC3                | Cookie only                                 |
| `humid_pct`   | SHTC3                | Cookie only                                 |
| `t_active_ms` | SED loop             | wake-window duration                        |
| `accel_g`     | ICM-20648            | Cookie only; X, Y, Z in g                   |
| `gyro_dps`    | ICM-20648            | Cookie only; X, Y, Z in deg/s               |
| `i_avg_ma`    | INA333 + SAADC ch6   | mean current over the burst window          |
| `i_pk_ma`     | INA333 + SAADC ch6   | peak current observed in the burst window   |
| `vbat_mv`     | NRF_SAADC_VDD ÷5     | battery voltage                              |

Control events use `"event": "<name>"`:

```json
{ "event": "topology", "tree": { "0000": ["a1b2", "c3d4"] } }
{ "event": "node_lost", "src": "a1b2", "reason": "timeout" }
{ "event": "marker",   "tag": "x", "ts_host": 17304... }
```

## Repository layout

```
apps/
    sensor_node/         # one source tree, AUTO or SED via overlays/
    gateway/             # CoAP server + raw passthrough to console
boards/
    cei/cookie_nrf_v200/ # HWMv2 board definition + DTS for Cookie V2.00
dts/
    bindings/sensor/     # invensense,icm20648 binding (no upstream driver)
libs/
    cookie_proto/        # frame schema, JSON codec, CoAP client
    cookie_sensors/      # SHTC3 + ICM-20648 wrappers
    cookie_power/        # INA333 + VBAT pipeline (SAADC bursts)
scripts/
    build_all.py         # multi-variant build helper
tools/
    pc_tool/             # cookie-pctool: TUI + Plotly export + stub generator
zephyr/
    module.yml           # makes this repo a Zephyr module (board_root + dts_root)
```

## Tuning knobs

| Kconfig                                  | Default    | Effect                                      |
|------------------------------------------|------------|---------------------------------------------|
| `CONFIG_NODE_REPORT_INTERVAL_SEC`        | 5 / 30     | sensor cadence (AUTO / SED)                 |
| `CONFIG_OPENTHREAD_CHANNEL`              | 25         | shared mesh radio channel                   |
| `CONFIG_OPENTHREAD_NETWORK_NAME`         | CookieMesh | shared mesh name                            |
| `CONFIG_COOKIE_POWER_BURST_SAMPLES`      | 32         | ADC samples per (i_avg, i_pk) cycle         |
| `CONFIG_COOKIE_POWER_SHUNT_MOHM`         | 100        | sense resistor in milliohms                 |
| `CONFIG_COOKIE_POWER_INA333_GAIN`        | 101        | 1 + 100 k / Rg                              |
| `CONFIG_COOKIE_PROTO_FRAME_BUF_SIZE`     | 384        | upper bound on encoded frame size           |
| `CONFIG_COOKIE_PROTO_DISCOVERY_TIMEOUT_MS` | 1500     | gateway-discovery NON wait                  |

For lab override, drop a `prj_lab.conf` next to `prj.conf` (gitignored) and
add `-DEXTRA_CONF_FILE=prj_lab.conf` to the build invocation.
