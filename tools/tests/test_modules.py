"""
test_modules.py — тести реальних маніфестів бізнес-модулів

Перевіряє що production manifests (equipment, protection, thermostat, defrost):
1. Проходять ManifestValidator (V1-V13)
2. Проходять cross-module validation (inputs)
3. Коректно генерують ui.json, state_meta.h, mqtt_topics.h, display_screens.h
"""
import json
import sys
from pathlib import Path

import pytest

# Додаємо tools/ до sys.path для імпорту generate_ui
TOOLS_DIR = Path(__file__).parent.parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

from generate_ui import (
    ManifestValidator,
    UIJsonGenerator,
    StateMetaGenerator,
    MqttTopicsGenerator,
    DisplayScreensGenerator,
)

# ═══════════════════════════════════════════════════════════════
#  Шлях до реальних маніфестів
# ═══════════════════════════════════════════════════════════════

PROJECT_ROOT = Path(__file__).parent.parent.parent
MODULES_DIR = PROJECT_ROOT / "modules"


def load_manifest(module_name):
    """Завантажити реальний manifest.json модуля."""
    path = MODULES_DIR / module_name / "manifest.json"
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def load_project():
    """Завантажити реальний project.json."""
    path = PROJECT_ROOT / "project.json"
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


# ═══════════════════════════════════════════════════════════════
#  Fixtures
# ═══════════════════════════════════════════════════════════════

@pytest.fixture
def equipment():
    return load_manifest("equipment")


@pytest.fixture
def protection():
    return load_manifest("protection")


@pytest.fixture
def thermostat():
    return load_manifest("thermostat")


@pytest.fixture
def defrost():
    return load_manifest("defrost")


@pytest.fixture
def datalogger():
    return load_manifest("datalogger")


@pytest.fixture
def all_manifests(equipment, protection, thermostat, defrost, datalogger):
    return [equipment, protection, thermostat, defrost, datalogger]


@pytest.fixture
def project():
    return load_project()


@pytest.fixture
def active_modules():
    return ["equipment", "protection", "thermostat", "defrost"]


# ═══════════════════════════════════════════════════════════════
#  Equipment Module — Manifest Validation
# ═══════════════════════════════════════════════════════════════

class TestEquipmentManifest:
    """Тести маніфесту Equipment Manager."""

    def test_validates_ok(self, equipment):
        """Equipment manifest проходить валідацію."""
        v = ManifestValidator()
        result = v.validate_manifest(equipment, "equipment")
        assert result is True, f"Errors: {v.errors}"
        assert len(v.errors) == 0

    def test_module_name(self, equipment):
        """Модуль називається 'equipment'."""
        assert equipment["module"] == "equipment"

    def test_has_24_state_keys(self, equipment):
        """Equipment має 24 state keys."""
        assert len(equipment["state"]) == 24

    def test_sensor_keys_readonly(self, equipment):
        """Sensor/actuator state keys — read-only, settings — readwrite."""
        readwrite_keys = {"equipment.filter_coeff", "equipment.ntc_beta",
                          "equipment.ntc_r_series", "equipment.ntc_r_nominal",
                          "equipment.ds18b20_offset"}
        for key, info in equipment["state"].items():
            if key in readwrite_keys:
                assert info["access"] == "readwrite", f"{key} не readwrite"
            else:
                assert info["access"] == "read", f"{key} не read-only"

    def test_sensor_keys(self, equipment):
        """Має ключі сенсорів."""
        assert "equipment.air_temp" in equipment["state"]
        assert "equipment.evap_temp" in equipment["state"]
        assert "equipment.sensor1_ok" in equipment["state"]
        assert "equipment.sensor2_ok" in equipment["state"]

    def test_actuator_keys(self, equipment):
        """Має ключі актуаторів."""
        for key in ["compressor", "defrost_relay", "evap_fan", "cond_fan"]:
            assert f"equipment.{key}" in equipment["state"]

    def test_door_key(self, equipment):
        """Має ключ дверей."""
        assert "equipment.door_open" in equipment["state"]

    def test_requires_drivers(self, equipment):
        """Equipment вимагає air_temp sensor і compressor relay."""
        roles = [r["role"] for r in equipment["requires"]]
        assert "air_temp" in roles
        assert "compressor" in roles

    def test_optional_drivers(self, equipment):
        """Додаткові drivers опціональні."""
        optional_roles = {r["role"]: r.get("optional", False) for r in equipment["requires"]}
        assert optional_roles.get("evap_temp") is True
        assert optional_roles.get("defrost_relay") is True
        assert optional_roles.get("cond_fan") is True

    def test_no_inputs(self, equipment):
        """Equipment не має inputs (читає HAL напряму)."""
        assert "inputs" not in equipment

    def test_mqtt_publish(self, equipment):
        """MQTT publish включає ключові сенсори."""
        pub = equipment["mqtt"]["publish"]
        assert "equipment.air_temp" in pub
        assert "equipment.compressor" in pub

    def test_no_warnings(self, equipment):
        """Без попереджень."""
        v = ManifestValidator()
        v.validate_manifest(equipment, "equipment")
        assert len(v.warnings) == 0


