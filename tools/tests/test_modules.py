"""
test_modules.py — integration tests for the framework template's real manifests.

Verifies the template's production manifests (whatever project.json lists —
currently equipment, datalogger, simple_thermo, abs_test, display):
  1. pass ManifestValidator (V1-V20),
  2. pass cross-module validation,
  3. generate coherent ui.json / state_meta.h / mqtt_topics.h / display_screens.h.

Where a total would be brittle (it shifts every time a manifest gains a key) the
expected value is computed from the manifests themselves; only stable, meaningful
facts are pinned as literals.
"""
import json
import re
import sys
from pathlib import Path

import pytest

# tools/ on sys.path so we can import generate_ui
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

PROJECT_ROOT = Path(__file__).parent.parent.parent
MODULES_DIR = PROJECT_ROOT / "modules"


def load_manifest(module_name):
    """Load a module's real manifest.json."""
    with open(MODULES_DIR / module_name / "manifest.json", "r", encoding="utf-8") as f:
        return json.load(f)


def load_project():
    with open(PROJECT_ROOT / "project.json", "r", encoding="utf-8") as f:
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
    return load_manifest("equipment")


# ═══════════════════════════════════════════════════════════════
#  All manifests validate (per-module + cross-module)
# ═══════════════════════════════════════════════════════════════

class TestManifestsValidate:
    def test_every_manifest_passes(self, all_manifests):
        """Each active manifest passes ManifestValidator with zero errors."""
        for m in all_manifests:
            v = ManifestValidator()
            ok = v.validate_manifest(m, m["module"])
            assert ok, f"{m['module']}: {v.errors}"
            assert v.errors == []

    def test_cross_module_clean(self, all_manifests, active_modules):
        """Cross-module validation (dup keys, inputs, visible_when) is clean."""
        v = ManifestValidator()
        v.validate_cross_module(all_manifests, active_modules)
        assert v.errors == [], f"errors: {v.errors}"
        assert v.warnings == [], f"warnings: {v.warnings}"

    def test_no_duplicate_state_keys(self, all_manifests):
        v = ManifestValidator()
        v.validate_cross_module(all_manifests)
        assert [e for e in v.errors if "Duplicate state key" in e] == []


# ═══════════════════════════════════════════════════════════════
#  Equipment — the framework's universal HAL module
# ═══════════════════════════════════════════════════════════════

class TestEquipmentManifest:
    def test_module_name(self, equipment):
        assert equipment["module"] == "equipment"

    def test_validates_ok(self, equipment):
        v = ManifestValidator()
        assert v.validate_manifest(equipment, "equipment"), v.errors

    def test_health_key_is_air_temp_ok(self, equipment):
        """Health key matches what equipment_base.cpp actually publishes
        (role 'air_temp' → equipment.air_temp_ok). The old phantom
        equipment.sensor1_ok — declared but never written — must be gone."""
        assert "equipment.air_temp_ok" in equipment["state"]
        assert "equipment.sensor1_ok" not in equipment["state"]

    def test_mqtt_publishes_air_temp_ok(self, equipment):
        pub = equipment["mqtt"]["publish"]
        assert "equipment.air_temp" in pub
        assert "equipment.air_temp_ok" in pub
        assert "equipment.sensor1_ok" not in pub

    def test_sensor_keys_readonly_settings_readwrite(self, equipment):
        """Sensor/flag keys are read-only; calibration settings are readwrite+persist."""
        readwrite = {"equipment.filter_coeff", "equipment.ntc_beta",
                     "equipment.ntc_r_series", "equipment.ntc_r_nominal",
                     "equipment.ds18b20_offset"}
        for key, info in equipment["state"].items():
            if key in readwrite:
                assert info["access"] == "readwrite", f"{key} not readwrite"
                assert info.get("persist") is True, f"{key} not persisted"
            else:
                assert info["access"] == "read", f"{key} not read-only"

    def test_requires_air_temp_sensor(self, equipment):
        roles = [r["role"] for r in equipment["requires"]]
        assert "air_temp" in roles

    def test_no_inputs(self, equipment):
        """Equipment reads HAL directly, never cross-module inputs."""
        assert "inputs" not in equipment


