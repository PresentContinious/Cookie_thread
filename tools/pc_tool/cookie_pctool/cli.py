"""CLI entry-point: dispatches to live, stub, or export."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from . import stub
from .export import export_log
from .source import from_file, from_serial, from_stdin
from .state import MeshState
from .tui import run_tui


def main() -> int:
    p = argparse.ArgumentParser(prog="cookie-pctool")
    sub = p.add_subparsers(dest="cmd", required=True)

    p_live = sub.add_parser("live", help="Live monitoring TUI from a port, stdin, or replay file")
    src = p_live.add_mutually_exclusive_group(required=True)
    src.add_argument("--port", help="USB-CDC serial port (e.g. COM5 or /dev/ttyACM0)")
    src.add_argument("--stdin", action="store_true", help="Read JSON-lines from stdin")
    src.add_argument("--replay", type=Path, help="Replay a recorded JSONL file")
    p_live.add_argument("--baud", type=int, default=115200)
    p_live.add_argument("--log", type=Path, default=None,
                        help="Append every received line to this log file")
    p_live.add_argument("--realtime", action="store_true",
                        help="When replaying a file, pace yields to original cadence")

    p_export = sub.add_parser("export", help="Render a Plotly HTML report from a JSONL log")
    p_export.add_argument("log", type=Path, help="Recorded JSONL file")
    p_export.add_argument("--out", type=Path, default=Path("report.html"))
    p_export.add_argument("--all-roles", action="store_true",
                          help="Comparative panel: include all roles, not only SED")

    p_stub = sub.add_parser("stub", help="Emit a synthetic JSON-line stream on stdout")
    p_stub.add_argument("--seed", type=int, default=42)
    p_stub.add_argument("--duration", type=float, default=None)
    p_stub.add_argument("--fast", action="store_true")

    p_snap = sub.add_parser(
        "snapshot",
        help="Replay a recorded JSONL capture and print one clean static frame",
    )
    p_snap.add_argument("log", type=Path, help="Recorded JSONL file")
    p_snap.add_argument("--tail", type=int, default=14,
                        help="Device-log lines to show at the bottom")
    p_snap.add_argument("--width", type=int, default=118)

    args = p.parse_args()

    if args.cmd == "live":
        return _cmd_live(args)
    if args.cmd == "export":
        export_log(args.log, args.out, sed_only_compare=not args.all_roles)
        print(f"wrote {args.out}", file=sys.stderr)
        return 0
    if args.cmd == "stub":
        stub.run(args.seed, args.duration, args.fast)
        return 0
    if args.cmd == "snapshot":
        from .snapshot import render_snapshot
        return render_snapshot(args.log, tail=args.tail, width=args.width)
    return 2


def _cmd_live(args: argparse.Namespace) -> int:
    if args.port:
        src = from_serial(args.port, baud=args.baud)
    elif args.stdin:
        src = from_stdin()
    else:
        src = from_file(args.replay, realtime=args.realtime)
    state = MeshState()
    log = str(args.log) if args.log else None
    run_tui(src, state, log)
    return 0


if __name__ == "__main__":
    sys.exit(main())
