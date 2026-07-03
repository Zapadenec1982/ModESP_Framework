# -*- coding: utf-8 -*-
"""Tests for manifest-driven MQTT: alarm flags, HA entities, topic root.

MqttTopicsGenerator + ManifestValidator mqtt.alarm/mqtt.ha rules.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from generate_ui import MqttTopicsGenerator, ManifestValidator  # noqa: E402


def _manifest(**over):
    m = {
        "manifest_version": 1,
        "module": "test_mod",
        "state": {
            "test_mod.temp":     {"type": "float", "access": "read", "unit": "°C"},
            "test_mod.overheat": {"type": "bool",  "access": "read"},
            "test_mod.setpoint": {"type": "float", "access": "readwrite",
                                  "min": 0, "max": 50, "step": 0.5},
        },
        "mqtt": {
            "publish": ["test_mod.temp", "test_mod.overheat"],
            "subscribe": ["test_mod.setpoint"],
        },
    }
    m["mqtt"].update(over)
    return m


class TestMqttTopicsGenerator:
    def test_alarm_bitmap_parallel_to_publish(self):
        out = MqttTopicsGenerator().generate(
            [_manifest(alarm=["test_mod.overheat"])])
        assert "static constexpr bool MQTT_PUBLISH_ALARM[] = {" in out
        assert "false,  // test_mod.temp" in out
        assert "true,  // test_mod.overheat" in out

    def test_ha_entities_with_unit_fallback(self):
        out = MqttTopicsGenerator().generate(
            [_manifest(ha={"test_mod.temp": {
                "name": "Temp", "component": "sensor",
                "device_class": "temperature"}})])
        # unit підтягнувся зі state-ключа
        assert '{"test_mod.temp", "Temp", "sensor", "temperature", "°C", ""},' in out
        assert "HA_ENTITIES_COUNT = 1" in out

    def test_ha_unit_override(self):
        out = MqttTopicsGenerator().generate(
            [_manifest(ha={"test_mod.temp": {"name": "T", "unit": "K"}})])
        assert '"K"' in out

    def test_topic_root_default_and_custom(self):
        gen = MqttTopicsGenerator()
        assert 'MQTT_TOPIC_ROOT = "modesp"' in gen.generate([_manifest()])
        assert 'MQTT_TOPIC_ROOT = "acme"' in gen.generate(
            [_manifest()], {"system": {"mqtt_topic_root": "acme"}})

    def test_empty_ha_entities(self):
        out = MqttTopicsGenerator().generate([_manifest()])
        assert "HA_ENTITIES_COUNT = 0" in out


class TestMqttValidation:
    def _validate(self, manifest):
        v = ManifestValidator()
        v.validate_manifest(manifest, "test/manifest.json")
        return v.errors

    def test_alarm_must_be_published(self):
        errors = self._validate(_manifest(alarm=["test_mod.setpoint"]))
        assert any("alarm key" in e and "not in mqtt.publish" in e for e in errors)

    def test_ha_must_be_published(self):
        errors = self._validate(_manifest(ha={"test_mod.setpoint": {"name": "SP"}}))
        assert any("ha key" in e and "not in mqtt.publish" in e for e in errors)

    def test_ha_component_whitelist(self):
        errors = self._validate(_manifest(ha={"test_mod.temp": {
            "name": "T", "component": "switch"}}))
        assert any("component 'switch' not" in e for e in errors)

    def test_ha_name_required(self):
        errors = self._validate(_manifest(ha={"test_mod.temp": {}}))
        assert any("non-empty 'name'" in e for e in errors)

    def test_ha_strings_reject_quotes(self):
        # Лапки/бекслеші потрапили б у C-літерал і discovery-JSON без escaping
        errors = self._validate(_manifest(ha={"test_mod.temp": {
            "name": 'Say "hi"'}}))
        assert any("must not contain quotes" in e for e in errors)

    def test_valid_ha_and_alarm_pass(self):
        errors = self._validate(_manifest(
            alarm=["test_mod.overheat"],
            ha={"test_mod.temp": {"name": "Temp", "component": "sensor"}}))
        assert errors == []
