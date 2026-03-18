"""
test_features.py — тести для Features System + Select Widgets

Перевіряє:
1. FeatureResolver — визначення active features з bindings
2. Constraints — фільтрація options за active features
3. Select widgets — options в ui.json
4. Disabled widgets — inactive features → disabled=true
5. FeaturesConfigGenerator — генерація features_config.h
"""
import json
import sys
from pathlib import Path

import pytest

# Setup path for imports
TOOLS_DIR = Path(__file__).parent.parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

from generate_ui import (
    FeatureResolver,
    UIJsonGenerator,
    FeaturesConfigGenerator,
    ManifestValidator,
)

PROJECT_ROOT = Path(__file__).parent.parent.parent
MODULES_DIR = PROJECT_ROOT / "modules"
FIXTURES_DIR = Path(__file__).parent / "fixtures"


def load_fixture(name):
    with open(FIXTURES_DIR / name, "r", encoding="utf-8") as f:
        return json.load(f)


def load_manifest(module_name):
    """Завантажити реальний manifest.json модуля."""
    path = MODULES_DIR / module_name / "manifest.json"
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def load_project():
    path = PROJECT_ROOT / "project.json"
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


# ═══════════════════════════════════════════════════════════════
# Fixtures
# ═══════════════════════════════════════════════════════════════

@pytest.fixture
def equipment():
    return load_manifest("equipment")


@pytest.fixture
def thermostat():
    return load_manifest("thermostat")


@pytest.fixture
def defrost():
    return load_manifest("defrost")


@pytest.fixture
def protection():
    return load_manifest("protection")


@pytest.fixture
def all_manifests(equipment, thermostat, defrost, protection):
    return [equipment, thermostat, defrost, protection]


@pytest.fixture
def project():
    return load_project()


# ═══════════════════════════════════════════════════════════════
# FeatureResolver
# ═══════════════════════════════════════════════════════════════

class TestFeatureResolver:
    """Тести визначення active features з bindings."""

    def test_always_active_with_empty_bindings(self, equipment, thermostat):
        """always_active=true → active навіть з порожніми bindings."""
        bindings = {"bindings": []}
        resolver = FeatureResolver(bindings, equipment)
        features = resolver.resolve_module(thermostat)
        assert features["basic_cooling"] is True

    def test_feature_active_all_roles_bound(self, equipment, thermostat):
        """fan_control active коли evap_fan є в bindings."""
        bindings = load_fixture("bindings_with_fans.json")
        resolver = FeatureResolver(bindings, equipment)
        features = resolver.resolve_module(thermostat)
        assert features["fan_control"] is True

    def test_feature_inactive_role_missing(self, equipment, thermostat):
        """fan_control inactive коли evap_fan відсутній."""
        bindings = load_fixture("bindings_minimal.json")
        resolver = FeatureResolver(bindings, equipment)
        features = resolver.resolve_module(thermostat)
        assert features["fan_control"] is False

    def test_feature_requires_multiple_roles(self, equipment, thermostat):
        """fan_temp_control потребує evap_fan AND evap_temp."""
        bindings = load_fixture("bindings_with_fans.json")
        resolver = FeatureResolver(bindings, equipment)
        features = resolver.resolve_module(thermostat)
        assert features["fan_temp_control"] is True

    def test_partial_roles_insufficient(self, equipment, thermostat):
        """fan_temp_control: тільки evap_fan без evap_temp → inactive."""
        # evap_fan є, evap_temp немає
        bindings = {"bindings": [
            {"role": "compressor"}, {"role": "air_temp"}, {"role": "evap_fan"}
        ]}
        resolver = FeatureResolver(bindings, equipment)
        features = resolver.resolve_module(thermostat)
        assert features["fan_temp_control"] is False
        assert features["fan_control"] is True

    def test_minimal_bindings_scenario(self, equipment, all_manifests):
        """compressor+air_temp → тільки always_active features."""
        bindings = load_fixture("bindings_minimal.json")
        resolver = FeatureResolver(bindings, equipment)
        all_features = resolver.resolve_all(all_manifests)

        # Thermostat: тільки basic_cooling
        assert all_features["thermostat"]["basic_cooling"] is True
        assert all_features["thermostat"]["fan_control"] is False
        assert all_features["thermostat"]["condenser_fan"] is False

        # Defrost: тільки defrost_timer
        assert all_features["defrost"]["defrost_timer"] is True
        assert all_features["defrost"]["defrost_electric"] is False
        assert all_features["defrost"]["defrost_hot_gas"] is False

        # Protection: тільки basic_protection
        assert all_features["protection"]["basic_protection"] is True
        assert all_features["protection"]["door_protection"] is False

    def test_full_bindings_scenario(self, equipment, all_manifests):
        """Всі roles bound → всі features active."""
        bindings = load_fixture("bindings_full.json")
        resolver = FeatureResolver(bindings, equipment)
        all_features = resolver.resolve_all(all_manifests)

        for mod_name, features in all_features.items():
            if mod_name == "equipment":
                continue
            for feat_name, active in features.items():
                # night_input не в bindings_full (немає din для night)
                if feat_name in ("night_di",):
                    continue
                assert active is True, f"{mod_name}.{feat_name} should be active"


