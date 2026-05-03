# cookie-pctool

Live monitor and post-mortem exporter for the Cookie Thread mesh demonstrator.

Reads JSON-line frames over USB-CDC from the gateway Dongle, displays a live
node table + ASCII topology + marker log in the terminal, and exports a
self-contained Plotly HTML report from any captured log file. A built-in stub
generator produces synthetic frames for hardware-free development.

## Install

```
pip install -e .[dev]
```

## Run

Live mode (USB-CDC):

```
cookie-pctool live --port COM5 --log run.jsonl
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
  "i_avg_ma": 6.4, "i_pk_ma": 51.0, "vbat_mv": 2940 }
```

Events use `"event": "<name>"` and a payload that depends on the event:

```json
{ "event": "node_joined", "src": "a1b2", "ts_host": 17304... }
{ "event": "topology", "tree": { "0000": ["a1b2", "c3d4"] }, "ts_host": ... }
{ "event": "marker", "tag": "x", "ts_host": ... }
```