# ═══════════════════════════════════════════════════════════════
#  Protection Module — Manifest Validation
# ═══════════════════════════════════════════════════════════════

class TestProtectionManifest:
    """Тести маніфесту Protection."""

    def test_validates_ok(self, protection):
        """Protection manifest проходить валідацію."""
        v = ManifestValidator()
        result = v.validate_manifest(protection, "protection")
        assert result is True, f"Errors: {v.errors}"
        assert len(v.errors) == 0

    def test_has_38_state_keys(self, protection):
        """Protection має 38 state keys."""
        assert len(protection["state"]) == 38

    def test_alarm_readonly_keys(self, protection):
        """Alarm keys — read-only."""
        ro_keys = ["protection.lockout", "protection.alarm_active",
                    "protection.alarm_code", "protection.high_temp_alarm",
                    "protection.low_temp_alarm", "protection.sensor1_alarm",
                    "protection.sensor2_alarm", "protection.door_alarm"]
        for key in ro_keys:
            assert protection["state"][key]["access"] == "read", f"{key} не read-only"

    def test_settings_readwrite_keys(self, protection):
        """Settings keys — readwrite з persist."""
        rw_keys = ["protection.high_limit", "protection.low_limit",
                    "protection.high_alarm_delay", "protection.low_alarm_delay",
                    "protection.door_delay", "protection.manual_reset"]
        for key in rw_keys:
            info = protection["state"][key]
            assert info["access"] == "readwrite", f"{key} не readwrite"
            assert info.get("persist") is True, f"{key} не persist"

    def test_high_limit_range(self, protection):
        """high_limit: min=-50, max=99."""
        info = protection["state"]["protection.high_limit"]
        assert info["min"] == -50.0
        assert info["max"] == 99.0

    def test_alarm_delay_range(self, protection):
        """high_alarm_delay: min=0, max=120; low_alarm_delay: min=0, max=120."""
        for key in ["protection.high_alarm_delay", "protection.low_alarm_delay"]:
            info = protection["state"][key]
            assert info["min"] == 0
            assert info["max"] == 120

    def test_has_14_inputs(self, protection):
        """Protection має 14 inputs (7 data + 5 thermostat UI-only + 2 defrost)."""
        assert len(protection["inputs"]) == 14

    def test_required_inputs(self, protection):
        """Обов'язкові inputs: equipment.air_temp, equipment.sensor1_ok."""
        assert protection["inputs"]["equipment.air_temp"]["optional"] is False
        assert protection["inputs"]["equipment.sensor1_ok"]["optional"] is False

    def test_optional_inputs(self, protection):
        """Опціональні inputs: sensor2_ok, door_open, defrost.active."""
        assert protection["inputs"]["equipment.sensor2_ok"]["optional"] is True
        assert protection["inputs"]["equipment.door_open"]["optional"] is True
        assert protection["inputs"]["defrost.active"]["optional"] is True

    def test_mqtt_21_publish(self, protection):
        """MQTT публікує 21 alarm/status keys."""
        assert len(protection["mqtt"]["publish"]) == 21

    def test_mqtt_18_subscribe(self, protection):
        """MQTT підписка на 18 settings keys."""
        assert len(protection["mqtt"]["subscribe"]) == 18

    def test_display_main_value(self, protection):
        """Display main_value — alarm_code."""
        assert protection["display"]["main_value"]["key"] == "protection.alarm_code"

    def test_display_8_menu_items(self, protection):
        """Display має 8 menu items."""
        assert len(protection["display"]["menu_items"]) == 8

    def test_reset_alarms_key(self, protection):
        """reset_alarms — readwrite trigger без persist."""
        info = protection["state"]["protection.reset_alarms"]
        assert info["access"] == "readwrite"
        assert info.get("persist") is not True


