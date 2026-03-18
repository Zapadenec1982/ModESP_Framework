"""
test_driver_validator.py — тести DriverManifestValidator та cross-валідації

Перевіряє валідацію driver manifests та їх зв'язок з module manifests.
"""
import json
import sys
from pathlib import Path

import pytest

# Додаємо tools/ до sys.path для імпорту generate_ui
TOOLS_DIR = Path(__file__).parent.parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

from generate_ui import DriverManifestValidator, cross_validate

FIXTURES_DIR = Path(__file__).parent / "fixtures"


def load_fixture(name):
    """Load a JSON fixture file by name."""
    with open(FIXTURES_DIR / name, "r", encoding="utf-8") as f:
        return json.load(f)


@pytest.fixture
def drv_validator():
    """Fresh DriverManifestValidator instance."""
    return DriverManifestValidator()


@pytest.fixture
def valid_ds18b20():
    return load_fixture("valid_ds18b20.json")


@pytest.fixture
def valid_relay():
    return load_fixture("valid_relay.json")


# ═══════════════════════════════════════════════════════════════
#  Valid driver manifests
# ═══════════════════════════════════════════════════════════════

class TestValidDriverManifests:
    """Валідні driver manifests проходять без помилок."""

    def test_valid_ds18b20(self, drv_validator, valid_ds18b20):
        """ds18b20 manifest проходить валідацію."""
        result = drv_validator.validate(valid_ds18b20, "test")
        assert result is True
        assert len(drv_validator.errors) == 0

    def test_valid_relay(self, drv_validator, valid_relay):
        """relay manifest проходить валідацію."""
        result = drv_validator.validate(valid_relay, "test")
        assert result is True
        assert len(drv_validator.errors) == 0


# ═══════════════════════════════════════════════════════════════
#  Missing required fields
# ═══════════════════════════════════════════════════════════════

class TestMissingFields:
    """Відсутні обов'язкові поля генерують помилки."""

    def test_missing_driver(self, drv_validator):
        """Відсутній driver → помилка."""
        manifest = {
            "manifest_version": 1,
            "category": "sensor",
            "hardware_type": "onewire_bus",
            "provides": {"type": "float"}
        }
        drv_validator.validate(manifest, "test")
        assert any("'driver'" in e for e in drv_validator.errors)

    def test_missing_category(self, drv_validator):
        """Відсутній category → помилка."""
        manifest = load_fixture("invalid_driver_no_category.json")
        drv_validator.validate(manifest, "test")
        assert any("'category'" in e for e in drv_validator.errors)

    def test_missing_hardware_type(self, drv_validator):
        """Відсутній hardware_type → помилка."""
        manifest = load_fixture("invalid_driver_no_hw_type.json")
        drv_validator.validate(manifest, "test")
        assert any("'hardware_type'" in e for e in drv_validator.errors)

    def test_missing_provides(self, drv_validator):
        """Відсутній provides → помилка."""
        manifest = {
            "manifest_version": 1,
            "driver": "test_drv",
            "category": "sensor",
            "hardware_type": "adc_channel"
        }
        drv_validator.validate(manifest, "test")
        assert any("'provides'" in e for e in drv_validator.errors)

    def test_missing_manifest_version(self, drv_validator):
        """Відсутня manifest_version → помилка."""
        manifest = {
            "driver": "test_drv",
            "category": "sensor",
            "hardware_type": "adc_channel",
            "provides": {"type": "float"}
        }
        drv_validator.validate(manifest, "test")
        assert any("manifest_version" in e for e in drv_validator.errors)


# ═══════════════════════════════════════════════════════════════
#  Settings validation
# ═══════════════════════════════════════════════════════════════

