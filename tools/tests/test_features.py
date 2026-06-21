"""
test_features.py — тести для Features / Constraints subsystem у generate_ui.py.

Перевіряє підсистему, яка ЩЕ використовується фреймворком (FeatureResolver →
UIJsonGenerator select/disabled widgets → FeaturesConfigGenerator):

  1. FeatureResolver — конструюється з реальних data/bindings.json + board.json
     та equipment manifest; роздільна здатність active features з bindings.
  2. Constraints — enum_filter → всі options + requires_state/disabled_hint.
  3. Select widgets — state key з options → widget type 'select' в ui.json.
  4. Disabled widgets — inactive feature → disabled=true + disabled_reason.
  5. FeaturesConfigGenerator — генерація features_config.h.

ВАЖЛИВО про fixtures
--------------------
Старі тести вантажили видалені refrigeration-модулі (thermostat/defrost/protection)
та fixtures (bindings_*.json) із refrigeration-ролями. Поточні template-модулі
(equipment, datalogger, simple_thermo, abs_test, display) НЕ оголошують секцій
`features` / `constraints` і не мають state-keys з `options` — тож проти них
підсистема коректно повертає порожній результат (це теж перевіряється нижче).

Щоб усе ж покрити *алгоритм* підсистеми (а він живий), решта тестів будує
маленькі СИНТЕТИЧНІ маніфести прямо в тесті з нейтральними іменами фіч/ролей.
Реальний equipment manifest + реальні data/bindings.json / data/board.json
використовуються там, де вони мають сенс (конструкція резолвера, bound_roles).
"""
import json
import sys
from pathlib import Path

import pytest

# tools/ on sys.path so we can import generate_ui
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
DATA_DIR = PROJECT_ROOT / "data"


def load_manifest(module_name):
    """Завантажити реальний manifest.json модуля."""
    with open(MODULES_DIR / module_name / "manifest.json", "r", encoding="utf-8") as f:
        return json.load(f)


def load_project():
    with open(PROJECT_ROOT / "project.json", "r", encoding="utf-8") as f:
        return json.load(f)


def load_data(name):
    """Завантажити реальний файл з data/ (bindings.json / board.json)."""
    with open(DATA_DIR / name, "r", encoding="utf-8") as f:
        return json.load(f)


# ═══════════════════════════════════════════════════════════════
#  Fixtures — follow project.json so the suite tracks the active set
# ═══════════════════════════════════════════════════════════════

@pytest.fixture(scope="module")
def project():
    return load_project()


@pytest.fixture(scope="module")
def active_modules(project):
    return list(project["modules"])


@pytest.fixture(scope="module")
def all_manifests(active_modules):
    return [load_manifest(m) for m in active_modules]


@pytest.fixture(scope="module")
def equipment():
    """Реальний equipment manifest — джерело all_equipment_roles для резолвера."""
    return load_manifest("equipment")


@pytest.fixture(scope="module")
def real_bindings():
    """Реальний data/bindings.json (плата kc868_a6)."""
    return load_data("bindings.json")


@pytest.fixture(scope="module")
def real_board():
    """Реальний data/board.json."""
    return load_data("board.json")


# ── Синтетичні маніфести (нейтральні імена; підсистема живе, але template
#    модулі поки features/constraints не оголошують) ───────────────────────

@pytest.fixture(scope="module")
def synth_module():
    """Маленький модуль, що вправляє ВСІ гілки підсистеми:
    always_active, single-role, multi-role, enum_filter constraint,
    controls_settings (disabled widget) та звичайний number/slider widget."""
    return {
        "manifest_version": 1,
        "module": "demo",
        "description": "synthetic demo module for features subsystem tests",
        "features": {
            # always_active — активна навіть з порожніми bindings
            "core": {"always_active": True, "controls_settings": ["demo.core_set"]},
            # один required role
            "fan": {"requires_roles": ["evap_fan"],
                    "controls_settings": ["demo.fan_set"],
                    "description": "Fan control"},
            # два required roles (AND)
            "fan_temp": {"requires_roles": ["evap_fan", "evap_temp"]},
        },
        "constraints": {
            # option value 2 ('Auto') доступна лише коли є evap_temp
            "demo.mode": {"type": "enum_filter", "values": {
                "2": {"requires_state": "equipment.has_evap_temp",
                      "disabled_hint": "Немає датчика випарника"},
            }},
        },
        "state": {
            "demo.mode": {"type": "int", "access": "readwrite", "persist": True,
                          "default": 0,
                          "options": [
                              {"value": 0, "label": "Вимк"},
                              {"value": 1, "label": "Охолодження"},
                              {"value": 2, "label": "Авто"},
                          ]},
            "demo.core_set": {"type": "int", "access": "readwrite", "persist": True,
                              "min": 0, "max": 5, "step": 1, "default": 0,
                              "description": "Core setting"},
            "demo.fan_set": {"type": "int", "access": "readwrite", "persist": True,
                             "min": 0, "max": 5, "step": 1, "default": 0,
                             "description": "Fan setting"},
            "demo.setpoint": {"type": "float", "access": "readwrite", "persist": True,
                              "min": -30, "max": 30, "step": 0.5, "default": 0.0,
                              "unit": "°C", "description": "Setpoint"},
        },
        "ui": {
            "page": "Demo", "page_id": "demo",
            "cards": [{"title": "Demo", "widgets": [
                {"key": "demo.mode", "widget": "select"},
                {"key": "demo.core_set", "widget": "number_input"},
                {"key": "demo.fan_set", "widget": "number_input"},
                {"key": "demo.setpoint", "widget": "slider"},
            ]}],
        },
    }