# ═══════════════════════════════════════════════════════════════
#  Thermostat Module — Manifest Validation
# ═══════════════════════════════════════════════════════════════

class TestThermostatManifest:
    """Тести маніфесту Thermostat v2."""

    def test_validates_ok(self, thermostat):
        """Thermostat manifest проходить валідацію."""
        v = ManifestValidator()
        result = v.validate_manifest(thermostat, "thermostat")
        assert result is True, f"Errors: {v.errors}"
        assert len(v.errors) == 0

    def test_has_26_state_keys(self, thermostat):
        """Thermostat має 26 state keys."""
        assert len(thermostat["state"]) == 26

    def test_16_persist_params(self, thermostat):
        """Thermostat має 16 persist параметрів."""
        persist_count = sum(1 for v in thermostat["state"].values()
                           if v.get("persist") is True)
        assert persist_count == 16

    def test_setpoint_config(self, thermostat):
        """setpoint: min=-50, max=50, default=4, persist=true."""
        sp = thermostat["state"]["thermostat.setpoint"]
        assert sp["min"] == -50.0
        assert sp["max"] == 50.0
        assert sp["default"] == 4.0
        assert sp["persist"] is True

    def test_differential_config(self, thermostat):
        """differential: min=0.5, max=10."""
        d = thermostat["state"]["thermostat.differential"]
        assert d["min"] == 0.5
        assert d["max"] == 10.0

    def test_fan_mode_has_options(self, thermostat):
        """evap_fan_mode: має options замість min/max."""
        fm = thermostat["state"]["thermostat.evap_fan_mode"]
        assert "options" in fm
        assert len(fm["options"]) == 3
        values = [o["value"] for o in fm["options"]]
        assert values == [0, 1, 2]

    def test_request_keys(self, thermostat):
        """Thermostat має 3 request keys."""
        req_keys = [k for k in thermostat["state"] if k.startswith("thermostat.req.")]
        assert len(req_keys) == 3
        assert "thermostat.req.compressor" in thermostat["state"]
        assert "thermostat.req.evap_fan" in thermostat["state"]
        assert "thermostat.req.cond_fan" in thermostat["state"]

    def test_state_enum(self, thermostat):
        """thermostat.state має enum з 4 значеннями."""
        s = thermostat["state"]["thermostat.state"]
        assert set(s["enum"]) == {"startup", "idle", "cooling", "safety_run"}

    def test_has_10_inputs(self, thermostat):
        """Thermostat має 10 inputs (protection widgets перенесено в protection page)."""
        assert len(thermostat["inputs"]) == 10

    def test_equipment_inputs(self, thermostat):
        """Reads equipment.air_temp, equipment.sensor1_ok (required)."""
        assert thermostat["inputs"]["equipment.air_temp"]["optional"] is False
        assert thermostat["inputs"]["equipment.sensor1_ok"]["optional"] is False
        assert thermostat["inputs"]["equipment.compressor"]["optional"] is False

    def test_optional_defrost_inputs(self, thermostat):
        """defrost.active та defrost.phase опціональні."""
        assert thermostat["inputs"]["defrost.active"]["optional"] is True
        assert thermostat["inputs"]["defrost.phase"]["optional"] is True

    def test_protection_lockout_input(self, thermostat):
        """protection.lockout — опціональний input."""
        assert thermostat["inputs"]["protection.lockout"]["optional"] is True

    def test_mqtt_10_publish(self, thermostat):
        """MQTT публікує 10 keys."""
        assert len(thermostat["mqtt"]["publish"]) == 10

    def test_mqtt_17_subscribe(self, thermostat):
        """MQTT підписка на 17 settings."""
        assert len(thermostat["mqtt"]["subscribe"]) == 17

    def test_cross_module_widget_key(self, thermostat):
        """UI використовує equipment.compressor як cross-module widget."""
        all_widget_keys = []
        for card in thermostat["ui"]["cards"]:
            for w in card.get("widgets", []):
                all_widget_keys.append(w["key"])
        assert "equipment.compressor" in all_widget_keys