# ═══════════════════════════════════════════════════════════════
#  Generator: ui.json
# ═══════════════════════════════════════════════════════════════

class TestUIJsonFullProject:
    def test_generates_valid(self, project, all_manifests):
        result = UIJsonGenerator().generate(project, all_manifests)
        assert isinstance(result.get("pages"), list)

    def test_core_pages_present_equipment_absent(self, project, all_manifests):
        """Dashboard/chart/network/system are always present; equipment is not a
        page (its settings live on the bindings page)."""
        result = UIJsonGenerator().generate(project, all_manifests)
        page_ids = [p["id"] for p in result["pages"]]
        for pid in ("dashboard", "chart", "network", "system"):
            assert pid in page_ids, f"missing core page '{pid}'"
        assert "equipment" not in page_ids
        assert "firmware" not in page_ids  # merged into system

    def test_recipe_page_present(self, project, all_manifests):
        """The active recipe module (abs_test) contributes its own UI page."""
        result = UIJsonGenerator().generate(project, all_manifests)
        if any(m["module"] == "abs_test" for m in all_manifests):
            assert "abs_test" in [p["id"] for p in result["pages"]]

    def test_pages_sorted_by_order(self, project, all_manifests):
        result = UIJsonGenerator().generate(project, all_manifests)
        orders = [p.get("order", 50) for p in result["pages"]]
        assert orders == sorted(orders)


# ═══════════════════════════════════════════════════════════════
#  Generator: state_meta.h
# ═══════════════════════════════════════════════════════════════

class TestStateMetaFullProject:
    def test_generates_header(self, all_manifests):
        result = StateMetaGenerator().generate(all_manifests)
        assert "#pragma once" in result
        assert "namespace modesp::gen" in result

    def test_count_matches_writable_plus_persisted(self, all_manifests):
        """STATE_META_COUNT == readwrite keys + read-only-but-persisted keys."""
        expected = sum(
            1
            for m in all_manifests
            for info in m.get("state", {}).values()
            if info.get("access") == "readwrite"
            or (info.get("access") == "read" and info.get("persist"))
        )
        result = StateMetaGenerator().generate(all_manifests)
        assert f"STATE_META_COUNT = {expected}" in result

    def test_writable_setting_included(self, all_manifests):
        result = StateMetaGenerator().generate(all_manifests)
        assert '"equipment.filter_coeff"' in result

    def test_readonly_runtime_key_excluded(self, all_manifests):
        """Read-only, non-persisted keys never enter state_meta."""
        result = StateMetaGenerator().generate(all_manifests)
        assert '"equipment.air_temp"' not in result


# ═══════════════════════════════════════════════════════════════
#  Generator: mqtt_topics.h
# ═══════════════════════════════════════════════════════════════

class TestMqttTopicsFullProject:
    def test_generates_header(self, all_manifests):
        assert "#pragma once" in MqttTopicsGenerator().generate(all_manifests)

    def test_air_temp_ok_published_not_sensor1(self, all_manifests):
        result = MqttTopicsGenerator().generate(all_manifests)
        assert '"equipment.air_temp_ok"' in result
        assert "sensor1_ok" not in result

    def test_subscribe_keys_are_present(self, all_manifests):
        """Equipment calibration settings are MQTT-subscribable."""
        result = MqttTopicsGenerator().generate(all_manifests)
        assert '"equipment.ntc_beta"' in result


# ═══════════════════════════════════════════════════════════════
#  Generator: display_screens.h
# ═══════════════════════════════════════════════════════════════

class TestDisplayScreensFullProject:
    def test_generates_header(self, all_manifests):
        assert "#pragma once" in DisplayScreensGenerator().generate(all_manifests)

    def test_equipment_air_temp_is_a_main_value(self, all_manifests):
        result = DisplayScreensGenerator().generate(all_manifests)
        assert '"equipment.air_temp"' in result

    def test_main_values_count_matches_main_value_manifests(self, all_manifests):
        """MAIN_VALUES_COUNT == number of modules declaring display.main_value."""
        expected = sum(1 for m in all_manifests if m.get("display", {}).get("main_value"))
        result = DisplayScreensGenerator().generate(all_manifests)
        m = re.search(r"MAIN_VALUES_COUNT = (\d+)", result)
        assert m and int(m.group(1)) == expected
