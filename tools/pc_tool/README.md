# cookie-pctool

Live monitor and post-mortem exporter for the Cookie Thread mesh demonstrator.

Reads JSON-line frames over USB-CDC (Dongle gateway) or hardware UART (Cookie
gateway), displays a live node table + ASCII topology + marker log in the
terminal, and exports a self-contained Plotly HTML report from any captured
log file. A built-in stub generator produces synthetic frames for
hardware-free development.

## Install

```
pip install -e .[dev]
```

## Run

Live mode (Cookie gateway over UART, or Dongle gateway over USB-CDC):

```
cookie-pctool live --port COM7 --log run.jsonl
```

Stub mode (no hardware):

```
cookie-pctool stub | cookie-pctool live --stdin --log run.jsonl
```

Export a previously recorded log:

```
cookie-pctool export run.jsonl --out chapter6.html
```

While in live mode press a letter key (`a`-`z`) to insert a marker; the marker
is timestamped, written to the log file, and shown on every Plotly subplot at
export time.

## Frame schema

Frames are JSON objects, one per line. The schema is forward-compatible: any
unknown field is preserved and ignored, any missing optional field is treated
as absent. The fields below are documented in `apps/sensor_node/`.

```json
{ "ts": 1234567, "src": "a1b2", "role": "LEADER",
  "rssi": -67, "hops": 2,
  "temp_c": 24.31, "humid_pct": 41.2, "t_active_ms": 87,
  "accel_g":  [0.012, -0.018, 0.998],
  "gyro_dps": [0.31, -0.12, 0.04],
  "i_avg_ma": 6.4, "i_pk_ma": 51.0, "vbat_mv": 2940 }
```

| Field         | Source                | Notes                                       |
|---------------|-----------------------|---------------------------------------------|
| `ts`          | node uptime ms        | always present                              |
| `src`         | EUI-64 last 4 hex     | always present                              |
| `role`        | OpenThread role       | "LEADER" / "ROUTER" / "CHILD" / "SED"       |
| `rssi`        | parent link RSSI      | 0 if Leader / no parent                     |
| `hops`        | derived from role     | 0 Leader, 1 Router, 2 Child                 |
| `temp_c`      | SHTC3                 | Cookie only                                 |
| `humid_pct`   | SHTC3                 | Cookie only                                 |
| `t_active_ms` | SED loop              | wake-window duration (SED only)             |
| `accel_g`     | ICM-20648             | Cookie only; X/Y/Z in g                     |
| `gyro_dps`    | ICM-20648             | Cookie only; X/Y/Z in deg/s                 |
| `i_avg_ma`    | INA333 + SAADC ch6    | Cookie only; mean current over the burst    |
| `i_pk_ma`     | INA333 + SAADC ch6    | Cookie only; peak current over the burst    |
| `vbat_mv`     | NRF_SAADC_VDD         | Cookie only; battery voltage                |

Events use `"event": "<name>"` and a payload that depends on the event:

```json
{ "event": "node_joined", "src": "a1b2", "ts_host": 17304... }
{ "event": "topology", "tree": { "0000": ["a1b2", "c3d4"] }, "ts_host": ... }
{ "event": "marker", "tag": "x", "ts_host": ... }
```

## Tests

```
pytest -v
```

20 tests cover frame parsing (happy path, missing optionals, malformed JSON,
forward-compat extras, IMU array parsing), MeshState updates (telemetry plus
IMU values, topology events, node-lost, markers), battery projection, and
end-to-end stub-to-export.
