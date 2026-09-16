# -*- coding: utf-8 -*-
"""ModESP-CAN v1 registry (ADR-004): стабільні ID, колізії, типи, емісія.

Дзеркальні тест-вектори djb2-fold16 / CRC-16 живуть також у
tests/host/test_can_codec.cpp — обидві реалізації (Python-генератор і C++
runtime) мають давати однакові значення, інакше підписка драйвера-моста
не зійдеться з ID відправника.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from generate_ui import (can_djb2_32, can_fold16, can_crc16_ccitt,  # noqa: E402
                         collect_can_registry, can_registry_crc16,
                         generate_can_registry_header, schema_errors,
                         load_capabilities)

CAPS = load_capabilities()


def _module(**over):
    m = {
        "manifest_version": 1,
        "module": "test_mod",
        "state": {"test_mod.value": {"type": "float", "access": "read"}},
    }
    m.update(over)
    return m


# ── Тест-вектори хешу (СПІЛЬНІ з tests/host/test_can_codec.cpp) ──────

class TestHashVectors:
    def test_djb2_known_vectors(self):
        # djb2("") = 5381; далі — класичні значення hash*33+c
        assert can_djb2_32("") == 5381
        assert can_djb2_32("a") == (5381 * 33 + ord("a")) & 0xFFFFFFFF

    def test_fold16_stability(self):
        """Значення зафіксовані НАЗАВЖДИ — це wire-контракт ModESP-CAN v1.
        Якщо цей тест упав — ви зламали сумісність із прошитими вузлами."""
        assert can_fold16("equipment.air_temp") == 0xE753
        assert can_fold16("simple_thermo.temperature") == 0x2B0B
        assert can_fold16("keypad.cmd_start") == 0x4184

    def test_fold16_never_zero(self):
        # 0 зарезервовано (invalid); будь-який вхід → 1..0xFFFF
        for s in ("", "a", "equipment.x"):
            assert 1 <= can_fold16(s) <= 0xFFFF

    def test_crc16_ccitt_false_vector(self):
        # Канонічний вектор CRC-16/CCITT-FALSE("123456789") = 0x29B1
        assert can_crc16_ccitt(b"123456789") == 0x29B1


# ── Збирання реєстру з маніфестів ────────────────────────────────────

class TestCollectExports:
    def test_state_key_export(self):
        errs = []
        m = _module(state={"test_mod.level": {
            "type": "float", "access": "read", "can_export": True}})
        exports, events = collect_can_registry([m], CAPS, errs)
        assert errs == []
        assert exports == [{"key": "test_mod.level",
                            "id": can_fold16("test_mod.level"), "type": "float"}]
        assert events == []

    def test_string_export_rejected(self):
        errs = []
        m = _module(state={"test_mod.name": {
            "type": "string", "access": "read", "can_export": True}})
        collect_can_registry([m], CAPS, errs)
        assert any("not supported over CAN v1" in e for e in errs)

    def test_explicit_can_id_wins(self):
        errs = []
        m = _module(state={"test_mod.level": {
            "type": "int", "access": "read", "can_export": True, "can_id": 0x120}})
        exports, _ = collect_can_registry([m], CAPS, errs)
        assert errs == []
        assert exports[0]["id"] == 0x120

    def test_can_id_out_of_range(self):
        errs = []
        m = _module(state={"test_mod.level": {
            "type": "int", "access": "read", "can_export": True, "can_id": 0}})
        collect_can_registry([m], CAPS, errs)
        assert any("1..65535" in e for e in errs)

    def test_id_collision_detected(self):
        errs = []
        m = _module(state={
            "test_mod.a": {"type": "float", "access": "read",
                           "can_export": True, "can_id": 0x100},
            "test_mod.b": {"type": "float", "access": "read",
                           "can_export": True, "can_id": 0x100},
        })
        collect_can_registry([m], CAPS, errs)
        assert any("collides" in e for e in errs)

    def test_same_key_conflicting_ids_rejected(self):
        """Той самий ключ (state + requires-роль) з різними can_id → ERROR,
        а не два arb-ID для одного значення."""
        errs = []
        m1 = _module(state={"equipment.air_temp": {
            "type": "float", "access": "read", "can_export": True, "can_id": 0x200}})
        m2 = _module(module="other",
                     state={"other.v": {"type": "bool", "access": "read"}},
                     requires=[{"role": "air_temp", "type": "sensor",
                                "capability": "temperature",
                                "can_export": True, "can_id": 0x300}])
        collect_can_registry([m1, m2], CAPS, errs)
        assert any("different ids" in e for e in errs)

    def test_same_key_conflicting_types_rejected(self):
        """Той самий ключ із різними wire-типами → ERROR (тип не має залежати
        від порядку маніфестів)."""
        errs = []
        m1 = _module(state={"equipment.door": {
            "type": "int", "access": "read", "can_export": True}})
        m2 = _module(module="other",
                     state={"other.v": {"type": "bool", "access": "read"}},
                     requires=[{"role": "door", "type": "sensor",
                                "capability": "binary_in", "can_export": True}])
        collect_can_registry([m1, m2], CAPS, errs)
        assert any("different types" in e for e in errs)

    def test_same_key_consistent_redeclaration_ok(self):
        errs = []
        m = _module(state={"test_mod.level": {
            "type": "float", "access": "read", "can_export": True}})
        exports, _ = collect_can_registry([m, dict(m)], CAPS, errs)
        assert errs == []
        assert len(exports) == 1

    def test_long_key_warns_about_ok_companion(self):
        errs, warns = [], []
        key = "test_mod." + "x" * 21   # 30 символів > 29
        m = _module(state={key: {"type": "float", "access": "read",
                                 "can_export": True}})
        collect_can_registry([m], CAPS, errs, warns)
        assert errs == []
        assert any("_ok" in w for w in warns)

    def test_role_export_uses_capability_type(self):
        """requires[]-роль з can_export → ключ equipment.<role>, тип із capability."""
        errs = []
        m = _module(requires=[{"role": "air_temp", "type": "sensor",
                               "capability": "temperature", "can_export": True}])
        exports, _ = collect_can_registry([m], CAPS, errs)
        assert errs == []
        assert exports == [{"key": "equipment.air_temp",
                            "id": can_fold16("equipment.air_temp"), "type": "float"}]

    def test_role_export_requires_capability(self):
        errs = []
        m = _module(requires=[{"role": "air_temp", "type": "sensor",
                               "can_export": True}])
        collect_can_registry([m], CAPS, errs)
        assert any("requires an explicit 'capability'" in e for e in errs)

    def test_role_export_unknown_capability(self):
        errs = []
        m = _module(requires=[{"role": "x", "type": "sensor",
                               "capability": "no_such_cap", "can_export": True}])
        collect_can_registry([m], CAPS, errs)
        assert any("not found in tools/capabilities.json" in e for e in errs)


class TestCollectEvents:
    def test_out_event_gets_module_prefix(self):
        errs = []
        m = _module(module="keypad", can_events={"out": {"cmd_start": {}}})
        _, events = collect_can_registry([m], CAPS, errs)
        assert errs == []
        assert events == [{"name": "keypad.cmd_start",
                           "id": can_fold16("keypad.cmd_start"), "outbound": True}]

    def test_out_event_with_dots_rejected(self):
        errs = []
        m = _module(can_events={"out": {"other.event": {}}})
        collect_can_registry([m], CAPS, errs)
        assert any("БЕЗ крапок" in e for e in errs)

    def test_in_event_must_be_qualified(self):
        errs = []
        m = _module(can_events={"in": {"overtemp": {}}})
        collect_can_registry([m], CAPS, errs)
        assert any("кваліфікованою" in e for e in errs)

    def test_in_event_qualified_ok(self):
        errs = []
        m = _module(can_events={"in": {"alarm.overtemp": {}}})
        _, events = collect_can_registry([m], CAPS, errs)
        assert errs == []
        assert events == [{"name": "alarm.overtemp",
                           "id": can_fold16("alarm.overtemp"), "outbound": False}]

    def test_same_event_conflicting_ids_rejected(self):
        """out без can_id + in з явним can_id на ту саму подію → ERROR
        (конфліктний can_id не має тихо губитися)."""
        errs = []
        sender = _module(module="keypad", can_events={"out": {"cmd": {}}})
        listener = _module(module="actuator",
                           state={"actuator.v": {"type": "bool", "access": "read"}},
                           can_events={"in": {"keypad.cmd": {"can_id": 5}}})
        collect_can_registry([sender, listener], CAPS, errs)
        assert any("different ids" in e for e in errs)

    def test_same_event_in_and_out_merges(self):
        """Вузол і шле, і слухає одну подію (loopback-сценарій) — один запис outbound=True."""
        errs = []
        sender = _module(module="keypad", can_events={"out": {"cmd": {}}})
        listener = _module(module="actuator",
                           state={"actuator.v": {"type": "bool", "access": "read"}},
                           can_events={"in": {"keypad.cmd": {}}})
        _, events = collect_can_registry([sender, listener], CAPS, errs)
        assert errs == []
        assert len(events) == 1
        assert events[0]["outbound"] is True


# ── CRC реєстру + емісія header ──────────────────────────────────────

class TestEmission:
    def _registry(self):
        errs = []
        m = _module(
            module="tank",
            state={"tank.level": {"type": "float", "access": "read",
                                  "can_export": True, "can_id": 0x120}},
            can_events={"out": {"drained": {"can_id": 40}},
                        "in": {"alarm.overtemp": {"can_id": 12}}},
        )
        exports, events = collect_can_registry([m], CAPS, errs)
        assert errs == []
        return exports, events

    def test_header_contents(self):
        exports, events = self._registry()
        h = generate_can_registry_header(exports, events)
        assert '{"tank.level", 0x0120, CanValType::F32},' in h
        assert '{"tank.drained", 0x0028, true},' in h
        assert '{"alarm.overtemp", 0x000C, false},' in h
        assert "CAN_EXPORT_COUNT = 1" in h
        assert "CAN_EVENT_COUNT = 2" in h
        assert "CAN_PROTO_VER = 1" in h

    def test_empty_registry_has_sentinels(self):
        h = generate_can_registry_header([], [])
        assert "CAN_EXPORT_COUNT = 0" in h
        assert "CAN_EVENT_COUNT = 0" in h
        assert "sentinel" in h
        assert "CAN_REGISTRY_CRC16 = 0x" in h

    def test_crc_covers_exports_and_out_events_only(self):
        exports, events = self._registry()
        crc_all = can_registry_crc16(exports, events)
        # Вхідні (in) події НЕ входять у CRC контракту — вони не частина того,
        # що вузол ВІДДАЄ на шину.
        crc_no_in = can_registry_crc16(
            exports, [e for e in events if e["outbound"]])
        assert crc_all == crc_no_in

    def test_deterministic(self):
        exports, events = self._registry()
        assert (generate_can_registry_header(exports, events)
                == generate_can_registry_header(exports, events))


# ── Схема: нові поля приймаються, сміття — ні ────────────────────────

class TestSchema:
    def test_can_export_accepted(self):
        m = _module(state={"test_mod.v": {"type": "float", "access": "read",
                                          "can_export": True, "can_id": 42}})
        assert schema_errors(m, "module", "test_mod") == []

    def test_can_events_accepted(self):
        m = _module(can_events={"out": {"cmd": {"payload": "u8"}},
                                "in": {"alarm.overtemp": {}}})
        assert schema_errors(m, "module", "test_mod") == []

    def test_can_events_bad_payload_rejected(self):
        m = _module(can_events={"out": {"cmd": {"payload": "u64"}}})
        assert schema_errors(m, "module", "test_mod")

    def test_can_events_pathological_names_rejected(self):
        """'a..b' і 'alarm.' не може згенерувати жоден вузол — схема відкидає."""
        for bad in ("a..b", "alarm."):
            m = _module(can_events={"in": {bad: {}}})
            assert schema_errors(m, "module", "test_mod"), bad

    def test_requires_can_export_accepted(self):
        m = _module(requires=[{"role": "air_temp", "type": "sensor",
                               "capability": "temperature", "can_export": True}])
        assert schema_errors(m, "module", "test_mod") == []
