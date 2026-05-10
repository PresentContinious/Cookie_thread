"""Frame parser smoke tests — covers happy path, missing optionals, malformed
JSON, non-JSON log lines, and unknown-extra-fields forward-compat."""

from cookie_pctool.frames import Event, Frame, LogLine, parse_line


def test_parse_full_frame():
    line = (
        '{"src":"a1b2","role":"LEADER","ts":1234,"rssi":-67,"hops":2,'
        '"temp_c":24.31,"humid_pct":41.2,"t_active_ms":87,'
        '"accel_g":[0.01,-0.02,0.99],"gyro_dps":[0.3,-0.1,0.05],'
        '"i_avg_ma":6.4,"i_pk_ma":51.0,"vbat_mv":2940}'
    )
    rec = parse_line(line, ts_host_ns=1000)
    assert isinstance(rec, Frame)
    assert rec.src == "a1b2" and rec.role == "LEADER"
    assert rec.ts_node_ms == 1234
    assert rec.rssi_dbm == -67 and rec.hops == 2
    assert rec.temp_c == 24.31 and rec.humid_pct == 41.2
    assert rec.t_active_ms == 87
    assert rec.accel_g == (0.01, -0.02, 0.99)
    assert rec.gyro_dps == (0.3, -0.1, 0.05)
    assert rec.i_avg_ma == 6.4 and rec.i_pk_ma == 51.0 and rec.vbat_mv == 2940


def test_accel_magnitude_property():
    rec = parse_line('{"src":"x","role":"SED","accel_g":[3.0,4.0,0.0]}', ts_host_ns=1)
    assert isinstance(rec, Frame)
    # |(3,4,0)| = 5 by 3-4-5 triangle
    assert rec.accel_mag_g is not None
    assert abs(rec.accel_mag_g - 5.0) < 1e-9


def test_gyro_magnitude_property():
    rec = parse_line('{"src":"x","role":"SED","gyro_dps":[0.0,0.0,10.0]}', ts_host_ns=1)
    assert isinstance(rec, Frame)
    assert rec.gyro_mag_dps == 10.0


def test_parse_frame_with_only_required_fields():
    rec = parse_line('{"src":"abcd","role":"CHILD"}', ts_host_ns=42)
    assert isinstance(rec, Frame)
    assert rec.src == "abcd"
    assert rec.temp_c is None and rec.i_avg_ma is None
    assert rec.accel_g is None and rec.gyro_dps is None


def test_malformed_imu_array_falls_back_to_none():
    # Wrong length array should not raise — field stays None.
    rec = parse_line('{"src":"x","role":"SED","accel_g":[1.0,2.0]}', ts_host_ns=1)
    assert isinstance(rec, Frame)
    assert rec.accel_g is None


def test_parse_event():
    rec = parse_line('{"event":"node_joined","src":"a1b2"}', ts_host_ns=99)
    assert isinstance(rec, Event)
    assert rec.name == "node_joined"
    assert rec.payload == {"src": "a1b2"}


def test_unknown_extra_fields_preserved():
    rec = parse_line('{"src":"a1b2","role":"SED","new_v3_field":42}', ts_host_ns=1)
    assert isinstance(rec, Frame)
    assert rec.extras == {"new_v3_field": 42}


def test_malformed_json_becomes_log_line():
    rec = parse_line('{"src":"abc' , ts_host_ns=1)
    assert isinstance(rec, LogLine)


def test_non_object_json_is_log_line():
    rec = parse_line('[1, 2, 3]', ts_host_ns=1)
    assert isinstance(rec, LogLine)


def test_plain_text_line_is_log():
    rec = parse_line('[00:00:01.234] gateway: hello', ts_host_ns=1)
    assert isinstance(rec, LogLine)
    assert "gateway: hello" in rec.text


def test_empty_line_is_empty_log():
    rec = parse_line('', ts_host_ns=1)
    assert isinstance(rec, LogLine)
    assert rec.text == ""
