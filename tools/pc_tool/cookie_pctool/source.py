"""Line sources: serial, stdin, recorded JSONL file.

Each source is an iterable of `(host_ns, raw_line)` tuples. Higher layers
parse the raw line and attach state. Sources are non-blocking-friendly: the
serial source uses a short timeout so the TUI can poll keypresses between
reads, and the file source can replay at recorded cadence or as fast as the
host can drain it.
"""

from __future__ import annotations

import sys
import time
from pathlib import Path
from typing import Iterator


def from_serial(port: str, baud: int = 115200, timeout: float = 0.1) -> Iterator[tuple[int, str]]:
    """Yield lines from a USB-CDC port. Empty reads are skipped silently."""
    import serial  # imported lazily so the stub/export paths don't require pyserial

    ser = serial.Serial(port, baudrate=baud, timeout=timeout)
    try:
        buf = bytearray()
        while True:
            chunk = ser.read(256)
            if not chunk:
                continue
            buf.extend(chunk)
            while b"\n" in buf:
                line, _, rest = buf.partition(b"\n")
                buf = bytearray(rest)
                yield (time.monotonic_ns(), line.decode("utf-8", errors="replace"))
    finally:
        ser.close()


def from_stdin() -> Iterator[tuple[int, str]]:
    """Yield lines from stdin (used when piped from a stub generator)."""
    for line in sys.stdin:
        yield (time.monotonic_ns(), line.rstrip("\n"))


def from_file(path: Path, realtime: bool = False) -> Iterator[tuple[int, str]]:
    """Replay a recorded JSONL file.

    With `realtime=False` (default) lines are yielded as fast as possible and
    the host timestamp is the current monotonic clock — used by the export
    command. With `realtime=True` the producer paces yields to match the
    original ts_host gaps — used to demo the TUI without hardware.
    """
    if not realtime:
        with path.open("r", encoding="utf-8") as f:
            for line in f:
                yield (time.monotonic_ns(), line.rstrip("\n"))
        return

    last_orig: int | None = None
    last_wall: float | None = None
    with path.open("r", encoding="utf-8") as f:
        for line in f:
            stripped = line.rstrip("\n")
            now = time.monotonic_ns()
            yield (now, stripped)
            orig = _extract_ts_host_ns(stripped)
            if orig is not None and last_orig is not None and last_wall is not None:
                elapsed = (orig - last_orig) / 1e9
                wall_now = time.monotonic()
                target = last_wall + elapsed
                if target > wall_now:
                    time.sleep(target - wall_now)
                last_wall = target
            else:
                last_wall = time.monotonic()
            if orig is not None:
                last_orig = orig


def _extract_ts_host_ns(line: str) -> int | None:
    import json
    try:
        obj = json.loads(line)
    except json.JSONDecodeError:
        return None
    val = obj.get("ts_host") if isinstance(obj, dict) else None
    return int(val) if isinstance(val, (int, float)) else None