@pytest.fixture
def bindings_minimal():
    """Лише air_temp — fan/fan_temp мають бути inactive."""
    return {"bindings": [{"role": "air_temp"}]}


@pytest.fixture
def bindings_fan_only():
    """evap_fan є, evap_temp нема — fan active, fan_temp inactive."""
    return {"bindings": [{"role": "air_temp"}, {"role": "evap_fan"}]}


@pytest.fixture
def bindings_full():
    """Усі ролі прив'язані — всі features active."""
    return {"bindings": [{"role": "air_temp"}, {"role": "evap_fan"},
                         {"role": "evap_temp"}]}


def _synth_project():
    """Мінімальний project, що рендерить лише demo-сторінку."""
    return {
        "project": "demo", "version": "1.0.0", "modules": ["demo"],
        "system": {"device_name": "Demo",
                   "pages": {"dashboard": False, "network": False, "system": False}},
    }


def _find_widget(ui, key):
    for page in ui["pages"]:
        for card in page.get("cards", []):
            for w in card.get("widgets", []):
                if w.get("key") == key:
                    return w
    return None


def _count_disabled(ui):
    return sum(
        1
        for page in ui["pages"]
        for card in page.get("cards", [])
        for w in card.get("widgets", [])
        if w.get("disabled")
    )


# ═══════════════════════════════════════════════════════════════
#  FeatureResolver — конструкція з РЕАЛЬНИХ даних
# ═══════════════════════════════════════════════════════════════

class TestFeatureResolverRealData:
    """Резолвер будується з data/bindings.json + реального equipment manifest."""

    def test_bound_roles_from_real_bindings(self, real_bindings, equipment):
        """bound_roles == множина ролей з реального data/bindings.json."""
        resolver = FeatureResolver(real_bindings, equipment)
        expected = {b["role"] for b in real_bindings["bindings"]}
        assert resolver.bound_roles == expected
        # active board binds at least one role (board-agnostic — не прив'язуємось до
        # конкретних ролей конкретної плати; data/ може містити будь-яку активну плату)
        assert len(resolver.bound_roles) > 0

    def test_equipment_roles_from_real_manifest(self, real_bindings, equipment):
        """all_equipment_roles == ролі з equipment.requires."""
        resolver = FeatureResolver(real_bindings, equipment)
        expected = {r["role"] for r in equipment["requires"]}
        assert resolver.all_equipment_roles == expected
        assert "air_temp" in resolver.all_equipment_roles

    def test_real_modules_have_no_features(self, real_bindings, equipment, all_manifests):
        """Поточні template-модулі не оголошують features → resolve_all порожній.
        (Підсистема жива; коли модуль додасть `features`, тест нижче його покриє.)"""
        resolver = FeatureResolver(real_bindings, equipment)
        all_features = resolver.resolve_all(all_manifests)
        # Кожен модуль присутній у мапі, але без жодної фічі
        for m in all_manifests:
            assert all_features[m["module"]] == {}

    def test_resolver_constructs_with_real_board_bindings(self, real_bindings,
                                                          real_board, equipment):
        """board.json + bindings.json реально узгоджені: усі hardware id з bindings
        існують на платі (sanity — підсистема працює з цією парою файлів)."""
        # Збираємо hardware id з УСІХ груп плати (будь-який list-of-objects з 'id') —
        # future-proof: i2c_buses/i2c_displays/i2s_buses/ble_devices/onewire/expanders тощо.
        hw_ids = set()
        for val in real_board.values():
            if isinstance(val, list):
                for x in val:
                    if isinstance(x, dict) and "id" in x:
                        hw_ids.add(x["id"])
        for b in real_bindings["bindings"]:
            assert b["hardware"] in hw_ids, f"{b['hardware']} not on board"
        # резолвер будується без помилок
        FeatureResolver(real_bindings, equipment)


