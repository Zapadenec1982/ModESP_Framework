"""
P0 — capability vocabulary (tools/capabilities.json) load/validate + driver-capability
derivation. Capability is BUILD-TIME only and NOT wired into matching yet (P3), so these
are pure-logic unit tests; the "no output change" guarantee is covered by the generator's
existing golden tests plus the fact that no capability token appears in ui.json.
"""
import json
import sys
from pathlib import Path

import pytest

TOOLS_DIR = Path(__file__).resolve().parent.parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

from generate_ui import load_capabilities, derive_driver_capabilities  # noqa: E402


# ── vocabulary load + validation ────────────────────────────────

def test_capabilities_json_loads_and_is_consistent():
    caps = load_capabilities()
    assert caps, "tools/capabilities.json should load a non-empty vocabulary"
    # a couple of anchors
    assert caps["temperature"]["kind"] == "sensor"
    assert caps["temperature"]["direction"] == "in"
    assert caps["relay_out"]["kind"] == "actuator"
    assert caps["relay_out"]["direction"] == "out"
    # kind <-> direction consistency holds for every entry
    for name, c in caps.items():
        assert (c["kind"] == "sensor") == (c["direction"] == "in"), name
        assert c["value_type"] in ("float", "bool", "enum", "blob"), name


def test_missing_file_is_empty(tmp_path):
    assert load_capabilities(tmp_path / "nope.json") == {}


def test_inconsistent_kind_direction_rejected(tmp_path):
    bad = tmp_path / "caps.json"
    bad.write_text(json.dumps({"capabilities": [
        {"name": "x", "kind": "sensor", "direction": "out", "value_type": "float"}]}))
    with pytest.raises(ValueError):
        load_capabilities(bad)


def test_bad_kind_rejected(tmp_path):
    bad = tmp_path / "caps.json"
    bad.write_text(json.dumps({"capabilities": [
        {"name": "x", "kind": "widget", "direction": "in", "value_type": "float"}]}))
    with pytest.raises(ValueError):
        load_capabilities(bad)


def test_duplicate_name_rejected(tmp_path):
    bad = tmp_path / "caps.json"
    bad.write_text(json.dumps({"capabilities": [
        {"name": "t", "kind": "sensor", "direction": "in", "value_type": "float"},
        {"name": "t", "kind": "sensor", "direction": "in", "value_type": "bool"}]}))
    with pytest.raises(ValueError):
        load_capabilities(bad)


# ── derivation (must cover all 13 shipped drivers) ──────────────

def test_derive_single_value_sensors():
    assert derive_driver_capabilities(
        {"driver": "ds18b20", "category": "sensor", "hardware_type": "onewire_bus",
         "provides": {"type": "float", "unit": "°C"}}) == {None: "temperature"}
    assert derive_driver_capabilities(
        {"driver": "ntc", "category": "sensor", "hardware_type": "adc_channel",
         "provides": {"unit": "°C"}}) == {None: "temperature"}
    assert derive_driver_capabilities(
        {"driver": "digital_input", "category": "sensor", "hardware_type": "gpio_input"}) == {None: "binary_in"}
    assert derive_driver_capabilities(
        {"driver": "pcf8574_input", "category": "sensor", "hardware_type": "i2c_expander_input"}) == {None: "binary_in"}
    assert derive_driver_capabilities(
        {"driver": "ld2410b", "category": "sensor", "hardware_type": "uart_bus",
         "provides": {"type": "float"}}) == {None: "presence"}


def test_derive_single_value_actuators_and_rich():
    assert derive_driver_capabilities(
        {"driver": "relay", "category": "actuator", "hardware_type": "gpio_output"}) == {None: "relay_out"}
    assert derive_driver_capabilities(
        {"driver": "pcf8574_relay", "category": "actuator", "hardware_type": "i2c_expander_output"}) == {None: "relay_out"}
    assert derive_driver_capabilities(
        {"driver": "amt630a", "category": "display", "hardware_type": "i2c_display"}) == {None: "display"}
    assert derive_driver_capabilities(
        {"driver": "at7456e", "category": "display", "hardware_type": "spi_display"}) == {None: "display"}
    assert derive_driver_capabilities(
        {"driver": "max98357a", "category": "audio", "hardware_type": "i2s_bus"}) == {None: "audio"}
    assert derive_driver_capabilities(
        {"driver": "ble_led_panel", "category": "actuator", "hardware_type": "ble",
         "connect_name_prefix": "LED_BLE"}) == {None: "panel"}


def test_derive_multichannel():
    assert derive_driver_capabilities(
        {"driver": "ble_xiaomi_th", "category": "sensor", "hardware_type": "ble",
         "address_channels": [{"value": "temperature"}, {"value": "humidity"}, {"value": "battery"}]}) == \
        {"temperature": "temperature", "humidity": "humidity", "battery": "battery"}
    assert derive_driver_capabilities(
        {"driver": "ble_nrf_tilt", "category": "sensor", "hardware_type": "ble",
         "address_channels": [{"value": "angle"}, {"value": "tilted"}, {"value": "battery"},
                              {"value": "ax"}, {"value": "ay"}, {"value": "az"}]}) == \
        {"angle": "angle", "tilted": "binary_in", "battery": "battery",
         "ax": "accel", "ay": "accel", "az": "accel"}


def test_explicit_capability_overrides_inference():
    # an explicit per-channel capability wins over the name-based guess
    assert derive_driver_capabilities(
        {"driver": "x", "category": "sensor", "hardware_type": "ble",
         "address_channels": [{"value": "battery", "capability": "voltage"}]}) == {"battery": "voltage"}
    # provides.capability wins for a single-value driver
    assert derive_driver_capabilities(
        {"driver": "y", "category": "sensor", "hardware_type": "adc_channel",
         "provides": {"capability": "pressure", "unit": "°C"}}) == {None: "pressure"}


def test_every_shipped_driver_resolves():
    """No shipped driver may be left UNRESOLVED — the P2 migration must have a seed for all."""
    drivers_dir = TOOLS_DIR.parent / "drivers"
    for mpath in sorted(drivers_dir.glob("*/manifest.json")):
        drv = json.loads(mpath.read_text(encoding="utf-8"))
        derived = derive_driver_capabilities(drv)
        assert derived, f"{drv.get('driver')} derived no capability"
        caps = load_capabilities()
        for cap in derived.values():
            assert cap in caps, f"{drv.get('driver')} → '{cap}' not in capabilities.json"