# ═══════════════════════════════════════════════════════════════
# Constraints
# ═══════════════════════════════════════════════════════════════

class TestConstraintsResolver:
    """Тести фільтрації options за constraints."""

    def test_defrost_constraints_with_requires_state(self, equipment, defrost):
        """Defrost constraints — всі options є, disabled мають requires_state."""
        bindings = load_fixture("bindings_minimal.json")
        resolver = FeatureResolver(bindings, equipment)
        active = resolver.resolve_module(defrost)
        constraints = resolver.resolve_constraints(defrost, active)
        # defrost.type має constraints → всі 3 options в результаті
        assert "defrost.type" in constraints
        type_opts = constraints["defrost.type"]
        assert len(type_opts) == 3
        # Option 0 (за часом) — без requires_state
        assert "requires_state" not in type_opts[0]
        # Option 1 (електрична) — requires_state = equipment.has_defrost_relay
        assert type_opts[1]["requires_state"] == "equipment.has_defrost_relay"
        assert "disabled_hint" in type_opts[1]
        # Option 2 (гарячий газ) — requires_state = equipment.has_defrost_relay
        assert type_opts[2]["requires_state"] == "equipment.has_defrost_relay"

    def test_defrost_initiation_constraints(self, equipment, defrost):
        """Defrost initiation constraints — sensor options мають requires_state."""
        bindings = load_fixture("bindings_minimal.json")
        resolver = FeatureResolver(bindings, equipment)
        active = resolver.resolve_module(defrost)
        constraints = resolver.resolve_constraints(defrost, active)
        assert "defrost.initiation" in constraints
        init_opts = constraints["defrost.initiation"]
        assert len(init_opts) == 4
        # Option 0 (таймер) та 3 (вимкнено) — без requires_state
        assert "requires_state" not in init_opts[0]
        assert "requires_state" not in init_opts[3]
        # Option 1, 2 — requires_state = equipment.has_evap_temp
        assert init_opts[1]["requires_state"] == "equipment.has_evap_temp"
        assert init_opts[2]["requires_state"] == "equipment.has_evap_temp"

    def test_fan_mode_without_evap_temp(self, equipment, thermostat):
        """evap_fan без evap_temp → всі options є, mode 2 має requires_state."""
        bindings = {"bindings": [
            {"role": "compressor"}, {"role": "air_temp"}, {"role": "evap_fan"}
        ]}
        resolver = FeatureResolver(bindings, equipment)
        active = resolver.resolve_module(thermostat)
        constraints = resolver.resolve_constraints(thermostat, active)
        values = [o["value"] for o in constraints["thermostat.evap_fan_mode"]]
        assert 0 in values
        assert 1 in values
        assert 2 in values  # Тепер всі присутні
        # mode 2 має requires_state
        mode2 = [o for o in constraints["thermostat.evap_fan_mode"] if o["value"] == 2][0]
        assert mode2["requires_state"] == "equipment.has_evap_temp"
        assert "disabled_hint" in mode2

    def test_fan_mode_full(self, equipment, thermostat):
        """evap_fan+evap_temp → всі 3 режими."""
        bindings = load_fixture("bindings_with_fans.json")
        resolver = FeatureResolver(bindings, equipment)
        active = resolver.resolve_module(thermostat)
        constraints = resolver.resolve_constraints(thermostat, active)
        values = [o["value"] for o in constraints["thermostat.evap_fan_mode"]]
        assert values == [0, 1, 2]