# ═══════════════════════════════════════════════════════════════
#  FeatureResolver — алгоритм (синтетичні маніфести)
# ═══════════════════════════════════════════════════════════════

class TestFeatureResolverAlgorithm:
    """always_active / requires_roles subset-логіка."""

    def test_always_active_with_empty_bindings(self, equipment, synth_module):
        """always_active=true → active навіть з порожніми bindings."""
        resolver = FeatureResolver({"bindings": []}, equipment)
        features = resolver.resolve_module(synth_module)
        assert features["core"] is True

    def test_feature_active_when_role_bound(self, equipment, synth_module,
                                            bindings_fan_only):
        """fan active коли evap_fan є в bindings."""
        resolver = FeatureResolver(bindings_fan_only, equipment)
        features = resolver.resolve_module(synth_module)
        assert features["fan"] is True

    def test_feature_inactive_when_role_missing(self, equipment, synth_module,
                                                bindings_minimal):
        """fan inactive коли evap_fan відсутній."""
        resolver = FeatureResolver(bindings_minimal, equipment)
        features = resolver.resolve_module(synth_module)
        assert features["fan"] is False

    def test_multi_role_requires_all(self, equipment, synth_module, bindings_full):
        """fan_temp потребує evap_fan AND evap_temp — обидва є → active."""
        resolver = FeatureResolver(bindings_full, equipment)
        features = resolver.resolve_module(synth_module)
        assert features["fan_temp"] is True

    def test_partial_roles_insufficient(self, equipment, synth_module,
                                        bindings_fan_only):
        """fan_temp: лише evap_fan без evap_temp → inactive (а fan — active)."""
        resolver = FeatureResolver(bindings_fan_only, equipment)
        features = resolver.resolve_module(synth_module)
        assert features["fan_temp"] is False
        assert features["fan"] is True

    def test_resolve_all_keys_by_module(self, equipment, synth_module,
                                        bindings_minimal):
        """resolve_all → {module_name: {feature: bool}}."""
        resolver = FeatureResolver(bindings_minimal, equipment)
        all_features = resolver.resolve_all([synth_module])
        assert all_features["demo"]["core"] is True
        assert all_features["demo"]["fan"] is False
        assert all_features["demo"]["fan_temp"] is False


# ═══════════════════════════════════════════════════════════════
#  Constraints — enum_filter → всі options + requires_state
# ═══════════════════════════════════════════════════════════════

class TestConstraintsResolver:
    """resolve_constraints повертає ВСІ options; gated отримують requires_state."""

    def test_all_options_present_with_requires_state(self, equipment, synth_module,
                                                     bindings_minimal):
        """Усі 3 options присутні; option value=2 має requires_state+disabled_hint."""
        resolver = FeatureResolver(bindings_minimal, equipment)
        active = resolver.resolve_module(synth_module)
        constraints = resolver.resolve_constraints(synth_module, active)
        assert "demo.mode" in constraints
        opts = constraints["demo.mode"]
        assert [o["value"] for o in opts] == [0, 1, 2]
        # option 0, 1 — без requires_state
        assert "requires_state" not in opts[0]
        assert "requires_state" not in opts[1]
        # option 2 — gated
        assert opts[2]["requires_state"] == "equipment.has_evap_temp"
        assert "disabled_hint" in opts[2]

    def test_constraints_independent_of_bindings(self, equipment, synth_module,
                                                 bindings_full):
        """resolve_constraints віддає всі options незалежно від bindings
        (рантайм вирішує disabled через requires_state, не build-time)."""
        resolver = FeatureResolver(bindings_full, equipment)
        active = resolver.resolve_module(synth_module)
        constraints = resolver.resolve_constraints(synth_module, active)
        values = [o["value"] for o in constraints["demo.mode"]]
        assert values == [0, 1, 2]
        # requires_state присутній навіть коли роль прив'язана —
        # це декларація рантайм-залежності, а не фільтр
        gated = [o for o in constraints["demo.mode"] if o["value"] == 2][0]
        assert gated["requires_state"] == "equipment.has_evap_temp"

    def test_no_constraints_for_real_modules(self, equipment, all_manifests,
                                             real_bindings):
        """Поточні template-модулі не мають constraints → порожній результат."""
        resolver = FeatureResolver(real_bindings, equipment)
        for m in all_manifests:
            active = resolver.resolve_module(m)
            assert resolver.resolve_constraints(m, active) == {}