# ═══════════════════════════════════════════════════════════════
#  Defrost Module — Manifest Validation
# ═══════════════════════════════════════════════════════════════

class TestDefrostManifest:
    """Тести маніфесту Defrost."""

    def test_validates_ok(self, defrost):
        """Defrost manifest проходить валідацію."""
        v = ManifestValidator()
        result = v.validate_manifest(defrost, "defrost")
        assert result is True, f"Errors: {v.errors}"
        assert len(v.errors) == 0

    def test_has_28_state_keys(self, defrost):
        """Defrost має 28 state keys."""
        assert len(defrost["state"]) == 28

    def test_14_persist_readwrite_params(self, defrost):
        """Defrost має 14 readwrite persist параметрів."""
        rw_persist = sum(1 for v in defrost["state"].values()
                         if v.get("access") == "readwrite" and v.get("persist") is True)
        assert rw_persist == 14

    def test_no_readonly_persist_params(self, defrost):
        """Defrost не має read-only persist params (interval_timer/defrost_count скидаються при ребуті)."""
        ro_persist = sum(1 for v in defrost["state"].values()
                         if v.get("access") == "read" and v.get("persist") is True)
        assert ro_persist == 0

    def test_interval_timer_no_persist(self, defrost):
        """interval_timer: read-only, без persist (скидається при ребуті)."""
        it = defrost["state"]["defrost.interval_timer"]
        assert it["access"] == "read"
        assert it.get("persist") is not True

    def test_defrost_count_no_persist(self, defrost):
        """defrost_count: read-only, без persist (скидається при ребуті)."""
        dc = defrost["state"]["defrost.defrost_count"]
        assert dc["access"] == "read"
        assert dc.get("persist") is not True

    def test_phase_enum(self, defrost):
        """defrost.phase має 7 enum значень."""
        p = defrost["state"]["defrost.phase"]
        expected = {"idle", "stabilize", "valve_open", "active", "equalize", "drip", "fad"}
        assert set(p["enum"]) == expected

    def test_type_has_options(self, defrost):
        """defrost.type: має options (0=stop, 1=heater, 2=hot gas)."""
        t = defrost["state"]["defrost.type"]
        assert "options" in t
        values = [o["value"] for o in t["options"]]
        assert values == [0, 1, 2]

    def test_initiation_has_options(self, defrost):
        """defrost.initiation: має options (0-3)."""
        i = defrost["state"]["defrost.initiation"]
        assert "options" in i
        values = [o["value"] for o in i["options"]]
        assert values == [0, 1, 2, 3]

    def test_4_request_keys(self, defrost):
        """Defrost має 4 request keys."""
        req_keys = [k for k in defrost["state"] if k.startswith("defrost.req.")]
        assert len(req_keys) == 4
        expected = {"defrost.req.compressor", "defrost.req.defrost_relay",
                    "defrost.req.evap_fan", "defrost.req.cond_fan"}
        assert set(req_keys) == expected

    def test_has_6_inputs(self, defrost):
        """Defrost має 6 inputs."""
        assert len(defrost["inputs"]) == 6

    def test_compressor_input_required(self, defrost):
        """equipment.compressor — обов'язковий input (для dct=2)."""
        assert defrost["inputs"]["equipment.compressor"]["optional"] is False

    def test_optional_inputs(self, defrost):
        """evap_temp, sensor2_ok, protection.lockout — опціональні."""
        assert defrost["inputs"]["equipment.evap_temp"]["optional"] is True
        assert defrost["inputs"]["equipment.sensor2_ok"]["optional"] is True
        assert defrost["inputs"]["protection.lockout"]["optional"] is True

    def test_manual_start_trigger(self, defrost):
        """manual_start — readwrite trigger без persist."""
        ms = defrost["state"]["defrost.manual_start"]
        assert ms["access"] == "readwrite"
        assert ms.get("persist") is not True

    def test_mqtt_10_publish(self, defrost):
        """MQTT публікує 10 keys."""
        assert len(defrost["mqtt"]["publish"]) == 10

    def test_mqtt_15_subscribe(self, defrost):
        """MQTT підписка на 15 settings."""
        assert len(defrost["mqtt"]["subscribe"]) == 15

    def test_end_temp_range(self, defrost):
        """end_temp: min=-5, max=30, default=8."""
        et = defrost["state"]["defrost.end_temp"]
        assert et["min"] == -5.0
        assert et["max"] == 30.0
        assert et["default"] == 8.0

    def test_hot_gas_params(self, defrost):
        """Hot gas params (dFT=2) exist with correct ranges (minutes/seconds)."""
        st = defrost["state"]["defrost.stabilize_time"]
        assert st["min"] == 0
        assert st["max"] == 10
        assert st["unit"] == "хв"

        vd = defrost["state"]["defrost.valve_delay"]
        assert vd["min"] == 1
        assert vd["max"] == 30
        assert vd["unit"] == "с"

        eq = defrost["state"]["defrost.equalize_time"]
        assert eq["min"] == 0
        assert eq["max"] == 10
        assert eq["unit"] == "хв"

    def test_display_4_menu_items(self, defrost):
        """Display має 4 menu items."""
        assert len(defrost["display"]["menu_items"]) == 4


