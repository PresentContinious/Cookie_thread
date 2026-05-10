"""Live Rich TUI: node table on top, ASCII topology in the middle, log + markers
at the bottom. Markers are inserted by pressing a single letter key.

The TUI runs the source iterator on a worker thread so keypress polling and
screen refresh stay responsive even with a slow serial port. The state object
is updated in place; Rich's `Live` redraws at a fixed 4 Hz rate.
"""

from __future__ import annotations

import sys
import threading
import time
from queue import Empty, Queue
from typing import Iterator

from rich.console import Console
from rich.layout import Layout
from rich.live import Live
from rich.panel import Panel
from rich.table import Table
from rich.text import Text

from .frames import parse_line
from .state import MeshState, NodeRow

REFRESH_HZ = 4.0


def run_tui(
    src_iter: Iterator[tuple[int, str]],
    state: MeshState,
    log_path: str | None,
) -> None:
    log_fp = open(log_path, "a", encoding="utf-8") if log_path else None
    console = Console()
    queue: Queue[tuple[int, str]] = Queue(maxsize=4096)

    stop = threading.Event()
    reader = threading.Thread(target=_reader_thread, args=(src_iter, queue, stop), daemon=True)
    reader.start()

    key_thread = threading.Thread(target=_key_thread, args=(state, log_fp, stop), daemon=True)
    key_thread.start()

    layout = _build_layout()
    try:
        with Live(layout, console=console, refresh_per_second=REFRESH_HZ, screen=True):
            while not stop.is_set():
                _drain(queue, state, log_fp)
                _render(layout, state)
                time.sleep(1.0 / REFRESH_HZ)
    except KeyboardInterrupt:
        stop.set()
    finally:
        stop.set()
        if log_fp:
            log_fp.close()


def _reader_thread(src: Iterator[tuple[int, str]], queue: Queue, stop: threading.Event) -> None:
    try:
        for ts, line in src:
            if stop.is_set():
                break
            try:
                queue.put((ts, line), timeout=0.5)
            except Exception:
                pass
    except Exception as exc:
        queue.put((time.monotonic_ns(), f'{{"event":"error","msg":"{exc!r}"}}'))
        stop.set()


def _key_thread(state: MeshState, log_fp, stop: threading.Event) -> None:
    """Read single keypresses without echo. On Windows uses msvcrt; on POSIX
    falls back to termios cbreak."""
    try:
        if sys.platform == "win32":
            import msvcrt
            while not stop.is_set():
                if msvcrt.kbhit():
                    ch = msvcrt.getwch()
                    _handle_key(ch, state, log_fp, stop)
                else:
                    time.sleep(0.05)
        else:
            import termios, tty, select
            fd = sys.stdin.fileno()
            old = termios.tcgetattr(fd)
            tty.setcbreak(fd)
            try:
                while not stop.is_set():
                    r, _, _ = select.select([sys.stdin], [], [], 0.05)
                    if r:
                        ch = sys.stdin.read(1)
                        _handle_key(ch, state, log_fp, stop)
            finally:
                termios.tcsetattr(fd, termios.TCSADRAIN, old)
    except Exception:
        pass


def _handle_key(ch: str, state: MeshState, log_fp, stop: threading.Event) -> None:
    if ch in ("q", "Q", "\x03"):
        stop.set()
        return
    if ch.isalpha() and len(ch) == 1:
        m = state.add_marker(tag=ch.lower())
        if log_fp is not None:
            import json
            log_fp.write(json.dumps({"event": "marker", "tag": m.tag, "ts_host": m.ts_host_ns}) + "\n")
            log_fp.flush()


def _drain(queue: Queue, state: MeshState, log_fp) -> None:
    while True:
        try:
            ts, line = queue.get_nowait()
        except Empty:
            return
        if log_fp is not None:
            log_fp.write(line + "\n")
        rec = parse_line(line, ts_host_ns=ts)
        state.feed(rec)


def _build_layout() -> Layout:
    layout = Layout()
    layout.split(
        Layout(name="nodes", size=14),
        Layout(name="topology", size=12),
        Layout(name="bottom"),
    )
    layout["bottom"].split_row(Layout(name="markers"), Layout(name="log"))
    return layout


def _render(layout: Layout, state: MeshState) -> None:
    layout["nodes"].update(Panel(_render_nodes(state), title="Nodes", border_style="cyan"))
    layout["topology"].update(Panel(state.render_topology_ascii(), title="Topology", border_style="green"))
    layout["markers"].update(Panel(_render_markers(state), title="Markers", border_style="magenta"))
    layout["log"].update(Panel(_render_log(state), title="Log", border_style="yellow"))


def _accel_mag(row: NodeRow) -> float | None:
    if row.accel_g is None:
        return None
    ax, ay, az = row.accel_g
    return (ax * ax + ay * ay + az * az) ** 0.5


def _render_nodes(state: MeshState) -> Table:
    t = Table(expand=True)
    for col in ("src", "role", "T", "RH", "|a|", "i_avg", "i_pk", "vbat", "rssi", "hops", "last"):
        t.add_column(col)
    now_ns = time.monotonic_ns()
    for src in sorted(state.nodes):
        row = state.nodes[src]
        age = (now_ns - row.last_seen_ns) / 1e9 if row.last_seen_ns else 0
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
            f"{age:5.1f} s ago" if row.last_seen_ns else "—",
        )
    return t


def _render_markers(state: MeshState) -> Text:
    if not state.markers:
        return Text("(press a–z to insert a marker; q to quit)", style="dim")
    lines = []
    for m in state.markers[-12:]:
        rel = (m.ts_host_ns - state.started_ns) / 1e9 if state.started_ns else 0
        lines.append(f"[{m.tag}]  +{rel:7.2f} s   {m.note}")
    return Text("\n".join(lines))


def _render_log(state: MeshState) -> Text:
    if not state.log_lines:
        return Text("(no log lines yet)", style="dim")
    return Text("\n".join(line.text for line in state.log_lines[-12:]))


def _fmt(value, fmt: str) -> str:
    return fmt.format(value) if value is not None else "—"