# ═══════════════════════════════════════════════════════════════
#  Select Widgets — state key з options → widget 'select'
# ═══════════════════════════════════════════════════════════════

class TestSelectWidgets:
    """UIJsonGenerator: options у state → select widget з options-масивом."""

    def _ui(self, synth_module, bindings, equipment):
        resolver = FeatureResolver(bindings, equipment)
        return UIJsonGenerator().generate(_synth_project(), [synth_module],
                                          resolver=resolver)

    def test_options_state_generates_select(self, synth_module, equipment,
                                            bindings_full):
        ui = self._ui(synth_module, bindings_full, equipment)
        w = _find_widget(ui, "demo.mode")
        assert w is not None
        assert w["widget"] == "select"

    def test_select_has_options_array(self, synth_module, equipment, bindings_full):
        ui = self._ui(synth_module, bindings_full, equipment)
        w = _find_widget(ui, "demo.mode")
        assert "options" in w and len(w["options"]) > 0
        for opt in w["options"]:
            assert "value" in opt
            assert "label" in opt

    def test_select_shows_all_options_regardless_of_bindings(self, synth_module,
                                                            equipment,
                                                            bindings_minimal):
        """Усі options видимі навіть з мінімальними bindings (рантайм disabled)."""
        ui = self._ui(synth_module, bindings_minimal, equipment)
        w = _find_widget(ui, "demo.mode")
        assert [o["value"] for o in w["options"]] == [0, 1, 2]

    def test_gated_option_carries_requires_state(self, synth_module, equipment,
                                                 bindings_minimal):
        """Select option під constraint несе requires_state у ui.json."""
        ui = self._ui(synth_module, bindings_minimal, equipment)
        w = _find_widget(ui, "demo.mode")
        gated = [o for o in w["options"] if o["value"] == 2][0]
        assert gated["requires_state"] == "equipment.has_evap_temp"

    def test_numeric_setting_is_not_select(self, synth_module, equipment,
                                           bindings_full):
        """Settings без options → не select, без options-масиву."""
        ui = self._ui(synth_module, bindings_full, equipment)
        w = _find_widget(ui, "demo.setpoint")
        assert w["widget"] != "select"
        assert "options" not in w


# ═══════════════════════════════════════════════════════════════
#  Disabled Widgets — inactive feature → disabled=true
# ═══════════════════════════════════════════════════════════════

class TestUIDisabledWidgets:
    """UIJsonGenerator: controls_settings inactive-фічі → disabled widget."""

    def _ui(self, synth_module, bindings, equipment):
        resolver = FeatureResolver(bindings, equipment)
        return UIJsonGenerator().generate(_synth_project(), [synth_module],
                                          resolver=resolver)

    def test_disabled_widget_has_fields(self, synth_module, equipment,
                                        bindings_minimal):
        """Widget неактивної fan-фічі → disabled=true + disabled_reason."""
        ui = self._ui(synth_module, bindings_minimal, equipment)
        w = _find_widget(ui, "demo.fan_set")
        assert w is not None
        assert w.get("disabled") is True
        assert "disabled_reason" in w
        # reason називає відсутню роль
        assert "evap_fan" in w["disabled_reason"]

    def test_active_feature_not_disabled(self, synth_module, equipment,
                                         bindings_full):
        """Widget активної fan-фічі НЕ disabled."""
        ui = self._ui(synth_module, bindings_full, equipment)
        w = _find_widget(ui, "demo.fan_set")
        assert w.get("disabled") is not True

    def test_always_active_never_disabled(self, synth_module, equipment,
                                          bindings_minimal):
        """controls_settings always_active-фічі — ніколи disabled."""
        ui = self._ui(synth_module, bindings_minimal, equipment)
        w = _find_widget(ui, "demo.core_set")
        assert w.get("disabled") is not True

    def test_setting_without_feature_not_disabled(self, synth_module, equipment,
                                                  bindings_minimal):
        """Setting, який жодна фіча не контролює — ніколи disabled."""
        ui = self._ui(synth_module, bindings_minimal, equipment)
        w = _find_widget(ui, "demo.setpoint")
        assert w.get("disabled") is not True

    def test_disabled_count_changes_with_bindings(self, synth_module, equipment,
                                                  bindings_minimal, bindings_full):
        """Менше bindings → більше disabled widgets, ніж за повних bindings."""
        ui_min = self._ui(synth_module, bindings_minimal, equipment)
        ui_full = self._ui(synth_module, bindings_full, equipment)
        assert _count_disabled(ui_min) >= 1
        assert _count_disabled(ui_min) > _count_disabled(ui_full)