# ═══════════════════════════════════════════════════════════════
#  Cross-Module Validation
# ═══════════════════════════════════════════════════════════════

class TestCrossModuleValidation:
    """Перевірка cross-module валідації всіх 4 модулів разом."""

    def test_no_duplicate_state_keys(self, all_manifests):
        """Немає дублікатів state keys між модулями."""
        v = ManifestValidator()
        v.validate_cross_module(all_manifests)
        dup_errors = [e for e in v.errors if "Duplicate state key" in e]
        assert len(dup_errors) == 0, f"Duplicates: {dup_errors}"

    def test_inputs_cross_validation(self, all_manifests, active_modules):
        """Inputs всіх модулів валідні (source_module існує, типи збігаються)."""
        v = ManifestValidator()
        v.validate_cross_module(all_manifests, active_modules)
        assert len(v.errors) == 0, f"Errors: {v.errors}"

    def test_no_warnings_with_all_modules(self, all_manifests, active_modules):
        """Без warnings коли всі 4 модулі присутні."""
        v = ManifestValidator()
        v.validate_cross_module(all_manifests, active_modules)
        # Допускаємо warnings тільки для key prefix (cross-module inputs)
        non_prefix_warnings = [w for w in v.warnings
                                if "does not start with" not in w
                                and "optional" not in w.lower()]
        assert len(non_prefix_warnings) == 0, f"Warnings: {non_prefix_warnings}"

    def test_protection_reads_equipment(self, all_manifests, active_modules):
        """Protection inputs резолвляться до equipment state keys."""
        v = ManifestValidator()
        v.validate_cross_module(all_manifests, active_modules)
        # Помилки пов'язані з protection inputs мають бути відсутні
        prot_errors = [e for e in v.errors if "protection" in e.lower()]
        assert len(prot_errors) == 0, f"Protection errors: {prot_errors}"

    def test_thermostat_reads_equipment_and_defrost(self, all_manifests, active_modules):
        """Thermostat inputs з equipment та defrost резолвляться."""
        v = ManifestValidator()
        v.validate_cross_module(all_manifests, active_modules)
        therm_errors = [e for e in v.errors if "thermostat" in e.lower()]
        assert len(therm_errors) == 0, f"Thermostat errors: {therm_errors}"

    def test_total_state_keys(self, all_manifests):
        """Всього 126 state keys у 5 модулях."""
        total = sum(len(m.get("state", {})) for m in all_manifests)
        assert total == 126


# ═══════════════════════════════════════════════════════════════
#  Generator: UI JSON — з повним набором модулів
# ═══════════════════════════════════════════════════════════════

