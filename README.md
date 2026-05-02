# Cookie Thread Mesh

Firmware tree for the Cookie nRF V2.00 sensor-node Thread mesh demonstrator (UPM TFM, Dmytro Kysil).

## Layout

- `apps/sensor_node` — sensor-bearing node (Cookie + Dongle), profiles FTD/MED/SED via Kconfig.
- `apps/gateway` — USB-CDC ACM gateway (Dongle only).
- `boards/cei/cookie_nrf_v200` — Zephyr HWMv2 board definition for the V2.00 PCB.
- `libs/cookie_proto`, `libs/cookie_sensors`, `libs/cookie_power` — populated by later sub-projects.

## Build

```
west init -l cookie-thread-mesh
west update
west build -p -b cookie_nrf_v200 cookie-thread-mesh/apps/sensor_node -- -DCONFIG_NODE_PROFILE_FTD=y
```

See `code_documentation/specs/2026-05-02-thread-system-architecture-design.md` for the system-level architecture.
