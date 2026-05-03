#!/usr/bin/env python3
"""
Build all four firmware variants in one go.

Works from any shell: if `west` isn't on PATH and ZEPHYR_BASE isn't set,
the script wraps each build with `nrfutil toolchain-manager launch
--ncs-version <ver> -- ...`, which is what the nRF Connect VS Code
extension does internally to set up the toolchain environment.

Usage from the workspace root (the directory containing the
cookie-thread-mesh repo as a sibling of nrf/, zephyr/, ...):

    python cookie-thread-mesh/scripts/build_all.py
    python cookie-thread-mesh/scripts/build_all.py --pristine
    python cookie-thread-mesh/scripts/build_all.py --only dongle_node_auto
    python cookie-thread-mesh/scripts/build_all.py --ncs-version v3.3.0
"""

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
APPS = REPO / "apps"

VARIANTS = [
    {
        "name":    "cookie_node_auto",
        "app":     APPS / "sensor_node",
        "board":   "cookie_nrf_v200/nrf52840",
        "extra":   ["overlays/profile_auto.conf"],
        "comment": "Cookie sensor node, AUTO profile (compile only — no Cookie HW yet).",
    },
    {
        "name":    "dongle_gateway",
        "app":     APPS / "gateway",
        "board":   "nrf52840dongle/nrf52840",
        "extra":   [],
        "comment": "Stub gateway on Dongle: CoAP server + USB-CDC raw passthrough.",
    },
    {
        "name":    "dongle_node_auto",
        "app":     APPS / "sensor_node",
        "board":   "nrf52840dongle/nrf52840",
        "extra":   ["overlays/profile_auto.conf"],
        "comment": "Dongle sensor node, AUTO profile.",
    },
    {
        "name":    "dongle_node_sed",
        "app":     APPS / "sensor_node",
        "board":   "nrf52840dongle/nrf52840",
        "extra":   ["overlays/profile_sed.conf"],
        "comment": "Dongle sensor node, SED profile.",
    },
]


# --- toolchain location -----------------------------------------------------

def find_west():
    """Return absolute path to west, or None."""
    w = shutil.which("west")
    if w:
        return w
    if os.name == "nt":
        ncs_root = Path(os.environ.get("NRF_CONNECT_SDK_ROOT", r"C:\ncs"))
        tcs = ncs_root / "toolchains"
        if tcs.exists():
            for tc in tcs.iterdir():
                for cand in [
                    tc / "opt" / "bin" / "Scripts" / "west.exe",
                    tc / "opt" / "bin" / "west.exe",
                    tc / "Scripts" / "west.exe",
                    tc / "python" / "Scripts" / "west.exe",
                ]:
                    if cand.exists():
                        return str(cand)
    return None


def find_nrfutil():
    """Return absolute path to nrfutil, or None."""
    n = shutil.which("nrfutil")
    if n:
        return n
    if os.name == "nt":
        # Standalone installs
        for cand in [
            Path(r"C:\Program Files\nrfutil\bin\nrfutil.exe"),
            Path(r"C:\Program Files\nordicsemi\nrfutil\nrfutil.exe"),
            Path(os.path.expandvars(r"%LOCALAPPDATA%\nrfutil\bin\nrfutil.exe")),
            Path(os.path.expandvars(r"%LOCALAPPDATA%\Programs\nrfutil\bin\nrfutil.exe")),
        ]:
            if cand.exists():
                return str(cand)
        # Bundled in NCS toolchains
        ncs_root = Path(os.environ.get("NRF_CONNECT_SDK_ROOT", r"C:\ncs"))
        tcs = ncs_root / "toolchains"
        if tcs.exists():
            for tc in tcs.iterdir():
                for cand in [
                    tc / "opt" / "bin" / "nrfutil.exe",
                    tc / "bin" / "nrfutil.exe",
                    tc / "nrfutil.exe",
                ]:
                    if cand.exists():
                        return str(cand)
    return None