class TestUIJsonFullProject:
    """Генерація ui.json з усіма 4 модулями."""

    def test_generates_valid_json(self, project, all_manifests):
        """ui.json генерується без помилок."""
        gen = UIJsonGenerator()
        result = gen.generate(project, all_manifests)
        assert "pages" in result
        assert isinstance(result["pages"], list)

    def test_pages(self, project, all_manifests):
        """Сторінки: Dashboard + 4 module + Bindings + Network + System (8 total)."""
        gen = UIJsonGenerator()
        result = gen.generate(project, all_manifests)
        page_ids = [p["id"] for p in result["pages"]]
        assert "dashboard" in page_ids
        assert "thermostat" in page_ids
        assert "defrost" in page_ids
        assert "protection" in page_ids
        assert "chart" in page_ids
        assert "network" in page_ids
        assert "system" in page_ids
        # equipment UI видалено — налаштування на сторінці bindings
        assert "equipment" not in page_ids
        assert "sensors" not in page_ids
        # firmware merged into system
        assert "firmware" not in page_ids

    def test_pages_sorted_by_order(self, project, all_manifests):
        """Сторінки відсортовані по order."""
        gen = UIJsonGenerator()
        result = gen.generate(project, all_manifests)
        orders = [p.get("order", 50) for p in result["pages"]]
        assert orders == sorted(orders)

    def test_state_meta_has_all_keys(self, project, all_manifests):
        """state_meta містить ключі від усіх модулів."""
        gen = UIJsonGenerator()
        result = gen.generate(project, all_manifests)
        meta = result["state_meta"]
        assert "equipment.air_temp" in meta
        assert "protection.high_limit" in meta
        assert "thermostat.setpoint" in meta
        assert "defrost.type" in meta
        assert "defrost.interval_timer" in meta

    def test_defrost_page_has_3_cards(self, project, all_manifests):
        """Сторінка Defrost має 3 cards (Стан, Налаштування, Гарячий газ)."""
        gen = UIJsonGenerator()
        result = gen.generate(project, all_manifests)
        defrost_page = next(p for p in result["pages"] if p["id"] == "defrost")
        assert len(defrost_page["cards"]) == 3

    def test_protection_page_has_alarm_cards(self, project, all_manifests):
        """Protection має окрему сторінку з 4 картками."""
        gen = UIJsonGenerator()
        result = gen.generate(project, all_manifests)
        prot_page = next(p for p in result["pages"] if p["id"] == "protection")
        card_titles = [c["title"] for c in prot_page["cards"]]
        assert "Статус аварій" in card_titles
        assert "Діагностика компресора" in card_titles
        assert "Налаштування захисту" in card_titles
        assert "Компресор" in card_titles
        # Alarm widgets на сторінці protection
        status_card = next(c for c in prot_page["cards"] if c["title"] == "Статус аварій")
        status_keys = [w["key"] for w in status_card["widgets"]]
        assert "protection.alarm_active" in status_keys
        assert "protection.alarm_code" in status_keys
        assert "protection.reset_alarms" in status_keys


# ═══════════════════════════════════════════════════════════════
#  Generator: State Meta — з повним набором модулів
# ═══════════════════════════════════════════════════════════════

class TestStateMetaFullProject:
    """Генерація state_meta.h з усіма 4 модулями."""

    def test_generates_header(self, all_manifests):
        """Генерує C++ header."""
        gen = StateMetaGenerator()
        result = gen.generate(all_manifests)
        assert "#pragma once" in result
        assert "namespace modesp::gen" in result

    def test_contains_readwrite_keys(self, all_manifests):
        """Містить readwrite ключі з усіх модулів."""
        gen = StateMetaGenerator()
        result = gen.generate(all_manifests)
        assert '"thermostat.setpoint"' in result
        assert '"protection.high_limit"' in result
        assert '"defrost.type"' in result
        assert '"defrost.interval"' in result

    def test_readonly_runtime_keys_excluded(self, all_manifests):
        """Read-only runtime keys (без persist) не в state_meta."""
        gen = StateMetaGenerator()
        result = gen.generate(all_manifests)
        assert '"defrost.interval_timer"' not in result
        assert '"defrost.defrost_count"' not in result

    def test_equipment_excluded(self, all_manifests):
        """Equipment read-only keys (без persist) не в state_meta."""
        gen = StateMetaGenerator()
        result = gen.generate(all_manifests)
        assert '"equipment.air_temp"' not in result
        assert '"equipment.compressor"' not in result

    def test_meta_count(self, all_manifests):
        """STATE_META_COUNT = readwrite keys only (no readonly persist)."""
        gen = StateMetaGenerator()
        result = gen.generate(all_manifests)
        # Рахуємо: equipment=0rw, protection=8rw, thermostat=17rw, defrost=15rw(14+manual_start)
        # protection: high_limit, low_limit, high_alarm_delay, low_alarm_delay,
        #   door_delay, manual_reset, reset_alarms, post_defrost_delay = 8
        # equipment: filter_coeff, ntc_beta, ntc_r_series, ntc_r_nominal, ds18b20_offset = 5
        # protection: high_limit, low_limit, high_alarm_delay, low_alarm_delay,
        #   door_delay, manual_reset, post_defrost_delay, reset_alarms = 8
        # thermostat: setpoint, differential, min_off_time, min_on_time, startup_delay,
        #   evap_fan_mode, fan_stop_temp, fan_stop_hyst, cond_fan_delay, safety_run_on,
        #   safety_run_off, night_setback, night_mode, night_start, night_end,
        #   night_active, display_defrost = 17
        # defrost: type, interval, counter_mode, initiation, termination, end_temp,
        #   max_duration, demand_temp, drip_time, fan_delay, fad_temp,
        #   stabilize_time, valve_delay, equalize_time, manual_start, manual_stop = 16 rw
        # datalogger: enabled, retention_hours, sample_interval, log_evap, log_cond,
        #   log_setpoint, log_humidity = 7 rw
        # Total: 61 (auto-counted from manifests)
        assert "STATE_META_COUNT = 63" in result

    def test_persist_true_for_setpoint(self, all_manifests):
        """thermostat.setpoint — writable=true, persist=true."""
        gen = StateMetaGenerator()
        result = gen.generate(all_manifests)
        for line in result.split("\n"):
            if '"thermostat.setpoint"' in line:
                assert "true, true," in line
                break

    def test_persist_false_for_reset_alarms(self, all_manifests):
        """protection.reset_alarms — writable=true, persist=false."""
        gen = StateMetaGenerator()
        result = gen.generate(all_manifests)
        for line in result.split("\n"):
            if '"protection.reset_alarms"' in line:
                assert "true, false," in line
                break


