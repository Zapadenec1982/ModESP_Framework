"""
test_validator.py — тести ManifestValidator

Перевіряє всі валідації V1-V13 з docs/09_manifest_specification.md
"""
import json
from pathlib import Path

import sys
from pathlib import Path

import pytest

# Додаємо tools/ до sys.path для імпорту generate_ui
TOOLS_DIR = Path(__file__).parent.parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

from generate_ui import ManifestValidator

FIXTURES_DIR = Path(__file__).parent / "fixtures"


def load_fixture(name):
    """Load a JSON fixture file by name."""
    import json
    with open(FIXTURES_DIR / name, "r", encoding="utf-8") as f:
        return json.load(f)


class TestValidManifests:
    """Валідні маніфести повинні проходити без помилок."""

    def test_valid_thermostat(self, validator, valid_thermostat):
        """V1-V11: повний thermostat manifest проходить валідацію."""
        result = validator.validate_manifest(valid_thermostat, "test")
        assert result is True
        assert len(validator.errors) == 0

    def test_valid_minimal(self, validator, valid_minimal):
        """Мінімальний маніфест (module + state) проходить."""
        result = validator.validate_manifest(valid_minimal, "test")
        assert result is True
        assert len(validator.errors) == 0

    def test_valid_no_warnings(self, validator, valid_thermostat):
        """Правильні state key prefix не генерують warnings."""
        validator.validate_manifest(valid_thermostat, "test")
        assert len(validator.warnings) == 0


class TestManifestVersion:
    """V1: manifest_version перевірка."""

    def test_missing_manifest_version(self, validator):
        """manifest_version обов'язковий."""
        manifest = load_fixture("invalid_no_version.json")
        validator.validate_manifest(manifest, "test")
        assert any("manifest_version" in e for e in validator.errors)

    def test_wrong_manifest_version(self, validator):
        """Непідтримувана версія — помилка."""
        manifest = load_fixture("valid_minimal.json")
        manifest["manifest_version"] = 99
        validator.validate_manifest(manifest, "test")
        assert any("manifest_version=99" in e for e in validator.errors)


class TestRequiredFields:
    """V2-V3: обов'язкові поля module та state."""

    def test_missing_module(self, validator):
        """V2: відсутній module — помилка."""
        manifest = load_fixture("invalid_no_module.json")
        validator.validate_manifest(manifest, "test")
        assert any("module" in e for e in validator.errors)

    def test_missing_state(self, validator):
        """V3: відсутній state — помилка."""
        manifest = load_fixture("invalid_no_state.json")
        validator.validate_manifest(manifest, "test")
        assert any("state" in e for e in validator.errors)


class TestStateKeyValidation:
    """V4-V6: перевірки state keys."""

    def test_missing_type(self, validator):
        """V4: state key без type — помилка."""
        manifest = {
            "manifest_version": 1,
            "module": "test",
            "state": {"test.x": {"access": "read"}}
        }
        validator.validate_manifest(manifest, "test")
        assert any("missing 'type'" in e for e in validator.errors)

    def test_missing_access(self, validator):
        """V5: state key без access — помилка."""
        manifest = {
            "manifest_version": 1,
            "module": "test",
            "state": {"test.x": {"type": "float"}}
        }
        validator.validate_manifest(manifest, "test")
        assert any("missing 'access'" in e for e in validator.errors)

    def test_readwrite_missing_min_max_step(self, validator):
        """V6: readwrite float без min/max/step — 3 помилки."""
        manifest = load_fixture("invalid_rw_no_min.json")
        validator.validate_manifest(manifest, "test")
        rw_errors = [e for e in validator.errors if "Readwrite" in e]
        assert len(rw_errors) == 3  # min, max, step

    def test_readwrite_bool_no_min_max_needed(self, validator):
        """readwrite bool не потребує min/max/step."""
        manifest = {
            "manifest_version": 1,
            "module": "test",
            "state": {
                "test.flag": {
                    "type": "bool",
                    "access": "readwrite",
                    "description": "toggle"
                }
            }
        }
        result = validator.validate_manifest(manifest, "test")
        assert result is True
        assert len(validator.errors) == 0


