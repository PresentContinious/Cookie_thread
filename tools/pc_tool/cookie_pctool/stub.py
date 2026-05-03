"""Synthetic frame/event generator for hardware-free development.

Emits the same JSON-line stream the gateway produces. The output is
deterministic enough to replay (RNG seed is configurable) but introduces
realistic-looking jitter on temperature, humidity, RSSI, and current. One
node is configured as a Cookie SED, one as a Dongle SED, one as a Cookie
Router — enough to exercise every panel of the export view.
"""

from __future__ import annotations

import argparse
import json
import random
import sys
import time
from dataclasses import dataclass


@dataclass
class StubNode:
    src: str
    role: str
    is_sed: bool
    is_cookie: bool
    base_temp: float
    base_humid: float
    base_i_active_ma: float
    period_s: float


def _default_nodes() -> list[StubNode]:
    return [
        StubNode("a1b2", "ROUTER", False, True, 23.5, 42.0, 9.0, 5.0),
        StubNode("c3d4", "SED", True, True, 24.1, 45.3, 6.4, 30.0),
        StubNode("e5f6", "SED", True, False, 24.0, 45.0, 11.2, 30.0),
    ]


def run(seed: int, duration_s: float | None, fast: bool) -> None:
    rng = random.Random(seed)
    nodes = _default_nodes()
    t0 = time.monotonic()

    print(json.dumps({
        "event": "topology", "ts_host": time.monotonic_ns(),
        "tree": {"0000": [n.src for n in nodes]},
    }), flush=True)

    next_emit = {n.src: 0.0 for n in nodes}
    while True:
        now = time.monotonic() - t0
        if duration_s is not None and now >= duration_s:
            break
        for n in nodes:
            if now < next_emit[n.src]:
                continue
            next_emit[n.src] = now + n.period_s
            frame = _emit_frame(n, rng, t0)
            print(json.dumps(frame), flush=True)
        if fast:
            continue
        time.sleep(0.1)


def _emit_frame(n: StubNode, rng: random.Random, t0: float) -> dict[str, object]:
    f: dict[str, object] = {
        "src": n.src,
        "role": n.role,
        "ts": int((time.monotonic() - t0) * 1000),
        "rssi": int(rng.gauss(-65, 5)),
        "hops": 1,
        "temp_c": round(n.base_temp + rng.gauss(0, 0.3), 2),
        "humid_pct": round(n.base_humid + rng.gauss(0, 0.5), 1),
    }
    if n.is_sed:
        f["t_active_ms"] = int(max(20, rng.gauss(85, 8)))
        f["i_avg_ma"] = round(rng.gauss(n.base_i_active_ma, 0.3), 2)
        f["i_pk_ma"] = round(rng.gauss(48 if n.is_cookie else 60, 3), 1)
    f["vbat_mv"] = int(rng.gauss(2940 if n.is_cookie else 2780, 15))
    return f


def main() -> int:
    p = argparse.ArgumentParser(description="Stub JSON-line generator for cookie-pctool.")
    p.add_argument("--seed", type=int, default=42)
    p.add_argument("--duration", type=float, default=None,
                   help="Stop after this many seconds of simulated runtime.")
    p.add_argument("--fast", action="store_true",
                   help="Do not pace output; useful when piping to file.")
    args = p.parse_args()
    try:
        run(args.seed, args.duration, args.fast)
    except KeyboardInterrupt:
        return 130
    return 0


if __name__ == "__main__":
    sys.exit(main())
