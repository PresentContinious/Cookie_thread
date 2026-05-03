"""MeshState integration tests — feed records, check derived state."""

from cookie_pctool.frames import parse_line
from cookie_pctool.state import BatteryProjection, MeshState


def feed(state: MeshState, lines: list[str]) -> None:
    ts = 1_000_000_000
    for line in lines:
        state.feed(parse_line(line, ts_host_ns=ts))
        ts += 1_000_000_000


def test_node_row_carries_last_values():
    state = MeshState()
    feed(state, [
        '{"src":"a1b2","role":"LEADER","temp_c":23.0,"humid_pct":40.0}',
        '{"src":"a1b2","role":"LEADER","temp_c":23.6,"humid_pct":41.4,"vbat_mv":2940}',
    ])
    row = state.nodes["a1b2"]
    assert row.role == "LEADER"
    assert row.temp_c == 23.6
    assert row.humid_pct == 41.4
    assert row.vbat_mv == 2940
    assert len(state.history["a1b2"]) == 2


def test_topology_event_updates_tree():
    state = MeshState()
    feed(state, [
        '{"event":"topology","tree":{"0000":["a1b2","c3d4"]}}',
    ])
    assert state.topology == {"0000": ["a1b2", "c3d4"]}
    ascii_tree = state.render_topology_ascii()
    assert "0000" in ascii_tree
    assert "a1b2" in ascii_tree
    assert "c3d4" in ascii_tree


def test_marker_recorded():
    state = MeshState()
    feed(state, ['{"src":"a1b2","role":"SED"}'])
    state.add_marker("x", note="test")
    assert state.markers[-1].tag == "x"
    assert state.markers[-1].note == "test"


def test_node_lost_event():
    state = MeshState()
    feed(state, [
        '{"src":"a1b2","role":"CHILD"}',
        '{"event":"node_lost","src":"a1b2","reason":"timeout"}',
    ])
    assert state.nodes["a1b2"].role == "LOST"


def test_battery_projection_basic_math():
    proj = BatteryProjection(
        i_active_ma=10.0,
        t_active_ms=100.0,
        t_cycle_ms=30_000.0,
        i_sleep_ua=1.5,
        capacity_mah=220.0,
    )
    avg = proj.average_current_ma()
    # 10 mA * (100/30000) + 1.5 µA * (1 - 100/30000) ≈ 0.0333 + 0.0015 mA
    assert 0.034 < avg < 0.036
    days = proj.days()
    assert 250 < days < 280


def test_battery_projection_zero_cycle_safe():
    proj = BatteryProjection(
        i_active_ma=10.0, t_active_ms=0.0, t_cycle_ms=0.0,
        i_sleep_ua=0.0, capacity_mah=220.0,
    )
    assert proj.average_current_ma() == 0.0
    assert proj.days() == float("inf")
