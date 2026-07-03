# -*- coding: utf-8 -*-
"""JSON Schema validation layer (tools/schemas/*.schema.json).

Ловить клас «тихих» помилок з аудиту універсальності: опечатки полів
(persits), string у priority, невідомі widget-типи, однину board-секції.
"""
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from generate_ui import (schema_errors, ManifestValidator,  # noqa: E402
                         DriverManifestValidator, ManifestLoader)

ROOT = Path(__file__).resolve().parent.parent.parent


def _module(**over):
    m = {
        "manifest_version": 1,
        "module": "test_mod",
        "state": {"test_mod.value": {"type": "float", "access": "read"}},
    }
    m.update(over)
    return m


class TestModuleSchema:
    def test_persist_typo_caught(self):
        """Опечатка 'persits' — раніше тихо губила persistence."""
        m = _module(state={"test_mod.sp": {
            "type": "float", "access": "readwrite",
            "min": 0, "max": 10, "step": 1, "persits": True}})
        errs = schema_errors(m, "module", "test_mod")
        assert any("persits" in e for e in errs)

    def test_priority_string_caught(self):
        """String у priority — раніше сирий TypeError у сортуванні."""
        errs = schema_errors(_module(priority="high"), "module", "test_mod")
        assert any("priority" in e for e in errs)

    def test_unknown_widget_caught(self):
        """Невідомий widget-тип — раніше compat=None і перевірку пропускало."""
        m = _module(ui={"cards": [{"title": "T", "widgets": [
            {"key": "test_mod.value", "widget": "gauge_XL"}]}]})
        errs = schema_errors(m, "module", "test_mod")
        assert any("gauge_XL" in e for e in errs)

    def test_unknown_top_level_key_caught(self):
        errs = schema_errors(_module(mqtttt={}), "module", "test_mod")
        assert any("mqtttt" in e for e in errs)

    def test_underscore_notes_allowed(self):
        errs = schema_errors(_module(_note="hello"), "module", "test_mod")
        assert errs == []

    def test_validator_short_circuits_on_schema_error(self):
        v = ManifestValidator()
        ok = v.validate_manifest(_module(priority="high"), "x.json")
        assert not ok and any("schema:" in e for e in v.errors)

    def test_loader_drops_schema_broken_manifest(self, tmp_path):
        """Схемно-битий маніфест не потрапляє у cross_validate (де б впав
        сирим TypeError на неправильному типі 'state') — load повертає None,
        а чиста schema-помилка збирається у validator.errors."""
        moddir = tmp_path / "badmod"
        moddir.mkdir()
        # state значення має бути об'єктом, тут — рядок
        (moddir / "manifest.json").write_text(
            json.dumps({"manifest_version": 1, "module": "badmod",
                        "state": {"badmod.x": "float"}}), encoding="utf-8")
        v = ManifestValidator()
        loader = ManifestLoader(tmp_path, v)
        assert loader.load_module("badmod") is None
        assert any("schema:" in e for e in v.errors)
        # load_all не має кинути виняток на битому маніфесті
        v2 = ManifestValidator()
        manifests = ManifestLoader(tmp_path, v2).load_all(["badmod"])
        assert manifests == [] and any("schema:" in e for e in v2.errors)


class TestBoardBindingsSchema:
    def test_singular_section_caught(self):
        """'gpio_output' (однина) — раніше мовчки ігнорувалося."""
        board = {"board": "b", "gpio_output": [{"id": "r1", "gpio": 5}]}
        errs = schema_errors(board, "board", "board")
        assert any("gpio_output" in e for e in errs)

    def test_expander_address_must_be_hex_string(self):
        board = {"board": "b", "i2c_expanders": [
            {"id": "exp", "bus": "i2c_0", "chip": "pcf8574", "address": 36}]}
        errs = schema_errors(board, "board", "board")
        assert errs  # число замість hex-рядка — device парсить strtol(...,16)

    def test_binding_extra_field_caught(self):
        b = {"manifest_version": 1, "bindings": [{
            "hardware": "ow_1", "driver": "ds18b20", "role": "t",
            "module": "equipment", "adress": "28:FF"}]}
        errs = schema_errors(b, "bindings", "bindings")
        assert any("adress" in e for e in errs)


class TestRealRepoFilesPass:
    """Жоден реальний файл репо не має ламатися об схеми."""

    def test_all_real_files_valid(self):
        checks = (
            [("module", p) for p in (ROOT / "modules").glob("*/manifest.json")]
            + [("driver", p) for p in (ROOT / "drivers").glob("*/manifest.json")]
            + [("board", p) for p in (ROOT / "boards").glob("*/board.json")]
            + [("bindings", p) for p in (ROOT / "boards").glob("*/bindings.json")]
            + [("project", ROOT / "project.json")]
        )
        assert len(checks) > 20
        failures = []
        for schema, p in checks:
            data = json.loads(p.read_text(encoding="utf-8"))
            failures += [f"{p.name}: {e}" for e in schema_errors(data, schema, p.stem)]
        assert failures == []


class TestDriverSchema:
    def test_driver_settings_typo(self):
        d = {"manifest_version": 1, "driver": "x", "category": "sensor",
             "hardware_type": "gpio_input", "provides": {"type": "bool"},
             "settings": [{"key": "beta", "type": "float", "defalt": 3950}]}
        v = DriverManifestValidator()
        v.validate(d, "x.json")
        assert any("schema:" in e for e in v.errors)