class TestWidgetValidation:
    """V7-V8: перевірки віджетів."""

    def test_widget_key_not_in_state(self, validator):
        """V7: widget key не існує в state — помилка."""
        manifest = load_fixture("invalid_widget_key.json")
        validator.validate_manifest(manifest, "test")
        assert any("not found in state" in e for e in validator.errors)

    def test_widget_type_incompatible(self, validator):
        """V8: gauge + bool = incompatible."""
        manifest = load_fixture("invalid_widget_type.json")
        validator.validate_manifest(manifest, "test")
        assert any("incompatible" in e for e in validator.errors)

    def test_widget_type_compatible(self, validator, valid_thermostat):
        """Правильні комбінації widget+type проходять."""
        validator.validate_manifest(valid_thermostat, "test")
        assert not any("incompatible" in e for e in validator.errors)


class TestMqttValidation:
    """V9-V10: MQTT keys перевірки."""

    def test_mqtt_publish_invalid_key(self, validator):
        """V9: MQTT publish key не в state — помилка."""
        manifest = {
            "manifest_version": 1,
            "module": "test",
            "state": {"test.x": {"type": "float", "access": "read"}},
            "mqtt": {"publish": ["test.nonexistent"]}
        }
        validator.validate_manifest(manifest, "test")
        assert any("MQTT publish" in e for e in validator.errors)

    def test_mqtt_subscribe_invalid_key(self, validator):
        """V10: MQTT subscribe key не в state — помилка."""
        manifest = {
            "manifest_version": 1,
            "module": "test",
            "state": {"test.x": {"type": "float", "access": "read"}},
            "mqtt": {"subscribe": ["test.nonexistent"]}
        }
        validator.validate_manifest(manifest, "test")
        assert any("MQTT subscribe" in e for e in validator.errors)


class TestDisplayValidation:
    """V11: display keys перевірки."""

    def test_display_main_value_invalid_key(self, validator):
        """V11: display main_value key не в state — помилка."""
        manifest = {
            "manifest_version": 1,
            "module": "test",
            "state": {"test.x": {"type": "float", "access": "read"}},
            "display": {"main_value": {"key": "test.nonexistent", "format": "%s"}}
        }
        validator.validate_manifest(manifest, "test")
        assert any("Display main_value" in e for e in validator.errors)

    def test_display_menu_item_invalid_key(self, validator):
        """V11: display menu_item key не в state — помилка."""
        manifest = {
            "manifest_version": 1,
            "module": "test",
            "state": {"test.x": {"type": "float", "access": "read"}},
            "display": {"menu_items": [{"label": "X", "key": "test.bad"}]}
        }
        validator.validate_manifest(manifest, "test")
        assert any("Display menu_item" in e for e in validator.errors)


class TestCrossModuleValidation:
    """V12: перевірка дублікатів між модулями."""

    def test_duplicate_state_keys(self, validator):
        """V12: однаковий state key в двох модулях — помилка."""
        m1 = {"module": "a", "state": {"shared.key": {"type": "float", "access": "read"}}}
        m2 = {"module": "b", "state": {"shared.key": {"type": "int", "access": "read"}}}
        validator.validate_cross_module([m1, m2])
        assert any("Duplicate state key" in e for e in validator.errors)

    def test_no_duplicate_state_keys(self, validator):
        """Різні state keys — ок."""
        m1 = {"module": "a", "state": {"a.key": {"type": "float", "access": "read"}}}
        m2 = {"module": "b", "state": {"b.key": {"type": "int", "access": "read"}}}
        validator.validate_cross_module([m1, m2])
        assert len(validator.errors) == 0


class TestStateKeyPrefix:
    """V13: state key повинен починатися з <module>."""

    def test_wrong_prefix_warning(self, validator):
        """V13: state key з неправильним prefix — warning."""
        manifest = {
            "manifest_version": 1,
            "module": "test",
            "state": {"other.value": {"type": "float", "access": "read"}}
        }
        validator.validate_manifest(manifest, "test")
        assert any("does not start with" in w for w in validator.warnings)

    def test_correct_prefix_no_warning(self, validator, valid_thermostat):
        """Правильний prefix — без warnings."""
        validator.validate_manifest(valid_thermostat, "test")
        prefix_warnings = [w for w in validator.warnings if "does not start with" in w]
        assert len(prefix_warnings) == 0
