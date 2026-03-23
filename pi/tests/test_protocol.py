"""
test_protocol.py — Unit tests for the serial protocol module.
No hardware required — pure string/parsing logic only.
Run with: pytest pi/tests/test_protocol.py
"""

import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent.parent))

from app.serial_bridge.protocol import (
    cmd_vel, cmd_stop, cmd_estop, cmd_home,
    cmd_set_speed, cmd_get_position,
    parse_position, parse_speed, parse_accel,
)


def test_vel_positive():
    assert cmd_vel("pan", 30.0) == "vel pan 30.000\n"

def test_vel_negative():
    assert cmd_vel("tilt", -12.5) == "vel tilt -12.500\n"

def test_vel_all():
    assert cmd_vel("all", 0.0) == "vel all 0.000\n"

def test_stop_default():
    assert cmd_stop() == "stop all\n"

def test_stop_axis():
    assert cmd_stop("pan") == "stop pan\n"

def test_estop():
    assert cmd_estop() == "estop\n"

def test_home():
    assert cmd_home("tilt") == "home tilt\n"

def test_set_speed():
    assert cmd_set_speed(90.0) == "set speed 90.00\n"

def test_parse_position_normal():
    resp = "pan  : 45.123 °\r\ntilt : -10.456 °\r\nptz> "
    result = parse_position(resp)
    assert result is not None
    pan, tilt = result
    assert abs(pan  -  45.123) < 0.001
    assert abs(tilt - -10.456) < 0.001

def test_parse_position_zero():
    resp = "pan  : 0.000 °\r\ntilt : 0.000 °\r\n"
    result = parse_position(resp)
    assert result == (0.0, 0.0)

def test_parse_position_malformed():
    assert parse_position("garbage") is None

def test_parse_speed():
    resp = "max speed : 180.00 °/s\r\n"
    assert parse_speed(resp) == 180.0

def test_parse_accel():
    resp = "accel : 30.00 °/s²\r\n"
    assert parse_accel(resp) == 30.0
