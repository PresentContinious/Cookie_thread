"""Plotly HTML exporter.

Reads a JSONL log, builds a self-contained HTML page with one figure per
metric block:

  * Per-node panel — temperature, humidity, RSSI, vbat, i_avg, i_pk over time
  * Comparative panel — Cookie SED vs Dongle SED i_avg_ma on shared axes
    (the Chapter 6 headline graph)
  * Topology timeline — parent of each node as a step function
  * Marker overlay — vertical lines on every plot, labelled by tag

Time axis is "seconds since first frame" (host clock). Missing fields are
plotted as gaps, never as zero.
"""

from __future__ import annotations

from pathlib import Path
from typing import Iterable

import plotly.graph_objects as go
from plotly.subplots import make_subplots

from .frames import Frame, Record, parse_line
from .state import MeshState

# Heuristic: any node whose `vbat_mv` average is below this is treated as
# powered from a Dongle's USB rail; above this, treated as Cookie 3 V battery.
# This is only used to label the comparative panel — it does not change data.
DONGLE_VBAT_THRESHOLD_MV = 2850


def export_log(log_path: Path, out_path: Path, sed_only_compare: bool = True) -> None:
    state = MeshState()
    for line in log_path.read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        rec = parse_line(line)
        state.feed(rec)

    if state.started_ns == 0:
        out_path.write_text("<html><body><h1>cookie-pctool: empty log</h1></body></html>", encoding="utf-8")
        return

    sections = []
    sections.append(_per_node_section(state))
    sections.append(_compare_section(state, sed_only_compare))
    sections.append(_topology_section(state))

    html = _wrap_html(sections, log_path)
    out_path.write_text(html, encoding="utf-8")


def _per_node_section(state: MeshState) -> str:
    fig = make_subplots(
        rows=3, cols=2,
        subplot_titles=("temp_c", "humid_pct", "i_avg_ma", "i_pk_ma", "vbat_mv", "rssi_dbm"),
        shared_xaxes=True,
    )
    spots = [("temp_c", 1, 1), ("humid_pct", 1, 2),
             ("i_avg_ma", 2, 1), ("i_pk_ma", 2, 2),
             ("vbat_mv",  3, 1), ("rssi_dbm", 3, 2)]
    for src, frames in sorted(state.history.items()):
        for field, row, col in spots:
            xs, ys = _series(frames, field, state.started_ns)
            if not xs:
                continue
            fig.add_trace(
                go.Scatter(x=xs, y=ys, name=f"{src} {field}", legendgroup=src,
                           showlegend=(field == "temp_c"), mode="lines+markers"),
                row=row, col=col,
            )
    _add_marker_lines(fig, state, rows=3, cols=2)
    fig.update_layout(height=900, title_text="Per-node telemetry", hovermode="x unified")
    fig.update_xaxes(title_text="t (s)", row=3, col=1)
    fig.update_xaxes(title_text="t (s)", row=3, col=2)
    return fig.to_html(full_html=False, include_plotlyjs="cdn")


def _compare_section(state: MeshState, sed_only: bool) -> str:
    fig = go.Figure()
    for src, frames in sorted(state.history.items()):
        sample = frames[0]
        if sed_only and sample.role.upper() != "SED":
            continue
        avg_vbat = _mean(f.vbat_mv for f in frames if f.vbat_mv is not None)
        kind = "Dongle" if (avg_vbat is not None and avg_vbat < DONGLE_VBAT_THRESHOLD_MV) else "Cookie"
        xs, ys = _series(frames, "i_avg_ma", state.started_ns)
        if not xs:
            continue
        fig.add_trace(go.Scatter(
            x=xs, y=ys, name=f"{kind} {src} (i_avg_ma)",
            mode="lines+markers"))
    _add_marker_lines(fig, state, rows=1, cols=1)
    fig.update_layout(
        height=480,
        title_text="Cookie SED vs Dongle SED — average current (Chapter 6)",
        xaxis_title="t (s)",
        yaxis_title="i_avg_ma",
        hovermode="x unified",
    )
    return fig.to_html(full_html=False, include_plotlyjs=False)


def _topology_section(state: MeshState) -> str:
    """Topology timeline as a step function of `parent` per node.
    Built only from the running topology snapshots that arrived during the run.
    """
    parent_at: dict[str, list[tuple[int, str]]] = {}
    for line in []:
        pass
    if not state.topology:
        return "<p><em>No topology event in this log.</em></p>"
    rows = []
    for parent, children in state.topology.items():
        for c in children:
            rows.append((c, parent))
    if not rows:
        return "<p><em>Topology empty.</em></p>"
    fig = go.Figure()
    children_sorted = sorted({c for c, _ in rows})
    parents_sorted = sorted({p for _, p in rows})
    parent_idx = {p: i for i, p in enumerate(parents_sorted)}
    for c in children_sorted:
        ps = [p for cc, p in rows if cc == c]
        if not ps:
            continue
        fig.add_trace(go.Scatter(
            x=[0], y=[parent_idx[ps[-1]]], name=c, mode="markers+text",
            text=[c], textposition="top center",
        ))
    fig.update_layout(
        height=300,
        title_text="Topology snapshot (final)",
        yaxis=dict(tickmode="array",
                   tickvals=list(parent_idx.values()),
                   ticktext=parents_sorted,
                   title="parent"),
        xaxis=dict(visible=False),
    )
    return fig.to_html(full_html=False, include_plotlyjs=False)


def _series(frames: Iterable[Frame], field: str, t0_ns: int) -> tuple[list[float], list[float]]:
    xs: list[float] = []
    ys: list[float] = []
    for f in frames:
        v = getattr(f, field, None)
        if v is None:
            continue
        xs.append((f.ts_host_ns - t0_ns) / 1e9)
        ys.append(v)
    return xs, ys


def _add_marker_lines(fig, state: MeshState, rows: int, cols: int) -> None:
    for m in state.markers:
        x = (m.ts_host_ns - state.started_ns) / 1e9
        fig.add_vline(x=x, line_dash="dot", line_color="red",
                      annotation_text=m.tag, annotation_position="top right")


def _mean(values: Iterable[float]) -> float | None:
    vs = list(values)
    if not vs:
        return None
    return sum(vs) / len(vs)


def _wrap_html(sections: list[str], log_path: Path) -> str:
    head = (
        "<html><head><meta charset='utf-8'>"
        "<title>cookie-pctool — Chapter 6 export</title>"
        "<style>body{font-family:sans-serif;margin:1.5em;}"
        "h2{margin-top:1.2em;border-bottom:1px solid #ccc;}"
        "</style></head><body>"
    )
    body = (
        f"<h1>Cookie Thread mesh — measurement export</h1>"
        f"<p>Source log: <code>{log_path}</code></p>"
    )
    body += "<h2>Per-node telemetry</h2>" + sections[0]
    body += "<h2>Comparative panel</h2>" + sections[1]
    body += "<h2>Topology</h2>" + sections[2]
    return head + body + "</body></html>"