# ═══════════════════════════════════════════════════════════════
# Select Widgets
# ═══════════════════════════════════════════════════════════════

class TestSelectWidgets:
    """Тести генерації select widgets в ui.json."""

    def _generate_ui(self, manifests, project, bindings=None, equipment=None):
        resolver = None
        if bindings and equipment:
            resolver = FeatureResolver(bindings, equipment)
        gen = UIJsonGenerator()
        return gen.generate(project, manifests, resolver=resolver)

    def test_options_in_state_generates_select(self, all_manifests, project, equipment):
        """State key з options → widget type 'select' в ui.json."""
        bindings = load_fixture("bindings_full.json")
        ui = self._generate_ui(all_manifests, project, bindings, equipment)
        # Знаходимо defrost.type widget
        found = False
        for page in ui["pages"]:
            for card in page.get("cards", []):
                for w in card.get("widgets", []):
                    if w.get("key") == "defrost.type":
                        assert w["widget"] == "select"
                        found = True
        assert found, "defrost.type widget not found"

    def test_select_has_options_array(self, all_manifests, project, equipment):
        """Select widget має options масив з value+label."""
        bindings = load_fixture("bindings_full.json")
        ui = self._generate_ui(all_manifests, project, bindings, equipment)
        for page in ui["pages"]:
            for card in page.get("cards", []):
                for w in card.get("widgets", []):
                    if w.get("key") == "defrost.type" and w.get("widget") == "select":
                        assert "options" in w
                        assert len(w["options"]) > 0
                        for opt in w["options"]:
                            assert "value" in opt
                            assert "label" in opt
                        return
        pytest.fail("defrost.type select widget not found")

    def test_defrost_type_all_options_visible(self, all_manifests, project, equipment):
        """Defrost type shows all 3 options regardless of bindings."""
        bindings = load_fixture("bindings_minimal.json")
        ui = self._generate_ui(all_manifests, project, bindings, equipment)
        for page in ui["pages"]:
            for card in page.get("cards", []):
                for w in card.get("widgets", []):
                    if w.get("key") == "defrost.type" and w.get("widget") == "select":
                        values = [o["value"] for o in w["options"]]
                        assert values == [0, 1, 2]
                        return
        pytest.fail("defrost.type select widget not found")

    def test_numeric_settings_still_number_input(self, all_manifests, project, equipment):
        """Settings без options → widget 'number_input' як раніше."""
        bindings = load_fixture("bindings_full.json")
        ui = self._generate_ui(all_manifests, project, bindings, equipment)
        for page in ui["pages"]:
            for card in page.get("cards", []):
                for w in card.get("widgets", []):
                    if w.get("key") == "thermostat.setpoint":
                        assert w["widget"] == "slider" or w["widget"] == "number_input"
                        assert "options" not in w
                        return


# ═══════════════════════════════════════════════════════════════
# Disabled Widgets
# ═══════════════════════════════════════════════════════════════

