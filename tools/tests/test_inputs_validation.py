"""
test_inputs_validation.py — тести валідації секції inputs

Перевіряє 6 правил з docs/10_manifest_standard.md секція 3.2a:
1. Ключ в inputs НЕ МОЖЕ бути одночасно в state цього ж модуля
2. source_module повинен існувати в project.json АБО optional=true
3. source_module повинен мати цей ключ в своїй state
4. type повинен збігатися з типом в state модуля-джерела
5. optional=false і модуля-джерела немає → ERROR
6. optional=true і модуля-джерела немає → WARNING
"""
import sys
from pathlib import Path

import pytest

TOOLS_DIR = Path(__file__).parent.parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

from generate_ui import ManifestValidator


# ── Допоміжні фабрики маніфестів ──────────────────────────────

def _thermostat_manifest():
    """Модуль-джерело: thermostat з temperature (float) та compressor (bool)."""
    return {
        "manifest_version": 1,
        "module": "thermostat",
        "state": {
            "thermostat.temperature": {
                "type": "float", "access": "read", "unit": "°C",
            },
            "thermostat.setpoint": {
                "type": "float", "access": "readwrite",
                "min": -35, "max": 0, "step": 0.5,
            },
            "thermostat.compressor": {
                "type": "bool", "access": "read",
            },
        },
    }


def _defrost_manifest():
    """Модуль-джерело: defrost з defrost.active (bool)."""
    return {
        "manifest_version": 1,
        "module": "defrost",
        "state": {
            "defrost.active": {
                "type": "bool", "access": "read",
            },
        },
    }


def _alarm_with_inputs(inputs):
    """Alarm модуль з довільними inputs."""
    return {
        "manifest_version": 1,
        "module": "alarm",
        "state": {
            "alarm.active": {"type": "bool", "access": "read"},
            "alarm.code": {"type": "string", "access": "read"},
        },
        "inputs": inputs,
    }


# ── Тести ─────────────────────────────────────────────────────

class TestInputsValid:
    """Коректний inputs з існуючим source_module."""

    def test_inputs_valid(self):
        """Alarm читає thermostat.temperature — все ок."""
        v = ManifestValidator()
        thermostat = _thermostat_manifest()
        alarm = _alarm_with_inputs({
            "thermostat.temperature": {
                "type": "float",
                "source_module": "thermostat",
                "optional": False,
                "description": "Температура для alarm",
            },
        })

        manifests = [thermostat, alarm]
        active_modules = ["thermostat", "alarm"]

        v.validate_cross_module(manifests, active_modules)
        assert len(v.errors) == 0
        assert len(v.warnings) == 0

    def test_inputs_multiple_valid(self):
        """Alarm читає і thermostat.temperature і defrost.active — все ок."""
        v = ManifestValidator()
        thermostat = _thermostat_manifest()
        defrost = _defrost_manifest()
        alarm = _alarm_with_inputs({
            "thermostat.temperature": {
                "type": "float",
                "source_module": "thermostat",
                "optional": False,
            },
            "defrost.active": {
                "type": "bool",
                "source_module": "defrost",
                "optional": True,
            },
        })

        manifests = [thermostat, defrost, alarm]
        active_modules = ["thermostat", "defrost", "alarm"]

        v.validate_cross_module(manifests, active_modules)
        assert len(v.errors) == 0
        assert len(v.warnings) == 0


class TestInputsKeyConflict:
    """Правило 1: ключ є і в state і в inputs → error."""

    def test_inputs_key_conflict(self):
        """alarm.active є в state alarm і в inputs — конфлікт."""
        v = ManifestValidator()
        thermostat = _thermostat_manifest()
        alarm = {
            "manifest_version": 1,
            "module": "alarm",
            "state": {
                "alarm.active": {"type": "bool", "access": "read"},
                "alarm.code": {"type": "string", "access": "read"},
            },
            "inputs": {
                "alarm.active": {  # конфлікт — є і в state
                    "type": "bool",
                    "source_module": "thermostat",
                },
            },
        }

        manifests = [thermostat, alarm]
        active_modules = ["thermostat", "alarm"]

        v.validate_cross_module(manifests, active_modules)
        assert any("conflicts with own state" in e for e in v.errors)


class TestInputsMissingSourceOptional:
    """Правило 6: source_module немає в project, optional=true → warning."""

    def test_inputs_missing_source_optional(self):
        """defrost не в project.json, але optional=true → warning, не error."""
        v = ManifestValidator()
        thermostat = _thermostat_manifest()
        alarm = _alarm_with_inputs({
            "defrost.active": {
                "type": "bool",
                "source_module": "defrost",
                "optional": True,
            },
        })

        # defrost НЕ в active_modules
        manifests = [thermostat, alarm]
        active_modules = ["thermostat", "alarm"]

        v.validate_cross_module(manifests, active_modules)
        assert len(v.errors) == 0
        assert any("not in project.json" in w and "optional" in w
                    for w in v.warnings)


class TestInputsMissingSourceRequired:
    """Правило 5: source_module немає в project, optional=false → error."""

    def test_inputs_missing_source_required(self):
        """defrost не в project.json і optional=false → error."""
        v = ManifestValidator()
        thermostat = _thermostat_manifest()
        alarm = _alarm_with_inputs({
            "defrost.active": {
                "type": "bool",
                "source_module": "defrost",
                "optional": False,
            },
        })

        manifests = [thermostat, alarm]
        active_modules = ["thermostat", "alarm"]

        v.validate_cross_module(manifests, active_modules)
        assert any("not in project.json" in e and "required" in e
                    for e in v.errors)


class TestInputsTypeMismatch:
    """Правило 4: type в inputs != type в state джерела → error."""

    def test_inputs_type_mismatch(self):
        """Alarm очікує int, thermostat має float → error."""
        v = ManifestValidator()
        thermostat = _thermostat_manifest()
        alarm = _alarm_with_inputs({
            "thermostat.temperature": {
                "type": "int",  # mismatch: thermostat має float
                "source_module": "thermostat",
                "optional": False,
            },
        })

        manifests = [thermostat, alarm]
        active_modules = ["thermostat", "alarm"]

        v.validate_cross_module(manifests, active_modules)
        assert any("type mismatch" in e for e in v.errors)


class TestInputsSourceKeyMissing:
    """Правило 3: source_module є, але ключ не в його state → error."""

    def test_inputs_source_key_missing(self):
        """thermostat існує, але thermostat.humidity немає в його state."""
        v = ManifestValidator()
        thermostat = _thermostat_manifest()
        alarm = _alarm_with_inputs({
            "thermostat.humidity": {
                "type": "float",
                "source_module": "thermostat",
                "optional": False,
            },
        })

        manifests = [thermostat, alarm]
        active_modules = ["thermostat", "alarm"]

        v.validate_cross_module(manifests, active_modules)
        assert any("not found in state" in e for e in v.errors)
