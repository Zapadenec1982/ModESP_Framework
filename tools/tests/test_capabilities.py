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

from generate_ui import (  # noqa: E402
    load_capabilities, derive_driver_capabilities, cross_validate, schema_errors,
    role_channels_for, _driver_declared_capabilities)


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


# ── P1: drift guard (dormant until a driver DECLARES capability) + schema fields ──

def test_drift_guard_wrong_kind_errors():
    caps = load_capabilities()
    errors, warnings = [], []
    # a SENSOR driver that declares an ACTUATOR capability must be an ERROR
    drv = {"driver": "x", "category": "sensor", "hardware_type": "ble",
           "provides": {"capability": "relay_out"}}
    cross_validate([], {"x": drv}, errors, warnings, caps)
    assert any("relay_out" in e and "kind" in e for e in errors), errors


def test_drift_guard_unknown_capability_errors():
    caps = load_capabilities()
    errors, warnings = [], []
    drv = {"driver": "y", "category": "sensor", "hardware_type": "onewire_bus",
           "provides": {"capability": "nonesuch"}}
    cross_validate([], {"y": drv}, errors, warnings, caps)
    assert any("nonesuch" in e for e in errors), errors


def test_drift_guard_valid_declared_capability_ok():
    caps = load_capabilities()
    errors, warnings = [], []
    drv = {"driver": "z", "category": "sensor", "hardware_type": "onewire_bus",
           "provides": {"capability": "temperature"}}
    cross_validate([], {"z": drv}, errors, warnings, caps)
    assert errors == []


def test_schema_accepts_new_driver_fields():
    drv = {"manifest_version": 1, "driver": "x", "category": "sensor", "hardware_type": "ble",
           "transport": "ble",
           "provides": {"type": "float", "capability": "temperature",
                        "channels": [{"channel": "t", "capability": "temperature", "unit": "°C"}]},
           "address_channels": [{"value": "t", "label": "T", "capability": "temperature"}]}
    assert schema_errors(drv, "driver", "x") == []


def test_schema_accepts_role_capability_and_kind():
    mod = {"manifest_version": 1, "module": "m", "state": {},
           "requires": [{"role": "r", "type": "sensor", "kind": "sensor", "capability": "temperature"}]}
    assert schema_errors(mod, "module", "m") == []


# ── P3: capability is the matcher (eligible-by-capability, superset safety) ──

def _real_cap_index():
    """capability -> set(driver names) from the real (post-P2) driver manifests."""
    idx = {}
    for mp in (TOOLS_DIR.parent / "drivers").glob("*/manifest.json"):
        drv = json.loads(mp.read_text(encoding="utf-8"))
        for cap in set(_driver_declared_capabilities(drv)):
            idx.setdefault(cap, set()).add(drv["driver"])
    return idx


def test_golden_superset_eligible_covers_legacy_driver_lists():
    """Every role's capability-eligible set must be a SUPERSET of the drivers it used to hand-list
    (so the P3 migration can never make a role LESS bindable than before)."""
    idx = _real_cap_index()
    HIST = {
        "air_temp": ("temperature", {"ds18b20", "ntc"}),
        "room_temp": ("temperature", {"ble_xiaomi_th"}),
        "orientation": ("angle", {"ble_nrf_tilt"}),
        "actuator_1": ("relay_out", {"relay", "pcf8574_relay"}),
        "display_main": ("display", {"amt630a"}),
        "audio_main": ("audio", {"max98357a"}),
        "panel": ("panel", {"ble_led_panel"}),
    }
    for role, (cap, legacy) in HIST.items():
        eligible = idx.get(cap, set())
        assert legacy <= eligible, f"{role}: legacy {legacy} not ⊆ eligible {eligible}"
    # and capability genuinely UNIFIES transports: a temperature role now also accepts BLE
    assert "ble_xiaomi_th" in idx["temperature"]


def test_role_channels_filtered_by_capability():
    drv = {"address_channels": [{"value": "temperature", "label": "T", "capability": "temperature"},
                                {"value": "humidity", "label": "H", "capability": "humidity"}]}
    assert [c["value"] for c in role_channels_for(drv, "temperature")] == ["temperature"]
    # provides.channels form (ld2410b): distance covers moving/static/detect
    drv2 = {"provides": {"channels": [{"channel": "moving", "capability": "distance"},
                                      {"channel": "presence", "capability": "presence"}]}}
    assert [c["value"] for c in role_channels_for(drv2, "distance")] == ["moving"]
    # no capability -> all address_channels (legacy)
    assert role_channels_for(drv, None) == drv["address_channels"]
    # a provides.channels entry lacking a 'channel' name is skipped, not crashed on
    drv3 = {"provides": {"channels": [{"capability": "distance"},
                                      {"channel": "moving", "capability": "distance"}]}}
    assert [c["value"] for c in role_channels_for(drv3, "distance")] == ["moving"]


def test_cross_validate_capability_role_ok():
    caps = load_capabilities()
    errors, warnings = [], []
    cross_validate([{"module": "m", "requires": [{"role": "t", "type": "sensor", "capability": "temperature"}]}],
                   {}, errors, warnings, caps, {"temperature": ["ds18b20"]})
    assert errors == []


def test_cross_validate_capability_wrong_kind_errors():
    caps = load_capabilities()
    errors, warnings = [], []
    cross_validate([{"module": "m", "requires": [{"role": "x", "type": "actuator", "capability": "temperature"}]}],
                   {}, errors, warnings, caps, {"temperature": ["ds18b20"]})
    assert any("kind" in e for e in errors), errors


def test_cross_validate_warns_on_driver_plus_capability():
    caps = load_capabilities()
    errors, warnings = [], []
    cross_validate(
        [{"module": "m", "requires": [
            {"role": "r", "type": "sensor", "capability": "temperature", "driver": ["ds18b20"]}]}],
        {"ds18b20": {"category": "sensor", "provides": {"capability": "temperature"}}},
        errors, warnings, caps, {"temperature": ["ds18b20"]})
    assert any("capability is the matcher" in w for w in warnings), warnings