class TestUIDisabledWidgets:
    """Тести disabled стану widgets для inactive features."""

    def _generate_ui(self, manifests, project, bindings, equipment):
        resolver = FeatureResolver(bindings, equipment)
        gen = UIJsonGenerator()
        return gen.generate(project, manifests, resolver=resolver)

    def _find_widget(self, ui, key):
        for page in ui["pages"]:
            for card in page.get("cards", []):
                for w in card.get("widgets", []):
                    if w.get("key") == key:
                        return w
        return None

    def _count_disabled(self, ui):
        count = 0
        for page in ui["pages"]:
            for card in page.get("cards", []):
                for w in card.get("widgets", []):
                    if w.get("disabled"):
                        count += 1
        return count

    def test_disabled_widget_has_fields(self, all_manifests, project, equipment):
        """Disabled widget має disabled=true і disabled_reason."""
        bindings = load_fixture("bindings_minimal.json")
        ui = self._generate_ui(all_manifests, project, bindings, equipment)
        w = self._find_widget(ui, "thermostat.evap_fan_mode")
        assert w is not None
        assert w.get("disabled") is True
        assert "disabled_reason" in w

    def test_active_feature_not_disabled(self, all_manifests, project, equipment):
        """Widgets активних features НЕ мають disabled."""
        bindings = load_fixture("bindings_full.json")
        ui = self._generate_ui(all_manifests, project, bindings, equipment)
        w = self._find_widget(ui, "thermostat.setpoint")
        assert w is not None
        assert w.get("disabled") is not True

    def test_always_active_never_disabled(self, all_manifests, project, equipment):
        """thermostat.setpoint ніколи disabled."""
        bindings = load_fixture("bindings_minimal.json")
        ui = self._generate_ui(all_manifests, project, bindings, equipment)
        w = self._find_widget(ui, "thermostat.setpoint")
        assert w is not None
        assert w.get("disabled") is not True

    def test_disabled_count_minimal_bindings(self, all_manifests, project, equipment):
        """Мінімальні bindings → >5 disabled settings."""
        bindings = load_fixture("bindings_minimal.json")
        ui = self._generate_ui(all_manifests, project, bindings, equipment)
        count = self._count_disabled(ui)
        assert count > 5, f"Expected >5 disabled widgets, got {count}"


# ═══════════════════════════════════════════════════════════════
# FeaturesConfigGenerator
# ═══════════════════════════════════════════════════════════════

class TestFeaturesConfigGenerator:
    """Тести генерації features_config.h."""

    def _generate(self, manifests, bindings, equipment):
        resolver = FeatureResolver(bindings, equipment)
        gen = FeaturesConfigGenerator()
        return gen.generate(manifests, resolver)

    def test_generates_pragma_once(self, all_manifests, equipment):
        bindings = load_fixture("bindings_minimal.json")
        result = self._generate(all_manifests, bindings, equipment)
        assert "#pragma once" in result

    def test_namespace(self, all_manifests, equipment):
        bindings = load_fixture("bindings_minimal.json")
        result = self._generate(all_manifests, bindings, equipment)
        assert "namespace modesp::gen" in result

    def test_contains_all_features(self, all_manifests, equipment):
        bindings = load_fixture("bindings_full.json")
        result = self._generate(all_manifests, bindings, equipment)
        assert "basic_cooling" in result
        assert "fan_control" in result
        assert "defrost_timer" in result
        assert "basic_protection" in result
        assert "door_protection" in result

    def test_active_flags_minimal_bindings(self, all_manifests, equipment):
        bindings = load_fixture("bindings_minimal.json")
        result = self._generate(all_manifests, bindings, equipment)
        # basic_cooling завжди active
        assert '"basic_cooling", true' in result
        # fan_control inactive з мінімальними bindings
        assert '"fan_control", false' in result

    def test_active_flags_full_bindings(self, all_manifests, equipment):
        bindings = load_fixture("bindings_full.json")
        result = self._generate(all_manifests, bindings, equipment)
        assert '"basic_cooling", true' in result
        assert '"fan_control", true' in result
        assert '"door_protection", true' in result

    def test_is_feature_active_function(self, all_manifests, equipment):
        bindings = load_fixture("bindings_minimal.json")
        result = self._generate(all_manifests, bindings, equipment)
        assert "is_feature_active" in result

    def test_features_count(self, all_manifests, equipment):
        bindings = load_fixture("bindings_full.json")
        result = self._generate(all_manifests, bindings, equipment)
        assert "FEATURES_COUNT" in result