def detect_env(ncs_version: str):
    """Decide how to invoke build commands.
    Returns (prefix_cmd, west_cmd, mode_str).

    Important: a west.exe found inside an NCS toolchain folder is NOT
    callable directly — it is a Python entry-point launcher that needs
    PATH, ZEPHYR_BASE and the toolchain's bundled Python set up first.
    Only `nrfutil toolchain-manager launch` does that correctly."""

    # 1) west on PATH (e.g. user prepended toolchain to PATH manually,
    #    or launched from the nRF Connect terminal). Trust it.
    if shutil.which("west"):
        return [], "west", "direct (west on PATH)"

    # 2) Wrap with nrfutil toolchain-manager (preferred when nothing else
    #    is set up).
    nrfutil = find_nrfutil()
    if nrfutil:
        return ([nrfutil, "toolchain-manager", "launch",
                 "--ncs-version", ncs_version, "--"],
                "west",
                f"wrapped via nrfutil toolchain-manager (NCS {ncs_version})")

    return None, None, None


# --- build invocation -------------------------------------------------------

def run(cmd, env=None):
    print(f"+ {' '.join(str(c) for c in cmd)}")
    return subprocess.run(cmd, env=env, check=False)


def build_one(v, build_root: Path, pristine: bool, prefix, west_cmd) -> int:
    out = build_root / v["name"]
    if pristine and out.exists():
        shutil.rmtree(out)

    # Pass BOARD_ROOT / DTS_ROOT explicitly so sysbuild finds custom boards
    # even when our top-level zephyr/module.yml hasn't been picked up
    # (e.g. west list cache stale).
    cmake_args = [
        "-DBOARD_ROOT=" + str(REPO),
        "-DDTS_ROOT="   + str(REPO),
    ]
    for e in v["extra"]:
        cmake_args.append("-DEXTRA_CONF_FILE=" + e)

    base = [west_cmd, "build",
            "-b", v["board"],
            "-d", str(out),
            str(v["app"])]
    if pristine:
        base.append("-p")
    base += ["--", *cmake_args]

    return run(prefix + base).returncode


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pristine", action="store_true")
    ap.add_argument("--only", action="append", default=[])
    ap.add_argument("--build-dir", default="build")
    ap.add_argument("--ncs-version", default="v3.3.0",
                    help="passed to nrfutil toolchain-manager (default: v3.3.0)")
    args = ap.parse_args()

    prefix, west_cmd, mode = detect_env(args.ncs_version)
    if west_cmd is None:
        print("ERROR: cannot find west or nrfutil.", file=sys.stderr)
        print("Either:", file=sys.stderr)
        print("  1) Open the nRF Connect Toolchain Manager and click", file=sys.stderr)
        print("     'Open Command Prompt' to launch a shell with the env", file=sys.stderr)
        print("     set, then re-run this script from there.", file=sys.stderr)
        print("  2) OR ensure 'nrfutil' is on PATH (install nRF Connect for", file=sys.stderr)
        print("     Desktop) so this script can wrap builds with", file=sys.stderr)
        print("     'nrfutil toolchain-manager launch'.", file=sys.stderr)
        return 2
    print(f"toolchain mode: {mode}")
    print()

    build_root = Path(args.build_dir).resolve()
    build_root.mkdir(parents=True, exist_ok=True)

    selected = VARIANTS
    if args.only:
        names = set(args.only)
        unknown = names - {v["name"] for v in VARIANTS}
        if unknown:
            print(f"unknown variant(s): {sorted(unknown)}", file=sys.stderr)
            return 2
        selected = [v for v in VARIANTS if v["name"] in names]

    failed = []
    for v in selected:
        print()
        print("=" * 72)
        print(f"BUILD  {v['name']}  ({v['comment']})")
        print("=" * 72)
        rc = build_one(v, build_root, args.pristine, prefix, west_cmd)
        if rc != 0:
            failed.append(v["name"])

    print()
    print("=" * 72)
    print("SUMMARY")
    print("=" * 72)
    for v in selected:
        status = "FAIL" if v["name"] in failed else "ok"
        print(f"  [{status:>4}]  {v['name']:<22}  {v['board']}")
    print()
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