# ═══════════════════════════════════════════════════════════════
#  Generator: MQTT Topics — з повним набором модулів
# ═══════════════════════════════════════════════════════════════

class TestMqttTopicsFullProject:
    """Генерація mqtt_topics.h з усіма 4 модулями."""

    def test_generates_header(self, all_manifests):
        """Генерує C++ header."""
        gen = MqttTopicsGenerator()
        result = gen.generate(all_manifests)
        assert "#pragma once" in result

    def test_publish_count(self, all_manifests):
        """Загальна кількість MQTT publish topics."""
        gen = MqttTopicsGenerator()
        result = gen.generate(all_manifests)
        # equipment=6, protection=19, thermostat=10, defrost=10, datalogger=3 = 48
        assert "MQTT_PUBLISH_COUNT = 50" in result

    def test_subscribe_count(self, all_manifests):
        """Загальна кількість MQTT subscribe topics."""
        gen = MqttTopicsGenerator()
        result = gen.generate(all_manifests)
        # equipment=5, protection=16, thermostat=17, defrost=15, datalogger=7 = 60
        assert "MQTT_SUBSCRIBE_COUNT = 62" in result

    def test_contains_all_module_topics(self, all_manifests):
        """Містить topics від усіх модулів."""
        gen = MqttTopicsGenerator()
        result = gen.generate(all_manifests)
        assert '"equipment.air_temp"' in result
        assert '"protection.alarm_active"' in result
        assert '"thermostat.temperature"' in result
        assert '"defrost.active"' in result


# ═══════════════════════════════════════════════════════════════
#  Generator: Display Screens — з повним набором модулів
# ═══════════════════════════════════════════════════════════════

class TestDisplayScreensFullProject:
    """Генерація display_screens.h з усіма 4 модулями."""

    def test_generates_header(self, all_manifests):
        """Генерує C++ header."""
        gen = DisplayScreensGenerator()
        result = gen.generate(all_manifests)
        assert "#pragma once" in result

    def test_4_main_values(self, all_manifests):
        """4 main values (по одному на модуль)."""
        gen = DisplayScreensGenerator()
        result = gen.generate(all_manifests)
        assert "MAIN_VALUES_COUNT = 4" in result

    def test_main_value_keys(self, all_manifests):
        """Main values від кожного модуля."""
        gen = DisplayScreensGenerator()
        result = gen.generate(all_manifests)
        assert '"equipment.air_temp"' in result
        assert '"protection.alarm_code"' in result
        assert '"thermostat.temperature"' in result
        assert '"defrost.state"' in result

    def test_menu_items_count(self, all_manifests):
        """Сума menu items: equipment=0, protection=5, thermostat=5, defrost=4 = 14."""
        gen = DisplayScreensGenerator()
        result = gen.generate(all_manifests)
        assert "MENU_ITEMS_COUNT = 17" in result