class TestSettingsValidation:
    """Перевірка settings драйвера."""

    def test_setting_missing_min_max_for_int(self, drv_validator):
        """Setting int без min/max/step → помилки."""
        manifest = {
            "manifest_version": 1,
            "driver": "test_drv",
            "category": "sensor",
            "hardware_type": "adc_channel",
            "provides": {"type": "float"},
            "settings": [
                {"key": "interval", "type": "int", "default": 1000}
            ]
        }
        drv_validator.validate(manifest, "test")
        errs = [e for e in drv_validator.errors if "interval" in e]
        # min, max, step — 3 помилки
        assert len(errs) == 3

    def test_setting_missing_min_max_for_float(self, drv_validator):
        """Setting float без min/max/step → помилки."""
        manifest = {
            "manifest_version": 1,
            "driver": "test_drv",
            "category": "sensor",
            "hardware_type": "adc_channel",
            "provides": {"type": "float"},
            "settings": [
                {"key": "offset", "type": "float", "default": 0.0}
            ]
        }
        drv_validator.validate(manifest, "test")
        errs = [e for e in drv_validator.errors if "offset" in e]
        assert len(errs) == 3

    def test_setting_missing_key(self, drv_validator):
        """Setting без key → помилка."""
        manifest = {
            "manifest_version": 1,
            "driver": "test_drv",
            "category": "sensor",
            "hardware_type": "adc_channel",
            "provides": {"type": "float"},
            "settings": [
                {"type": "int", "default": 100, "min": 0, "max": 1000, "step": 10}
            ]
        }
        drv_validator.validate(manifest, "test")
        assert any("missing 'key'" in e for e in drv_validator.errors)

    def test_setting_missing_type(self, drv_validator):
        """Setting без type → помилка."""
        manifest = {
            "manifest_version": 1,
            "driver": "test_drv",
            "category": "sensor",
            "hardware_type": "adc_channel",
            "provides": {"type": "float"},
            "settings": [
                {"key": "foo", "default": 100}
            ]
        }
        drv_validator.validate(manifest, "test")
        assert any("missing 'type'" in e for e in drv_validator.errors)

    def test_setting_invalid_key_format(self, drv_validator):
        """Setting key з невалідними символами → помилка."""
        manifest = {
            "manifest_version": 1,
            "driver": "test_drv",
            "category": "sensor",
            "hardware_type": "adc_channel",
            "provides": {"type": "float"},
            "settings": [
                {"key": "Bad-Key!", "type": "int", "default": 0, "min": 0, "max": 10, "step": 1}
            ]
        }
        drv_validator.validate(manifest, "test")
        assert any("[a-z0-9_]" in e for e in drv_validator.errors)


# ═══════════════════════════════════════════════════════════════
#  Cross-validation module <-> driver
# ═══════════════════════════════════════════════════════════════

class TestCrossValidation:
    """Cross-валідація module requires vs driver manifests."""

    def test_module_requires_missing_driver(self):
        """Module requires driver що не існує → помилка."""
        modules = [{
            "module": "test_mod",
            "requires": [
                {"role": "temp", "type": "sensor", "driver": "nonexistent"}
            ]
        }]
        drivers = {}
        errors = []
        warnings = []
        cross_validate(modules, drivers, errors, warnings)
        assert len(errors) == 1
        assert "nonexistent" in errors[0]

    def test_module_requires_wrong_category(self):
        """Module requires actuator але driver.category=sensor → помилка."""
        modules = [{
            "module": "test_mod",
            "requires": [
                {"role": "compressor", "type": "actuator", "driver": "ds18b20"}
            ]
        }]
        drivers = {
            "ds18b20": {"driver": "ds18b20", "category": "sensor"}
        }
        errors = []
        warnings = []
        cross_validate(modules, drivers, errors, warnings)
        assert len(errors) == 1
        assert "actuator" in errors[0]
        assert "sensor" in errors[0]

    def test_valid_cross_validation(self):
        """Правильний зв'язок module↔driver — без помилок."""
        modules = [{
            "module": "thermostat",
            "requires": [
                {"role": "chamber_temp", "type": "sensor", "driver": "ds18b20"},
                {"role": "compressor", "type": "actuator", "driver": "relay"}
            ]
        }]
        drivers = {
            "ds18b20": {"driver": "ds18b20", "category": "sensor"},
            "relay": {"driver": "relay", "category": "actuator"}
        }
        errors = []
        warnings = []
        cross_validate(modules, drivers, errors, warnings)
        assert len(errors) == 0
        assert len(warnings) == 0

    def test_optional_missing_driver_is_warning(self):
        """Optional require без driver manifest → warning, не error."""
        modules = [{
            "module": "test_mod",
            "requires": [
                {"role": "door", "type": "sensor", "driver": "digital_input",
                 "optional": True}
            ]
        }]
        drivers = {}
        errors = []
        warnings = []
        cross_validate(modules, drivers, errors, warnings)
        assert len(errors) == 0
        assert len(warnings) == 1
        assert "digital_input" in warnings[0]

    def test_driver_array_in_requires(self):
        """Module requires з масивом драйверів — всі перевіряються."""
        modules = [{
            "module": "test_mod",
            "requires": [
                {"role": "temp", "type": "sensor", "driver": ["ds18b20", "ntc"]}
            ]
        }]
        drivers = {
            "ds18b20": {"driver": "ds18b20", "category": "sensor"}
            # ntc відсутній
        }
        errors = []
        warnings = []
        cross_validate(modules, drivers, errors, warnings)
        # ntc не знайдений — помилка (non-optional)
        assert len(errors) == 1
        assert "ntc" in errors[0]
