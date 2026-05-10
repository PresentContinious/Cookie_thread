"""Mesh state derived from the frame/event stream.

Maintains one row per node (the most recent values), a running list of
historical samples per node for export, the current parent-child topology
tree (from `topology` events), and a marker log. All host timestamps are
nanoseconds since an arbitrary monotonic origin — the export converts them
to seconds-since-first-frame for human-readable axes.
"""

from __future__ import annotations

import time
from dataclasses import dataclass, field
from typing import Any

from .frames import Event, Frame, LogLine, Record


@dataclass
class NodeRow:
    """Most-recent observed state of one node, as shown in the live TUI."""

    src: str
    role: str = "?"
    rssi_dbm: int | None = None
    hops: int | None = None
    temp_c: float | None = None
    humid_pct: float | None = None
    t_active_ms: int | None = None
    accel_g: tuple[float, float, float] | None = None
    gyro_dps: tuple[float, float, float] | None = None
    i_avg_ma: float | None = None
    i_pk_ma: float | None = None
    vbat_mv: int | None = None
    last_seen_ns: int = 0


@dataclass
class Marker:
    tag: str
    ts_host_ns: int
    note: str = ""


@dataclass
class MeshState:
    """All cumulative state derived from a stream of records."""

    nodes: dict[str, NodeRow] = field(default_factory=dict)
    history: dict[str, list[Frame]] = field(default_factory=dict)
    topology: dict[str, list[str]] = field(default_factory=dict)
    markers: list[Marker] = field(default_factory=list)
    log_lines: list[LogLine] = field(default_factory=list)
    log_capacity: int = 200
    started_ns: int = 0

    def feed(self, rec: Record) -> None:
        if self.started_ns == 0:
            self.started_ns = rec.ts_host_ns
        if isinstance(rec, Frame):
            self._on_frame(rec)
        elif isinstance(rec, Event):
            self._on_event(rec)
        elif isinstance(rec, LogLine):
            self._on_log(rec)

    def _on_frame(self, frame: Frame) -> None:
        row = self.nodes.setdefault(frame.src, NodeRow(src=frame.src))
        row.role = frame.role
        if frame.rssi_dbm is not None:
            row.rssi_dbm = frame.rssi_dbm
        if frame.hops is not None:
            row.hops = frame.hops
        if frame.temp_c is not None:
            row.temp_c = frame.temp_c
        if frame.humid_pct is not None:
            row.humid_pct = frame.humid_pct
        if frame.t_active_ms is not None:
            row.t_active_ms = frame.t_active_ms
        if frame.accel_g is not None:
            row.accel_g = frame.accel_g
        if frame.gyro_dps is not None:
            row.gyro_dps = frame.gyro_dps
        if frame.i_avg_ma is not None:
            row.i_avg_ma = frame.i_avg_ma
        if frame.i_pk_ma is not None:
            row.i_pk_ma = frame.i_pk_ma
        if frame.vbat_mv is not None:
            row.vbat_mv = frame.vbat_mv
        row.last_seen_ns = frame.ts_host_ns
        self.history.setdefault(frame.src, []).append(frame)

    def _on_event(self, event: Event) -> None:
        if event.name == "topology":
            tree = event.payload.get("tree")
            if isinstance(tree, dict):
                clean: dict[str, list[str]] = {}
                for parent, children in tree.items():
                    if isinstance(children, list):
                        clean[str(parent)] = [str(c) for c in children]
                self.topology = clean
        elif event.name == "node_lost":
            src = event.payload.get("src")
            if isinstance(src, str) and src in self.nodes:
                self.nodes[src].role = "LOST"
        elif event.name == "marker":
            tag = str(event.payload.get("tag", "?"))
            note = str(event.payload.get("note", ""))
            self.markers.append(Marker(tag=tag, ts_host_ns=event.ts_host_ns, note=note))

    def _on_log(self, line: LogLine) -> None:
        self.log_lines.append(line)
        if len(self.log_lines) > self.log_capacity:
            self.log_lines = self.log_lines[-self.log_capacity:]

    def add_marker(self, tag: str, note: str = "") -> Marker:
        m = Marker(tag=tag, ts_host_ns=time.monotonic_ns(), note=note)
        self.markers.append(m)
        return m

    def render_topology_ascii(self) -> str:
        if not self.topology:
            return "(no topology event yet)"
        roots = self._topology_roots()
        out: list[str] = []
        for root in roots:
            self._draw_subtree(root, "", True, out)
        return "\n".join(out)

    def _topology_roots(self) -> list[str]:
        children_set: set[str] = set()
        for cs in self.topology.values():
            children_set.update(cs)
        return [p for p in self.topology if p not in children_set] or list(self.topology.keys())[:1]

    def _draw_subtree(self, node: str, prefix: str, is_last: bool, out: list[str]) -> None:
        connector = "└── " if is_last else "├── "
        role = self.nodes.get(node, NodeRow(src=node)).role
        out.append(f"{prefix}{connector}{node} [{role}]")
        children = self.topology.get(node, [])
        new_prefix = prefix + ("    " if is_last else "│   ")
        for i, child in enumerate(children):
            self._draw_subtree(child, new_prefix, i == len(children) - 1, out)


@dataclass
class BatteryProjection:
    """Closed-form projection from average active current and a sleep-current
    measurement supplied externally (e.g. PPK2 reading via CLI flag)."""

    i_active_ma: float
    t_active_ms: float
    t_cycle_ms: float
    i_sleep_ua: float
    capacity_mah: float = 220.0

    def average_current_ma(self) -> float:
        active_frac = self.t_active_ms / self.t_cycle_ms if self.t_cycle_ms else 0
        sleep_frac = max(0.0, 1.0 - active_frac)
        return self.i_active_ma * active_frac + (self.i_sleep_ua / 1000.0) * sleep_frac

    def days(self) -> float:
        avg = self.average_current_ma()
        if avg <= 0:
            return float("inf")
        hours = self.capacity_mah / avg
        return hours / 24.0
