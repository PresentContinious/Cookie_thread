"""End-to-end smoke test: stub generator -> file -> export -> HTML on disk."""

import io
import json
import sys
from pathlib import Path

from cookie_pctool import stub
from cookie_pctool.export import export_log


def _write_stub_log(tmp_path: Path) -> Path:
    log = tmp_path / "stub.jsonl"
    captured = io.StringIO()
    saved = sys.stdout
    sys.stdout = captured
    try:
        stub.run(seed=7, duration_s=2.0, fast=True)
    finally:
        sys.stdout = saved
    log.write_text(captured.getvalue(), encoding="utf-8")
    return log


def test_stub_to_export_pipeline(tmp_path: Path):
    log = _write_stub_log(tmp_path)
    assert log.stat().st_size > 0
    parsed = [json.loads(l) for l in log.read_text(encoding="utf-8").splitlines() if l.strip()]
    assert any(p.get("src") == "a1b2" for p in parsed)
    assert any(p.get("event") == "topology" for p in parsed)

    out = tmp_path / "report.html"
    export_log(log, out, sed_only_compare=True)
    html = out.read_text(encoding="utf-8")
    assert "<html>" in html
    assert "Per-node telemetry" in html
    assert "Comparative panel" in html


def test_export_empty_log_does_not_crash(tmp_path: Path):
    log = tmp_path / "empty.jsonl"
    log.write_text("", encoding="utf-8")
    out = tmp_path / "report.html"
    export_log(log, out)
    assert "empty log" in out.read_text(encoding="utf-8")
