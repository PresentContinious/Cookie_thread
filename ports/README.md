# Cross-OS ports of the Cookie sensor node

These ports exist for the TFM's central experiment: run the **same sensor-node
workload** — read the sensors, transmit the reading over the radio, sleep — on
the **same nRF52840 hardware** under three operating systems, and measure the
energy cost of each.

Each OS has a **sensor** (sends) and a **gateway/collector** (receives), so the
protocol can be observed with two Cookie boards and no Dongle.

| OS | sensor / gateway | Cookie board | Network stack | Status |
|----|------------------|--------------|---------------|--------|
| Zephyr (baseline) | `../apps/sensor_node`, `../apps/gateway` | in-tree | OpenThread (Thread mesh) | verified on hardware, measured |
| RIOT | `riot/sensor_node`, `riot/gateway` | `riot/boards/cookie_nrf_v200` | GNRC 6LoWPAN | verified on hardware, measured |
| Contiki-NG | `contiki-ng/sensor_node`, `contiki-ng/gateway` | `contiki-ng/board-cookie` (CI copies into the Contiki tree) | uIP 6LoWPAN/RPL | verified on hardware, measured |

All three took part in the July 2026 measurement campaign (see `MEASUREMENT.md`).
CI: `.github/workflows/ci-os-ports.yml` →
download `.hex` from the run artifacts, or build locally with `docker/`, and
flash from Windows with `nrfjprog`.

## The shared contract

Every port emits the **same single-line JSON frame** and POSTs it to the CoAP
resource `sensors/data`, so the OS-agnostic PC tool (`tools/pc_tool`) ingests
all three unchanged. Required keys: `ts, src, role, rssi, hops`. Optional:
`temp_c, humid_pct, t_active_ms, ...`. Each non-Zephyr port adds an additive
`"os"` tag (`"riot"`, `"contiki-ng"`); the schema is additive, so the Zephyr
parser and the PC tool ignore or preserve unknown keys.

The ports do **not** interoperate on one network — RIOT and Contiki-NG run
their own 6LoWPAN stacks, not Thread. The comparison is the same *workload* per
OS, which is what the energy measurement needs. The differing network stack is
itself one axis of the comparison.

## Build environment

RIOT and Contiki-NG are Linux-first (Make + POSIX) and do **not** build on
native Windows. Build the `.hex` in the Docker image (`docker/`), in WSL/Ubuntu
or in CI, and flash the board from Windows with `nrfjprog`. Per-port
build/flash commands are in each port's `README.md`.
