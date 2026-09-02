"""Static snapshot renderer: replay a recorded JSONL capture through the real
parse/state pipeline and print one clean, screenshot-ready frame of the run.

Same data path as the live TUI (parse_line -> MeshState.feed). Every value
shown comes from the capture file. The presentation differs from the live view
only in hygiene:

* device-log lines are printed with their ANSI colour codes stripped (the raw
  gateway console embeds Zephyr's ``\\x1b[0m`` sequences, which are terminal
  styling, not content);
* the log tail keeps only complete timestamped lines (a serial capture can cut
  a line mid-byte at the moment the capture stops);
* marker keys that are not plain letters (key held down, dead keys) are summed
  as unlabelled instead of being printed raw.
"""

from __future__ import annotations

import re
import sys
from collections import Counter
from pathlib import Path

from rich import box
from rich.console import Console, Group
from rich.panel import Panel
from rich.rule import Rule
from rich.table import Table
from rich.text import Text

from .frames import Event, Frame, LogLine, parse_line
from .state import MeshState, NodeRow

# CSI sequences plus any stray ESC byte left over from a cut sequence.
_ANSI_RE = re.compile(r"\x1b\[[0-9;]*[A-Za-z]|\x1b")
# A complete Zephyr console line: "[HH:MM:SS.mmm,uuu] <lvl> module: message"
_LOG_RE = re.compile(r"^\[\d{2}:\d{2}:\d{2}\.\d{3},\d{3}\]\s+<\w+>\s+\S+.+$")


def _accel_mag(row: NodeRow) -> float | None:
    if row.accel_g is None:
        return None
    ax, ay, az = row.accel_g
    return (ax * ax + ay * ay + az * az) ** 0.5


def _fmt(value, fmt: str) -> str:
    return fmt.format(value) if value is not None else "—"


def render_snapshot(log: Path, tail: int = 14, width: int = 118) -> int:
    counts = Counter()
    state = MeshState()
    ts = 0
    with open(log, "r", encoding="utf-8", errors="replace") as fp:
        for raw in fp:
            line = raw.rstrip("\r\n")
            if not line.strip():
                continue
            ts += 1_000_000  # synthetic monotonic ns; replay order is what matters
            rec = parse_line(line, ts_host_ns=ts)
            if isinstance(rec, Frame):
                counts["frames"] += 1
            elif isinstance(rec, Event):
                counts["events"] += 1
                if rec.name == "marker":
                    counts["markers"] += 1
            elif isinstance(rec, LogLine):
                counts["logs"] += 1
            state.feed(rec)

    console = Console(width=width, highlight=False)
    total = counts["frames"] + counts["events"] + counts["logs"]

    console.print(Rule(f"[bold]cookie-pctool[/bold]  —  live-view snapshot (replay of recorded capture)"))
    console.print(f"source capture : {log.name}")
    console.print(
        f"lines ingested : {total}   (frames={counts['frames']}, events={counts['events']}, "
        f"device-log lines={counts['logs']}, markers={counts['markers']})"
    )
    console.print(f"nodes observed : {len(state.nodes)}   ->  {', '.join(sorted(state.nodes))}")
    console.print()

    console.print(Panel(_nodes_table(state), title="Nodes (last value per node)",
                        border_style="cyan", box=box.SQUARE))
    console.print(Panel(_topology(state), title="Topology (from last topology event)",
                        border_style="green", box=box.SQUARE))
    console.print(Panel(_markers(state), title=f"Markers inserted during capture ({counts['markers']} total)",
                        border_style="magenta", box=box.SQUARE))
    console.print(Panel(_log_tail(state, tail), title="Device-log tail (gateway console, colour codes stripped)",
                        border_style="yellow", box=box.SQUARE))
    console.print(Rule("end of snapshot"))
    return 0


def _nodes_table(state: MeshState) -> Table:
    t = Table(expand=True, box=box.SQUARE, header_style="bold")
    for col in ("src", "role", "T", "RH", "|a|", "i_avg", "i_pk", "vbat", "rssi", "hops"):
        t.add_column(col)
    for src in sorted(state.nodes):
        row = state.nodes[src]
        t.add_row(
            row.src,
            row.role,
            _fmt(row.temp_c, "{:.2f} C"),
            _fmt(row.humid_pct, "{:.1f} %"),
            _fmt(_accel_mag(row), "{:.2f} g"),
            _fmt(row.i_avg_ma, "{:.2f} mA"),
            _fmt(row.i_pk_ma, "{:.1f} mA"),
            _fmt(row.vbat_mv, "{} mV"),
            _fmt(row.rssi_dbm, "{} dBm"),
            _fmt(row.hops, "{}"),
        )
    return t


def _topology(state: MeshState) -> Text:
    if not state.topology:
        return Text("(no topology event in capture)", style="dim")
    in_tree: set[str] = set(state.topology)
    for cs in state.topology.values():
        in_tree.update(cs)

    out: list[str] = []
    roots = [p for p in state.topology
             if p not in {c for cs in state.topology.values() for c in cs}]
    for root in roots or list(state.topology)[:1]:
        _draw(state, root, "", True, out, is_root=True)
    text = Text("\n".join(out))
    stragglers = sorted(set(state.nodes) - in_tree)
    for s in stragglers:
        role = state.nodes[s].role
        text.append(f"\n{s} [{role}]", style="default")
        text.append("  — observed earlier in the run, absent from the last topology event",
                    style="dim")
    return text


def _draw(state: MeshState, node: str, prefix: str, is_last: bool, out: list[str],
          is_root: bool = False) -> None:
    connector = "└── " if is_last else "├── "
    role = state.nodes.get(node, NodeRow(src=node)).role
    if is_root and role == "?":
        # the topology event is emitted by the collector about its children
        role = "GATEWAY"
    out.append(f"{prefix}{connector}{node} [{role}]")
    children = state.topology.get(node, [])
    new_prefix = prefix + ("    " if is_last else "│   ")
    for i, child in enumerate(children):
        _draw(state, child, new_prefix, i == len(children) - 1, out)


def _markers(state: MeshState) -> Text:
    if not state.markers:
        return Text("(none)", style="dim")
    tags = Counter(m.tag for m in state.markers)
    labelled = [(t, n) for t, n in sorted(tags.items()) if t.isascii() and t.isalpha()]
    junk = sum(n for t, n in tags.items() if not (t.isascii() and t.isalpha()))
    parts = [f"'{t}' x{n}" for t, n in labelled]
    text = Text(", ".join(parts) if parts else "")
    if junk:
        if parts:
            text.append(", ")
        text.append(f"{junk} unlabelled keypresses", style="dim")
    return text


def _log_tail(state: MeshState, tail: int) -> Text:
    clean: list[str] = []
    for line in state.log_lines:
        s = _ANSI_RE.sub("", line.text).strip()
        if _LOG_RE.match(s):
            clean.append(s)
    if not clean:
        return Text("(no device-log lines in capture)", style="dim")
    return Text("\n".join(clean[-tail:]))