# ═══════════════════════════════════════════════════════════════
# Select widgets in manifests (structure validation)
# ═══════════════════════════════════════════════════════════════

class TestManifestOptions:
    """Перевірка що маніфести мають правильну структуру options."""

    def test_thermostat_evap_fan_mode_has_options(self, thermostat):
        state = thermostat["state"]["thermostat.evap_fan_mode"]
        assert "options" in state
        assert len(state["options"]) == 3
        labels = [o["label"] for o in state["options"]]
        assert "Постійно" in labels
        assert "З компресором" in labels

    def test_defrost_type_has_options(self, defrost):
        state = defrost["state"]["defrost.type"]
        assert "options" in state
        assert len(state["options"]) == 3
        assert state["options"][0]["label"] == "За часом (зупинка компресора)"

    def test_defrost_counter_mode_has_options(self, defrost):
        state = defrost["state"]["defrost.counter_mode"]
        assert "options" in state
        assert len(state["options"]) == 2

    def test_defrost_initiation_has_options(self, defrost):
        state = defrost["state"]["defrost.initiation"]
        assert "options" in state
        assert len(state["options"]) == 4

    def test_options_keys_no_min_max_step(self, defrost):
        """Keys з options не повинні мати min/max/step."""
        state = defrost["state"]["defrost.type"]
        assert "min" not in state
        assert "max" not in state
        assert "step" not in state

    def test_options_keys_have_persist_and_default(self, defrost):
        """Keys з options мають persist і default."""
        state = defrost["state"]["defrost.type"]
        assert state.get("persist") is True
        assert "default" in state

    def test_manifests_have_features(self):
        """Всі 3 бізнес-модулі мають секцію features."""
        for name in ["thermostat", "defrost", "protection"]:
            m = load_manifest(name)
            assert "features" in m, f"{name} missing features"

    def test_manifests_have_constraints(self):
        """thermostat і defrost мають constraints."""
        for name in ["thermostat", "defrost"]:
            m = load_manifest(name)
            assert "constraints" in m, f"{name} missing constraints"

    def test_equipment_has_labels(self):
        """equipment.requires має label для кожної ролі."""
        m = load_manifest("equipment")
        for req in m["requires"]:
            assert "label" in req, f"Role {req['role']} missing label"

    def test_equipment_has_door_contact(self):
        """equipment.requires містить door_contact."""
        m = load_manifest("equipment")
        roles = [r["role"] for r in m["requires"]]
        assert "door_contact" in roles


# ═══════════════════════════════════════════════════════════════
# Manifest validation (options keys pass validation)
# ═══════════════════════════════════════════════════════════════

class TestManifestValidationWithOptions:
    """Маніфести з options проходять валідацію без помилок."""

    def test_thermostat_validates_ok(self, thermostat):
        v = ManifestValidator()
        v.validate_manifest(thermostat, "thermostat")
        assert len(v.errors) == 0, f"Errors: {v.errors}"

    def test_defrost_validates_ok(self, defrost):
        v = ManifestValidator()
        v.validate_manifest(defrost, "defrost")
        assert len(v.errors) == 0, f"Errors: {v.errors}"

    def test_protection_validates_ok(self, protection):
        v = ManifestValidator()
        v.validate_manifest(protection, "protection")
        assert len(v.errors) == 0, f"Errors: {v.errors}"

    def test_equipment_validates_ok(self, equipment):
        v = ManifestValidator()
        v.validate_manifest(equipment, "equipment")
        assert len(v.errors) == 0, f"Errors: {v.errors}"

    def test_cross_module_validates_ok(self, all_manifests):
        v = ManifestValidator()
        for m in all_manifests:
            v.validate_manifest(m, m["module"])
        v.validate_cross_module(all_manifests)
        assert len(v.errors) == 0, f"Errors: {v.errors}"