# ═══════════════════════════════════════════════════════════════
#  FeaturesConfigGenerator — features_config.h
# ═══════════════════════════════════════════════════════════════

class TestFeaturesConfigGenerator:
    """Генерація generated/features_config.h."""

    def _gen(self, manifests, bindings, equipment):
        resolver = FeatureResolver(bindings, equipment)
        return FeaturesConfigGenerator().generate(manifests, resolver)

    def test_generates_pragma_once(self, synth_module, equipment, bindings_minimal):
        result = self._gen([synth_module], bindings_minimal, equipment)
        assert "#pragma once" in result

    def test_namespace(self, synth_module, equipment, bindings_minimal):
        result = self._gen([synth_module], bindings_minimal, equipment)
        assert "namespace modesp::gen" in result

    def test_contains_all_features(self, synth_module, equipment, bindings_full):
        result = self._gen([synth_module], bindings_full, equipment)
        for feat in ("core", "fan", "fan_temp"):
            assert f'"{feat}"' in result

    def test_active_flags_minimal_bindings(self, synth_module, equipment,
                                           bindings_minimal):
        result = self._gen([synth_module], bindings_minimal, equipment)
        # always_active → true; fan (потребує evap_fan) → false
        assert '"demo", "core", true' in result
        assert '"demo", "fan", false' in result

    def test_active_flags_full_bindings(self, synth_module, equipment, bindings_full):
        result = self._gen([synth_module], bindings_full, equipment)
        assert '"demo", "core", true' in result
        assert '"demo", "fan", true' in result
        assert '"demo", "fan_temp", true' in result

    def test_is_feature_active_helper(self, synth_module, equipment, bindings_minimal):
        result = self._gen([synth_module], bindings_minimal, equipment)
        assert "is_feature_active" in result

    def test_features_count_matches_entries(self, synth_module, equipment,
                                            bindings_full):
        """FEATURES_COUNT == кількість (module,feature) пар (обчислено з маніфесту)."""
        expected = len(synth_module["features"])
        result = self._gen([synth_module], bindings_full, equipment)
        assert f"FEATURES_COUNT = {expected}" in result

    def test_real_modules_produce_placeholder(self, all_manifests, equipment,
                                              real_bindings):
        """Template-модулі без features → валідний placeholder-заголовок."""
        result = self._gen(all_manifests, real_bindings, equipment)
        assert "#pragma once" in result
        assert "FEATURES_COUNT" in result
        # жодних реальних фіч → таблиця має placeholder-рядок
        assert "placeholder" in result


# ═══════════════════════════════════════════════════════════════
#  Manifest structure / validation для options-keys
# ═══════════════════════════════════════════════════════════════

class TestOptionsManifestValidation:
    """state-key з options проходить ManifestValidator без помилок (V18 тощо)."""

    def test_synth_module_validates_ok(self, synth_module):
        v = ManifestValidator()
        ok = v.validate_manifest(synth_module, "demo")
        assert ok, f"Errors: {v.errors}"
        assert v.errors == []

    def test_options_key_has_no_min_max_step(self, synth_module):
        """Key з options не повинен мати min/max/step."""
        state = synth_module["state"]["demo.mode"]
        assert "min" not in state
        assert "max" not in state
        assert "step" not in state

    def test_options_key_has_persist_and_default(self, synth_module):
        state = synth_module["state"]["demo.mode"]
        assert state.get("persist") is True
        assert "default" in state

    def test_select_widget_matches_options_key(self, synth_module):
        """V18: state key з options → UI widget має бути 'select'."""
        widgets = synth_module["ui"]["cards"][0]["widgets"]
        mode_w = [w for w in widgets if w["key"] == "demo.mode"][0]
        assert mode_w["widget"] == "select"

    def test_equipment_requires_have_labels(self, equipment):
        """Реальний equipment.requires — кожна роль має label."""
        for req in equipment["requires"]:
            assert "label" in req, f"Role {req['role']} missing label"

    def test_equipment_validates_ok(self, equipment):
        v = ManifestValidator()
        ok = v.validate_manifest(equipment, "equipment")
        assert ok, f"Errors: {v.errors}"
        assert v.errors == []
