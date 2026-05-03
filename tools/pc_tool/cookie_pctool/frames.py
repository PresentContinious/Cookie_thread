"""Frame and event parsing for JSON-lines emitted by the Cookie gateway.

The gateway forwards each sensor-node frame as one JSON object per line on
USB-CDC. The same line discipline is used for control events (node_joined,
node_lost, topology snapshot, marker). Anything that is not valid JSON or does
not look like a frame/event is silently passed back as a `LogLine` so the TUI
can show device-side log noise without crashing.
"""

from __future__ import annotations

import json
import time
from dataclasses import dataclass, field
from typing import Any, Iterable


@dataclass
class Frame:
    """One sensor-node report. Fields not present in the JSON stay None."""

    src: str
    role: str
    ts_node_ms: int
    ts_host_ns: int
    rssi_dbm: int | None = None
    hops: int | None = None
    temp_c: float | None = None
    humid_pct: float | None = None
    t_active_ms: int | None = None
    i_avg_ma: float | None = None
    i_pk_ma: float | None = None
    vbat_mv: int | None = None
    extras: dict[str, Any] = field(default_factory=dict)


@dataclass
class Event:
    """Control event from the gateway or PC tool itself (markers)."""

    name: str
    ts_host_ns: int
    payload: dict[str, Any] = field(default_factory=dict)


@dataclass
class LogLine:
    """Raw line we could not parse as frame or event. Kept for the TUI log pane."""

    text: str
    ts_host_ns: int


Record = Frame | Event | LogLine


_FRAME_REQUIRED = ("src", "role")
"""A JSON object that lacks these keys is treated as a non-frame log line."""


def parse_line(line: str, ts_host_ns: int | None = None) -> Record:
    """Parse a single line into Frame / Event / LogLine.

    `ts_host_ns` is the host timestamp in nanoseconds; when omitted the current
    monotonic clock is used. The host timestamp is what every plot in the
    export uses as the X axis, so it must be filled by the producer (live
    reader, file replay) consistently.
    """
    if ts_host_ns is None:
        ts_host_ns = time.monotonic_ns()
    s = line.strip()
    if not s:
        return LogLine("", ts_host_ns)
    if s[0] != "{":
        return LogLine(s, ts_host_ns)
    try:
        obj = json.loads(s)
    except json.JSONDecodeError:
        return LogLine(s, ts_host_ns)

    if not isinstance(obj, dict):
        return LogLine(s, ts_host_ns)

    if "event" in obj:
        name = str(obj["event"])
        payload = {k: v for k, v in obj.items() if k != "event"}
        return Event(name=name, ts_host_ns=ts_host_ns, payload=payload)

    if all(k in obj for k in _FRAME_REQUIRED):
        return _frame_from_obj(obj, ts_host_ns)

    return LogLine(s, ts_host_ns)


def _frame_from_obj(obj: dict[str, Any], ts_host_ns: int) -> Frame:
    known = {
        "src", "role", "ts", "rssi", "hops",
        "temp_c", "humid_pct", "t_active_ms",
        "i_avg_ma", "i_pk_ma", "vbat_mv",
    }
    extras = {k: v for k, v in obj.items() if k not in known}
    return Frame(
        src=str(obj["src"]),
        role=str(obj["role"]),
        ts_node_ms=int(obj.get("ts", 0)),
        ts_host_ns=ts_host_ns,
        rssi_dbm=_opt_int(obj.get("rssi")),
        hops=_opt_int(obj.get("hops")),
        temp_c=_opt_float(obj.get("temp_c")),
        humid_pct=_opt_float(obj.get("humid_pct")),
        t_active_ms=_opt_int(obj.get("t_active_ms")),
        i_avg_ma=_opt_float(obj.get("i_avg_ma")),
        i_pk_ma=_opt_float(obj.get("i_pk_ma")),
        vbat_mv=_opt_int(obj.get("vbat_mv")),
        extras=extras,
    )


def _opt_int(v: Any) -> int | None:
    return int(v) if v is not None else None


def _opt_float(v: Any) -> float | None:
    return float(v) if v is not None else None


def parse_lines(lines: Iterable[str]) -> Iterable[Record]:
    """Convenience wrapper for replay scenarios (one host timestamp per line)."""
    for line in lines:
        yield parse_line(line)
