"""Synthetic frame/event generator for hardware-free development.

Emits the same JSON-line stream the gateway produces. The output is
deterministic enough to replay (RNG seed is configurable) but introduces
realistic-looking jitter on temperature, humidity, RSSI, current, and IMU
axes. One node is configured as a Cookie Router, one as a Cookie SED, one
as a Dongle SED — enough to exercise every panel of the export view.
"""

from __future__ import annotations

import argparse
import json
import math
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
    has_imu: bool
    base_temp: float
    base_humid: float
    base_i_active_ma: float
    period_s: float
    parent: str
    hops: int
    base_rssi: int


# Border router short address, the root the collector reports about. A plain
# 16-bit RLOC like the firmware prints, not a placeholder.
BR = "b41c"


def _default_nodes() -> list[StubNode]:
    """A four-node Cookie mesh under one border router.

    Shaped after the deployment this tool was written for: two always-on
    Cookie routers carrying the mesh and two sleepy Cookie nodes hanging off
    them, so every panel of the live view and the export has something to
    show. Hop counts are distances to the leader, the same convention the
    firmware reports.
    """
    return [
        # Two Cookie boards and two dongles, the border router being a third
        # dongle driven by the PC. Only the Cookies carry the SHTC3, the IMU
        # and the shunt, so only they report anything but link state, exactly
        # as the recorded hardware capture shows.
        # Currents follow the measured campaign: an always-on device sits near
        # 5.5 mA because its radio never leaves receive, a sleepy one averages
        # a few hundred microamps because it only wakes to report.
        StubNode("a1b2", "LEADER", False, True, True, 23.5, 42.0, 5.45, 5.0,
                 BR, 0, -41),
        StubNode("c3d4", "ROUTER", False, False, False, 0.0, 0.0, 0.0, 5.0,
                 "a1b2", 1, -52),
        StubNode("e5f6", "SED", True, True, True, 24.1, 45.3, 0.38, 30.0,
                 "a1b2", 1, -47),
        StubNode("9a80", "SED", True, False, False, 0.0, 0.0, 0.0, 30.0,
                 "c3d4", 2, -63),
    ]


def run(seed: int, duration_s: float | None, fast: bool) -> None:
    rng = random.Random(seed)
    nodes = _default_nodes()
    t0 = time.monotonic()

    # Parent-child tree, so the collector's topology panel shows real mesh
    # depth instead of every node hanging flat off the border router.
    tree: dict[str, list[str]] = {}
    for n in nodes:
        tree.setdefault(n.parent, []).append(n.src)
    print(json.dumps({
        "event": "topology", "ts_host": time.monotonic_ns(), "tree": tree,
    }), flush=True)

    # In fast mode the clock is virtual, otherwise a --duration of 900 would
    # take 900 real seconds and the flag would buy nothing.
    step = 0.1
    now = 0.0
    next_emit = {n.src: 0.0 for n in nodes}
    while True:
        if not fast:
            now = time.monotonic() - t0
        if duration_s is not None and now >= duration_s:
            break
        for n in nodes:
            if now < next_emit[n.src]:
                continue
            next_emit[n.src] = now + n.period_s
            frame = _emit_frame(n, rng, t0, now)
            print(json.dumps(frame), flush=True)
            # The gateway prints its console alongside the JSON. Those lines
            # are not JSON, so the parser keeps them for the log pane, which
            # is where a reader looks to see the exchange actually happening.
            for line in _console_lines(n, rng, now, frame):
                print(line, flush=True)
        if fast:
            now += step
            continue
        time.sleep(step)


def _zephyr_ts(now: float) -> str:
    """Zephyr's log timestamp, [HH:MM:SS.mmm,uuu]."""
    h, rem = divmod(now, 3600.0)
    m, s = divmod(rem, 60.0)
    frac = s - int(s)
    ms = int(frac * 1000)
    us = int(round((frac * 1000 - ms) * 1000))
    return "[%02d:%02d:%02d.%03d,%03d]" % (int(h), int(m), int(s), ms, us)


def _console_lines(n: StubNode, rng: random.Random, now: float,
                   frame: dict) -> list[str]:
    """Gateway console lines for one report, in the firmware's own format."""
    ts = _zephyr_ts(now)
    payload = 59 + rng.randint(0, 3)
    out = [
        "%s <inf> node_loop: work_handler fired, uptime=%d ms"
        % (ts, int(now * 1000)),
        "%s <inf> cookie_coap: push: CON POST /sensors/data, %d B"
        % (_zephyr_ts(now + 0.0015), payload),
    ]
    if rng.random() < 0.25:
        out.append("%s <inf> cookie_coap: gateway discovered, caching address"
                   % _zephyr_ts(now + 0.0182))
    if n.is_sed and rng.random() < 0.15:
        out.append("%s <inf> %s: radio parked, entering deep sleep"
                   % (_zephyr_ts(now + 0.0204), "sed_loop"))
    return out


def _emit_frame(n: StubNode, rng: random.Random, t0: float, now: float) -> dict[str, object]:
    f: dict[str, object] = {
        "src": n.src,
        "role": n.role,
        # Host timestamp so a --realtime replay can pace itself and the
        # per-node "last seen" ages stagger the way they do on hardware.
        "ts_host": int(now * 1_000_000_000),
        "ts": int(now * 1000),
        "rssi": int(rng.gauss(n.base_rssi, 3)),
        "hops": n.hops,
    }
    # The SHTC3 is a Cookie part. A node without one reports no temperature
    # and no humidity at all, rather than a plausible-looking invention.
    if n.is_cookie:
        f["temp_c"] = round(n.base_temp + rng.gauss(0, 0.3), 2)
        f["humid_pct"] = round(n.base_humid + rng.gauss(0, 0.5), 1)
    if n.has_imu:
        # Slow tilt component on z + jitter on x,y so the magnitude stays
        # near 1 g but each axis varies plausibly.
        tilt = math.sin(now / 8.0) * 0.05
        f["accel_g"] = [
            round(rng.gauss(0.0, 0.02), 3),
            round(rng.gauss(0.0, 0.02), 3),
            round(1.0 + tilt + rng.gauss(0.0, 0.01), 3),
        ]
        f["gyro_dps"] = [
            round(rng.gauss(0.0, 0.5), 2),
            round(rng.gauss(0.0, 0.5), 2),
            round(rng.gauss(0.0, 0.5), 2),
        ]
    # Every Cookie carries the shunt and INA333, so every Cookie reports its
    # own current, router or sleepy alike. A dongle has no sense path and
    # reports none. The always-on nodes sit at their standing radio current,
    # the sleepy ones at their duty-cycled average, which is the contrast the
    # whole measurement is about.
    if n.is_cookie:
        f["i_avg_ma"] = round(max(0.05, rng.gauss(n.base_i_active_ma,
                                                  n.base_i_active_ma * 0.06)), 2)
        # Transmit peak on this SoC, matching the recorded captures.
        f["i_pk_ma"] = round(rng.gauss(26.0, 1.2), 1)
        f["t_active_ms"] = int(max(20, rng.gauss(85, 8)))
        # Battery sensing rides the same analogue front end, so it is a Cookie
        # field too. A dongle contributes link state and nothing else.
        f["vbat_mv"] = int(rng.gauss(2940, 15))
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
