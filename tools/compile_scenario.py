#!/usr/bin/env python3
"""
compile_scenario.py — JSON manifest "scenario" section → `.modr` binary.

Build-time tool, parallel до tools/generate_ui.py. Scans modules/<name>/manifest.json,
filters those з `module_type: "recipe"` AND `scenario` key, validates against
tools/scenario_schema.json, resolves action/condition names через
tools/known_actions.json, emits compiled binary до data/scenarios/<name>.modr.

Engine code reference: components/modesp_sequence/include/modesp/sequence/modr_format.h
Spec reference: docs/sequence_engine/02_binary_format.md і 09_manifest_integration.md
ADR: docs/sequence_engine/adr/0001-binary-format-not-constexpr.md

USAGE:
    python tools/compile_scenario.py --modules-dir modules --output-dir data/scenarios
    python tools/compile_scenario.py --recipe modules/recipe_X/manifest.json --output recipe_X.modr

Error format: <file>:<line>:<col>: error[<code>]: <message>
Code prefixes:
  E01XX — schema validation
  E02XX — semantics (references, types, hash collisions)
  E03XX — binary emission
  E04XX — cross-validation з manifest.state section
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
import zlib
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

try:
    import jsonschema  # type: ignore[import-untyped]
except ImportError as _e:
    sys.stderr.write(
        "compile_scenario.py: jsonschema package required для schema validation.\n"
        "Install via: pip install -r tools/requirements.txt\n"
        f"Original error: {_e}\n"
    )
    sys.exit(2)


# ── Constants matching modr_format.h ──

MODR_MAGIC = 0x52444F4D
MODR_FORMAT_VERSION = 1
MODR_MAX_SIZE = 16 * 1024
MODR_NO_OFFSET = 0xFFFF
MODR_TARGET_COMPLETE = 0xFFFF
MODR_TARGET_ABORT = 0xFFFE

MODR_COMPLETION = {
    "all_tracks_complete": 0,
    "any_track_complete": 1,
    "main_track_complete": 2,
}

MODR_TRACK_FLAG_MAIN = 1 << 0
MODR_TRACK_FLAG_LOOP = 1 << 1

MODR_TRANS_KIND_TIME = 0
MODR_TRANS_KIND_COND = 1
MODR_TRANS_KIND_TIME_OR_COND = 2
MODR_TRANS_KIND_TIME_AND_COND = 3
MODR_TRANS_KIND_UNCONDITIONAL = 4

MODR_GLOBAL_SCOPE = {"abort_scenario": 0, "abort_only_main_track": 1}

MODR_PARAM_TYPE = {"i32": 0, "f32": 1, "bool": 2, "str": 3}

MODR_RESOURCE_SCOPE = {"scenario": 0, "phase": 1}

# Struct sizes
SIZE_HEADER = 56
SIZE_TRACK = 16
SIZE_PHASE = 20
SIZE_TRANSITION = 12
SIZE_GLOBAL_TRANSITION = 8
SIZE_ACTION = 8
SIZE_PARAM_ENTRY = 8
SIZE_RESOURCE_DECL = 4
SIZE_PHASE_RESOURCE_CLAIM = 4


# ── Error reporting ──


class CompileError(Exception):
    """Compile-time error з structured location info."""
    def __init__(self, code: str, message: str, file: str = "?", line: int = 0, col: int = 0):
        self.code = code
        self.message = message
        self.file = file
        self.line = line
        self.col = col
        super().__init__(self.format())

    def format(self) -> str:
        return f"{self.file}:{self.line}:{self.col}: error[{self.code}]: {self.message}"


# ── djb2 hash16 (matches modr_format.h::djb2_hash16) ──


def djb2_hash16(s: str) -> int:
    h = 5381
    for ch in s.encode("utf-8"):
        h = ((h << 5) + h + ch) & 0xFFFFFFFF
    return h & 0xFFFF


# ── Known actions/conditions registry loader ──


@dataclass
class KnownActionRegistry:
    actions: dict[str, dict[str, Any]]
    conditions: dict[str, dict[str, Any]]

    @classmethod
    def load(cls, path: Path) -> KnownActionRegistry:
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except Exception as e:
            raise CompileError("E0301", f"failed to read {path}: {e}", str(path))

        # Verify hash entries match djb2 computation (catches drift у known_actions.json).
        for name, info in data.get("actions", {}).items():
            expected = djb2_hash16(name)
            if info.get("hash") != expected:
                raise CompileError(
                    "E0202",
                    f"action {name!r} hash mismatch: stored {info.get('hash')}, "
                    f"expected djb2_hash16 = {expected}",
                    str(path),
                )

        for name, info in data.get("conditions", {}).items():
            expected = djb2_hash16(name)
            if info.get("hash") != expected:
                raise CompileError(
                    "E0202",
                    f"condition {name!r} hash mismatch: stored {info.get('hash')}, "
                    f"expected djb2_hash16 = {expected}",
                    str(path),
                )

        # Cross-collision check: action hash може conflict з condition hash?
        # Vsi 16-bit hashes should be unique у entire built-in space.
        seen: dict[int, str] = {}
        for name in list(data.get("actions", {}).keys()) + list(data.get("conditions", {}).keys()):
            h = djb2_hash16(name)
            if h in seen and seen[h] != name:
                raise CompileError(
                    "E0203",
                    f"hash collision у known_actions.json: {seen[h]!r} і {name!r} "
                    f"both hash to 0x{h:04x}. Rename one.",
                    str(path),
                )
            seen[h] = name

        return cls(
            actions=data.get("actions", {}),
            conditions=data.get("conditions", {}),
        )

    def is_known_action(self, name: str) -> bool:
        return name in self.actions

    def is_known_condition(self, name: str) -> bool:
        return name in self.conditions


# ── String pool builder ──


@dataclass
class StringPool:
    """Length-prefixed strings, 1-byte aligned. Returns offsets within pool data."""

    _data: bytearray = field(default_factory=bytearray)
    _offsets: dict[str, int] = field(default_factory=dict)

    def intern(self, s: str) -> int:
        """Add s to pool if absent; return offset."""
        if s in self._offsets:
            return self._offsets[s]
        encoded = s.encode("utf-8")
        if len(encoded) > 255:
            raise CompileError("E0301", f"string too long для u8 length prefix: {s[:32]!r}...")
        offset = len(self._data)
        self._offsets[s] = offset
        self._data.append(len(encoded))
        self._data.extend(encoded)
        return offset

    def offset_of(self, s: str) -> int:
        """Public accessor for previously-interned string's offset.
        Raises KeyError якщо not interned (caller's bug)."""
        return self._offsets[s]

    def to_bytes(self) -> bytes:
        return bytes(self._data)


# ── Schema validator ──


def validate_scenario_schema(scenario: dict[str, Any], schema: dict[str, Any], file: str) -> None:
    """Run JSON Schema validation. Re-raises як CompileError E01XX.
    jsonschema is hard requirement — module-level import fails fast if missing."""
    validator_cls = jsonschema.Draft7Validator
    validator = validator_cls(schema)
    errors = sorted(validator.iter_errors(scenario), key=lambda e: e.path)
    if errors:
        e = errors[0]
        path = "/".join(str(p) for p in e.absolute_path) or "<root>"
        raise CompileError(
            "E0101",
            f"schema validation: {e.message} (at scenario.{path})",
            file,
        )


# ── Pool builder (conditions, actions, params) ──

# Maximum recursion depth для composite condition expressions (all_of/any_of/not).
# Conservative cap protects compiler from CVE-class stack overflow attacks
# (CVE-2024-21907 Newtonsoft.Json, CVE-2025-52999 Jackson). Build-time-only
# context limits blast radius (no runtime exposure), але cheap mitigation
# defends against malformed manifests AND defensive against malicious recipes
# у future OTA distribution paths (Stage 1.5).
# Real-world recipes rarely nest beyond 3-4 levels; 16 is generous.
_MAX_CONDITION_DEPTH = 16


@dataclass
class _ActionRecord:
    """Internal representation of a modr_action entry (8 bytes у binary)."""
    hash: int          # uint16 djb2_hash16
    param_idx: int     # uint16 first param index, MODR_NO_OFFSET if no params
    param_n: int       # uint8 param count
    flags: int         # uint8 MODR_ACTION_FLAG_*


@dataclass
class _ParamRecord:
    """Internal representation of a modr_param_entry (8 bytes у binary)."""
    key_hash: int      # uint16 — для composite conditions can be 0 (positional)
    type: int          # uint8 MODR_PARAM_TYPE_*
    flags: int         # uint8
    value: int         # uint32 — i32 reinterpret, f32 reinterpret, bool, або str_idx


class PoolBuilder:
    """Accumulates action_pool, cond_pool, і param_pool entries.

    Conditions і actions share modr_action struct shape — separate pools maintained
    for engine-side find_action vs find_condition lookup, але same wire format.

    Composite conditions (all_of/any_of/not) use param entries з type=i32 і
    value=cond_pool_idx of the nested condition. param_n = number of children.
    Recursive — children added before parent so indices known when parent emits.
    """

    def __init__(self) -> None:
        self.actions: list[_ActionRecord] = []
        self.conditions: list[_ActionRecord] = []
        self.params: list[_ParamRecord] = []

    def add_action(self, name: str, params: dict[str, Any], string_pool: StringPool, file: str) -> int:
        """Adds entry to action_pool. Returns its index. params_dict is named (key_hash = djb2(param_name))."""
        # Special-case: set_state's "type" param is enum {"i32","f32","bool"} but Python
        # represents it як string. Pre-translate to MODR_PARAM_TYPE int code so engine
        # doesn't string-parse at runtime. Industry standard is integer enum encoding
        # (Protobuf, SBE) — string encoding wastes runtime cycles + flash.
        # Generic enum support через action descriptor metadata — Stage 1.5.
        if name == "set_state" and isinstance(params.get("type"), str):
            type_str = params["type"]
            if type_str in MODR_PARAM_TYPE:
                params = {**params, "type": MODR_PARAM_TYPE[type_str]}
            else:
                raise CompileError(
                    "E0225",
                    f"set_state 'type' param must be one of {list(MODR_PARAM_TYPE.keys())}, got {type_str!r}",
                    file,
                )
        param_idx, param_n = self._intern_named_params(params, string_pool, file)
        rec = _ActionRecord(
            hash=djb2_hash16(name),
            param_idx=param_idx,
            param_n=param_n,
            flags=0,
        )
        self.actions.append(rec)
        return len(self.actions) - 1

    def add_condition(self, expr: dict[str, Any], string_pool: StringPool, file: str,
                      depth: int = 0) -> int:
        """Recursively encodes a condition expression. Returns cond_pool index.

        Depth-bounded to _MAX_CONDITION_DEPTH (= 16) — guards проти stack overflow
        from deeply-nested composites (CVE-class у similar JSON libs).

        Handles all built-in condition forms per scenario_schema.json:
        - {"time_elapsed_ms": int}
        - {"state_key_eq|ne|lt|gt|le|ge": {"key": str, "value": <typed>}}
        - {"state_key_in_range": {"key": str, "min": num, "max": num}}
        - {"state_key_changed": {"key": str}}
        - {"time_of_day_eq": {"hh": int, "mm": int}}
        - {"all_of"|"any_of": [<cond>, ...]}
        - {"not": <cond>}
        """
        if depth > _MAX_CONDITION_DEPTH:
            raise CompileError(
                "E0218",
                f"condition expression exceeds maximum nesting depth of {_MAX_CONDITION_DEPTH}. "
                f"Refactor recipe to flatten conditions (most real recipes nest <4 levels).",
                file,
            )
        if not isinstance(expr, dict) or len(expr) != 1:
            raise CompileError(
                "E0211",
                f"condition expression must be single-key object, got: {expr!r}",
                file,
            )
        op = next(iter(expr))
        body = expr[op]

        if op == "time_elapsed_ms":
            if not isinstance(body, int) or body < 0:
                raise CompileError("E0212", f"time_elapsed_ms requires non-negative int, got {body!r}", file)
            params: list[_ParamRecord] = [
                _ParamRecord(key_hash=djb2_hash16("ms"), type=MODR_PARAM_TYPE["i32"], flags=0,
                             value=_pack_i32(body))
            ]
            return self._record_condition("time_elapsed_ms", params)

        if op in {"state_key_eq", "state_key_ne", "state_key_lt", "state_key_gt", "state_key_le", "state_key_ge"}:
            return self._record_state_key_cmp(op, body, string_pool, file)

        if op == "state_key_in_range":
            return self._record_state_key_in_range(body, string_pool, file)

        if op == "state_key_changed":
            if not isinstance(body, dict) or "key" not in body:
                raise CompileError("E0213", f"state_key_changed requires {{key: str}}, got {body!r}", file)
            params = [_ParamRecord(
                key_hash=djb2_hash16("key"), type=MODR_PARAM_TYPE["str"], flags=0,
                value=string_pool.intern(body["key"])
            )]
            return self._record_condition("state_key_changed", params)

        if op == "time_of_day_eq":
            if not isinstance(body, dict) or "hh" not in body or "mm" not in body:
                raise CompileError("E0214", f"time_of_day_eq requires {{hh, mm}}, got {body!r}", file)
            params = [
                _ParamRecord(key_hash=djb2_hash16("hh"), type=MODR_PARAM_TYPE["i32"], flags=0, value=_pack_i32(body["hh"])),
                _ParamRecord(key_hash=djb2_hash16("mm"), type=MODR_PARAM_TYPE["i32"], flags=0, value=_pack_i32(body["mm"])),
            ]
            return self._record_condition("time_of_day_eq", params)

        if op in {"all_of", "any_of"}:
            if not isinstance(body, list) or len(body) == 0:
                raise CompileError("E0215", f"{op} requires non-empty array of conditions, got {body!r}", file)
            # Add children FIRST so their indices are known when composite is recorded.
            child_indices = [self.add_condition(child, string_pool, file, depth + 1) for child in body]
            params = [
                _ParamRecord(key_hash=0, type=MODR_PARAM_TYPE["i32"], flags=0, value=_pack_i32(idx))
                for idx in child_indices
            ]
            return self._record_condition(op, params)

        if op == "not":
            child_idx = self.add_condition(body, string_pool, file, depth + 1)
            params = [_ParamRecord(key_hash=0, type=MODR_PARAM_TYPE["i32"], flags=0, value=_pack_i32(child_idx))]
            return self._record_condition("not", params)

        raise CompileError("E0210", f"unknown condition operator {op!r}", file)

    def _record_state_key_cmp(self, op: str, body: dict[str, Any], string_pool: StringPool, file: str) -> int:
        if not isinstance(body, dict) or "key" not in body or "value" not in body:
            raise CompileError("E0213", f"{op} requires {{key, value}}, got {body!r}", file)
        key_param = _ParamRecord(
            key_hash=djb2_hash16("key"), type=MODR_PARAM_TYPE["str"], flags=0,
            value=string_pool.intern(body["key"])
        )
        value_param = self._typed_value_param(body["value"], "value", file, string_pool)
        return self._record_condition(op, [key_param, value_param])

    def _record_state_key_in_range(self, body: dict[str, Any], string_pool: StringPool, file: str) -> int:
        if not isinstance(body, dict) or not all(k in body for k in ("key", "min", "max")):
            raise CompileError("E0213", f"state_key_in_range requires {{key, min, max}}, got {body!r}", file)
        params = [
            _ParamRecord(key_hash=djb2_hash16("key"), type=MODR_PARAM_TYPE["str"], flags=0,
                         value=string_pool.intern(body["key"])),
            self._typed_value_param(body["min"], "min", file, string_pool),
            self._typed_value_param(body["max"], "max", file, string_pool),
        ]
        return self._record_condition("state_key_in_range", params)

    def _typed_value_param(self, value: Any, param_name: str, file: str,
                           string_pool: StringPool | None = None) -> _ParamRecord:
        """Encode a scalar value into a param entry, picking type based on Python type.
        For string values, string_pool is required for interning.

        ORDER INVARIANT: bool MUST be checked before int — Python's `bool` subclasses
        `int`, so `isinstance(True, int)` returns True. Reordering breaks correct
        encoding silently (all bools become i32). Standard Python pattern (see
        labex.io/python-bool, realpython.com isinstance docs)."""
        kh = djb2_hash16(param_name)
        if isinstance(value, bool):  # MUST precede int check
            return _ParamRecord(key_hash=kh, type=MODR_PARAM_TYPE["bool"], flags=0, value=1 if value else 0)
        if isinstance(value, int):
            return _ParamRecord(key_hash=kh, type=MODR_PARAM_TYPE["i32"], flags=0, value=_pack_i32(value))
        if isinstance(value, float):
            return _ParamRecord(key_hash=kh, type=MODR_PARAM_TYPE["f32"], flags=0, value=_pack_f32(value))
        if isinstance(value, str):
            if string_pool is None:
                raise CompileError("E0216", f"param {param_name!r}: string value requires string_pool context", file)
            return _ParamRecord(
                key_hash=kh, type=MODR_PARAM_TYPE["str"], flags=0,
                value=string_pool.intern(value)  # u16 offset stored у low bits of value
            )
        raise CompileError("E0217", f"unsupported value type для param {param_name!r}: {type(value).__name__}", file)

    def _record_condition(self, name: str, params: list[_ParamRecord]) -> int:
        param_idx = MODR_NO_OFFSET if not params else len(self.params)
        self.params.extend(params)
        rec = _ActionRecord(
            hash=djb2_hash16(name),
            param_idx=param_idx,
            param_n=len(params),
            flags=0,
        )
        self.conditions.append(rec)
        return len(self.conditions) - 1

    def _intern_named_params(self, params: dict[str, Any], string_pool: StringPool, file: str) -> tuple[int, int]:
        if not params:
            return MODR_NO_OFFSET, 0
        idx = len(self.params)
        for name, val in params.items():
            self.params.append(self._typed_value_param(val, name, file, string_pool))
        return idx, len(params)


def _pack_i32(v: int) -> int:
    """Reinterpret signed int as uint32 для wire format."""
    return v & 0xFFFFFFFF


def _pack_f32(v: float) -> int:
    """Reinterpret float as uint32 (IEEE 754 little-endian)."""
    return struct.unpack("<I", struct.pack("<f", v))[0]


# ── Compiler ──


@dataclass
class CompiledScenario:
    blob: bytes
    module_name: str

    @property
    def size(self) -> int:
        return len(self.blob)


class ScenarioCompiler:
    def __init__(self, registry: KnownActionRegistry, schema: dict[str, Any], strict: bool = False):
        self.registry = registry
        self.schema = schema
        # Q6: --strict CLI flag elevates warnings (W0220 unknown action,
        # W0230 unknown ContinuousBehavior) to errors (E0226, E0231).
        # Industry standard pattern (TypeScript --strict, GCC -Werror, ESLint --max-warnings).
        # Default off для author convenience; CI uses --strict для drift protection.
        self.strict = strict

    def compile(self, manifest_path: Path) -> CompiledScenario:
        """Read manifest, extract scenario section, emit .modr bytes."""
        try:
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        except json.JSONDecodeError as e:
            raise CompileError("E0102", f"manifest JSON parse error: {e.msg}", str(manifest_path), e.lineno, e.colno)

        module_name = manifest.get("module")
        if not module_name:
            raise CompileError("E0103", "manifest missing 'module' field", str(manifest_path))

        if manifest.get("module_type") != "recipe":
            raise CompileError(
                "E0104",
                f"compile_scenario expects module_type='recipe', got {manifest.get('module_type')!r}",
                str(manifest_path),
            )

        scenario = manifest.get("scenario")
        if scenario is None:
            raise CompileError("E0105", "manifest missing 'scenario' section", str(manifest_path))

        # Schema validation
        validate_scenario_schema(scenario, self.schema, str(manifest_path))

        # Semantic uniqueness — JSON Schema doesn't enforce це bo uniqueItems працює
        # тільки for primitive arrays, not arrays of objects. Validate manually:
        self._validate_unique_names(scenario, str(manifest_path))

        # Cross-validate state keys vs manifest.state (E04XX).
        # Per plan Q3 і Q9: engine writes mirror keys derived from scenario structure.
        # Recipe author MUST declare each у manifest.state так що generate_ui.py emits
        # state_meta.h entry і SharedState capacity sized correctly.
        self._cross_validate_state_keys(scenario, manifest, module_name, str(manifest_path))

        # Build binary
        blob = self._emit(scenario, module_name, str(manifest_path))
        if len(blob) > MODR_MAX_SIZE:
            raise CompileError(
                "E0302",
                f"compiled scenario {len(blob)} bytes exceeds MODR_MAX_SIZE ({MODR_MAX_SIZE})",
                str(manifest_path),
            )
        return CompiledScenario(blob=blob, module_name=module_name)

    def _emit(self, scenario: dict[str, Any], module_name: str, file: str) -> bytes:
        """Layout the binary і emit final bytes including CRC32."""
        pool = StringPool()
        name_str_off = pool.intern(module_name)

        tracks_data = scenario["tracks"]
        track_count = len(tracks_data)

        # ── Pass 1: walk scenario і build pools ──
        # Conditions, actions, parameters all encoded eagerly into PoolBuilder.

        builder = PoolBuilder()

        # Pre-intern all strings (track names, phase names) — emitted у string pool.
        for tdata in tracks_data:
            pool.intern(tdata["name"])
            for pdata in tdata["phases"]:
                pool.intern(pdata["name"])

        # Per-phase entry/exit action ranges — parallel arrays, populated у Pass 1
        # before layout. Each phase has 4 numbers: (entry_off, entry_n, exit_off, exit_n).
        # entry_off/exit_off are ACTION POOL INDICES (not byte offsets) for first action;
        # MODR_NO_OFFSET when n=0.
        phase_action_ranges: list[tuple[int, int, int, int]] = []

        for tdata in tracks_data:
            for pdata in tdata["phases"]:
                # Q12: validate phase.continuous references (warn or strict-error)
                self._validate_continuous_behaviors(pdata, file)

                # Entry actions
                entry_actions = pdata.get("entry", [])
                if entry_actions:
                    entry_first_idx = len(builder.actions)
                    for action_inv in entry_actions:
                        self._validate_action_invocation(action_inv, file)
                        builder.add_action(action_inv["action"], action_inv.get("params", {}), pool, file)
                    entry_n = len(entry_actions)
                else:
                    entry_first_idx = MODR_NO_OFFSET
                    entry_n = 0

                # Exit actions
                exit_actions = pdata.get("exit", [])
                if exit_actions:
                    exit_first_idx = len(builder.actions)
                    for action_inv in exit_actions:
                        self._validate_action_invocation(action_inv, file)
                        builder.add_action(action_inv["action"], action_inv.get("params", {}), pool, file)
                    exit_n = len(exit_actions)
                else:
                    exit_first_idx = MODR_NO_OFFSET
                    exit_n = 0

                phase_action_ranges.append((entry_first_idx, entry_n, exit_first_idx, exit_n))

        # Per-transition cond_pool indices (parallel array — len matches total transitions)
        # Built у emission order (track 0 phase 0 trans 0, track 0 phase 0 trans 1, ...).
        transition_cond_indices: list[int] = []
        for tdata in tracks_data:
            for pdata in tdata["phases"]:
                for trdata in pdata["transitions"]:
                    when = trdata.get("when")
                    if when is None:
                        transition_cond_indices.append(MODR_NO_OFFSET)
                    else:
                        cond_idx = builder.add_condition(when, pool, file)
                        transition_cond_indices.append(cond_idx)

        # Resources — both scenario-scope і phase-scope supported (Step 2b.3).
        resource_count = len(scenario.get("resources", []))

        # Phase-scope resource claims. Each phase has its own claims array; we
        # concatenate them into one pool і compute per-phase byte offsets.
        # Parallel array to phases: each entry is the list of claim dicts.
        phase_resource_claims_per_phase: list[list[dict[str, Any]]] = []
        for tdata in tracks_data:
            for pdata in tdata["phases"]:
                phase_resource_claims_per_phase.append(pdata.get("phase_resources", []))

        flags_has_resources = (resource_count > 0) or any(phase_resource_claims_per_phase)

        # Global transitions — emit to dedicated pool. Each global transition
        # has condition (added to cond_pool), scope, priority. Always targets
        # $abort (validated explicitly — schema's `to` field requires uniformity).
        global_trans_records: list[tuple[int, int, int]] = []  # (cond_idx, scope, priority)
        for gt in scenario.get("global_transitions", []):
            # `to` field optional post-D3 cleanup — schema enforces literal "$abort"
            # if present. Globals always abort (binary format has no target_phase
            # field; scope determines whether scenario or only main track aborts).
            if "to" in gt and gt["to"] != "$abort":
                raise CompileError(
                    "E0223",
                    f"global_transitions.to must be '$abort' or omitted; got {gt.get('to')!r}",
                    file,
                )
            when = gt.get("when")
            if when is None:
                raise CompileError("E0224", "global_transitions require 'when' clause", file)
            cond_idx = builder.add_condition(when, pool, file)
            scope_str = gt.get("scope", "abort_scenario")
            scope = MODR_GLOBAL_SCOPE[scope_str]
            priority = gt.get("priority", 100)
            global_trans_records.append((cond_idx, scope, priority))

        # Sort by priority descending (engine evaluates highest first per Q5).
        global_trans_records.sort(key=lambda r: -r[2])
        global_trans_count = len(global_trans_records)
        flags_has_globals = global_trans_count > 0

        # ── Pass 2: compute offsets ──

        offset = SIZE_HEADER
        track_table_off = offset
        offset += SIZE_TRACK * track_count

        phase_offs_per_track: list[int] = []
        for tdata in tracks_data:
            phase_offs_per_track.append(offset)
            offset += SIZE_PHASE * len(tdata["phases"])

        transitions_off_per_phase: list[int] = []
        for tdata in tracks_data:
            for pdata in tdata["phases"]:
                transitions_off_per_phase.append(offset)
                offset += SIZE_TRANSITION * len(pdata["transitions"])

        # Action pool (empty у 2b.1; populated у 2b.2)
        action_pool_off = offset if builder.actions else 0
        offset += SIZE_ACTION * len(builder.actions)

        # Condition pool
        cond_pool_off = offset if builder.conditions else 0
        offset += SIZE_ACTION * len(builder.conditions)  # cond_pool entries — same struct as action

        # Param pool (shared by actions і conditions)
        param_pool_off = offset if builder.params else 0
        offset += SIZE_PARAM_ENTRY * len(builder.params)

        # Global transitions pool — 8 bytes per entry
        global_trans_off = offset if global_trans_count > 0 else 0
        offset += SIZE_GLOBAL_TRANSITION * global_trans_count

        # Scenario-scope resource declarations — 4 bytes each
        resource_off = offset if resource_count > 0 else 0
        offset += SIZE_RESOURCE_DECL * resource_count

        # Phase-scope resource claims pool — claim per phase, concatenated.
        # Each phase's first claim byte offset stored у phase.phase_resources_off field.
        phase_resources_byte_off: list[int] = []
        for prs in phase_resource_claims_per_phase:
            if prs:
                phase_resources_byte_off.append(offset)
                offset += SIZE_PHASE_RESOURCE_CLAIM * len(prs)
            else:
                phase_resources_byte_off.append(0)  # MODR_NO_OFFSET below; 0 used for clarity

        string_pool_off = offset
        pool_bytes = pool.to_bytes()
        offset += len(pool_bytes)

        total_size = offset + 4  # CRC32 trailer

        action_count = len(builder.actions)
        cond_count = len(builder.conditions)
        param_count = len(builder.params)

        # Compute scenario_id
        scenario_id = djb2_hash16(module_name)

        # Compute flags (per modr_format.h MODR_FLAG_*)
        flags = 0
        if flags_has_globals:
            flags |= 1 << 1  # MODR_FLAG_HAS_GLOBALS
        if flags_has_resources:
            flags |= 1 << 2  # MODR_FLAG_HAS_RESOURCES

        # ── Emit ──

        out = bytearray()

        # Header
        out.extend(struct.pack(
            "<IHHIHHBBBBBBHHHHHHHHHHHIII",
            MODR_MAGIC,
            MODR_FORMAT_VERSION,
            flags,
            total_size,
            scenario_id,
            name_str_off,
            track_count,
            0,  # cont_count
            resource_count,
            global_trans_count,
            MODR_COMPLETION[scenario["completion_rule"]],
            0,  # reserved_a
            track_table_off,
            param_pool_off,
            param_count,
            action_pool_off,
            action_count,
            cond_pool_off,
            cond_count,
            string_pool_off,
            global_trans_off,
            resource_off,
            0,  # reserved_b
            scenario["default_phase_timeout_ms"],
            scenario.get("scenario_timeout_max_ms", 0),
            0,  # reserved_c
        ))

        # Track table
        for ti, tdata in enumerate(tracks_data):
            tflags = 0
            for f in tdata.get("flags", []):
                if f == "main_track":
                    tflags |= MODR_TRACK_FLAG_MAIN
                elif f == "loop_on_complete":
                    tflags |= MODR_TRACK_FLAG_LOOP
            out.extend(struct.pack(
                "<HHHHBBHI",
                pool.offset_of(tdata["name"]),
                phase_offs_per_track[ti],
                0,  # initial_phase
                0,  # reserved_a
                len(tdata["phases"]),
                tflags,
                0,  # reserved_b
                0,  # reserved_c
            ))

        # Phase tables (per track, in track order)
        phase_idx_global = 0
        for tdata in tracks_data:
            for pdata in tdata["phases"]:
                trans_off = transitions_off_per_phase[phase_idx_global]
                entry_off, entry_n, exit_off, exit_n = phase_action_ranges[phase_idx_global]

                # Phase-scope resources — populate phase_resources_off і count
                pr_claims = phase_resource_claims_per_phase[phase_idx_global]
                pr_byte_off = phase_resources_byte_off[phase_idx_global] if pr_claims else MODR_NO_OFFSET
                pr_n = len(pr_claims)

                out.extend(struct.pack(
                    "<HHHHBBBBIHBB",
                    pool.offset_of(pdata["name"]),
                    entry_off,  # action_pool index of first entry action, MODR_NO_OFFSET if none
                    exit_off,
                    trans_off,
                    entry_n,
                    exit_n,
                    len(pdata["transitions"]),
                    0,  # cont_mask
                    pdata.get("timeout_ms", 0),
                    pr_byte_off,
                    pr_n,
                    0,  # reserved
                ))
                phase_idx_global += 1

        # Transition arrays — emit з kind based on (when present?, time_elapsed_ms only?)
        # transition_cond_indices is parallel to emission order (built у Pass 1).
        trans_idx = 0
        for tdata in tracks_data:
            for pdata in tdata["phases"]:
                for trdata in pdata["transitions"]:
                    target = self._resolve_target(trdata["to"], pdata, tdata, file)
                    cond_idx = transition_cond_indices[trans_idx]
                    trans_idx += 1
                    when = trdata.get("when")

                    # Determine encoding strategy:
                    # - No `when` → UNCONDITIONAL (cond_pool_idx irrelevant)
                    # - `when` is `{time_elapsed_ms: T}` ONLY → encode як TIME з threshold,
                    #   skip cond_pool reference (engine evaluates time directly)
                    # - Otherwise → COND з cond_pool_idx pointing to pre-built condition
                    # TIME_OR_COND і TIME_AND_COND — Step 2b.5 (compound time+cond — explicit syntax TBD)
                    if when is None:
                        kind = MODR_TRANS_KIND_UNCONDITIONAL
                        cond_emit_idx = MODR_NO_OFFSET
                        time_threshold = 0
                    elif (isinstance(when, dict) and len(when) == 1
                          and "time_elapsed_ms" in when and isinstance(when["time_elapsed_ms"], int)):
                        # Pure time threshold — no condition lookup needed at runtime
                        kind = MODR_TRANS_KIND_TIME
                        cond_emit_idx = MODR_NO_OFFSET
                        time_threshold = when["time_elapsed_ms"]
                        # Note: builder.add_condition was already called у Pass 1 для this when —
                        # creating an unused entry. Acceptable у v0 (small waste); Step 2b.5
                        # may dedupe by skipping pure-time conditions у Pass 1.
                    else:
                        kind = MODR_TRANS_KIND_COND
                        cond_emit_idx = cond_idx
                        time_threshold = 0

                    out.extend(struct.pack(
                        "<HHIBBH",
                        cond_emit_idx,
                        target,
                        time_threshold,
                        kind,
                        0,  # reserved_a
                        0,  # reserved_b
                    ))

        # Action pool
        for a in builder.actions:
            out.extend(struct.pack("<HHBBH", a.hash, a.param_idx, a.param_n, a.flags, 0))

        # Condition pool (same wire format as actions — modr_action shape)
        for c in builder.conditions:
            out.extend(struct.pack("<HHBBH", c.hash, c.param_idx, c.param_n, c.flags, 0))

        # Param pool
        for p in builder.params:
            out.extend(struct.pack("<HBBI", p.key_hash, p.type, p.flags, p.value))

        # Global transitions pool — sorted by priority descending у Pass 1
        for cond_idx, scope, priority in global_trans_records:
            out.extend(struct.pack(
                "<HHBBBB",
                cond_idx,           # cond_pool_idx
                0,                  # reserved_a
                MODR_TRANS_KIND_COND,  # kind — globals always COND-based
                priority,
                scope,
                0,                  # reserved_b
            ))

        # Scenario-scope resource declarations (always scope=SCENARIO; phase-scope
        # уlives у separate pool via phase.phase_resources). Schema enforces this
        # post-D1 cleanup — `scope` field removed from scenario.resources schema.
        for r in scenario.get("resources", []):
            out.extend(struct.pack(
                "<HBB",
                djb2_hash16(r["resource"]),
                1 if r.get("exclusive", True) else 0,
                MODR_RESOURCE_SCOPE["scenario"],
            ))

        # Phase-scope resource claims pool — concatenated per-phase claim arrays.
        # Engine reads per phase via phase.phase_resources_off + phase_resource_n.
        for prs in phase_resource_claims_per_phase:
            for claim in prs:
                out.extend(struct.pack(
                    "<HBB",
                    djb2_hash16(claim["resource"]),
                    1 if claim.get("exclusive", True) else 0,
                    0,  # reserved
                ))

        # String pool
        out.extend(pool_bytes)

        # CRC32 trailer
        crc = zlib.crc32(bytes(out)) & 0xFFFFFFFF
        out.extend(struct.pack("<I", crc))

        if len(out) != total_size:
            raise CompileError(
                "E0303",
                f"emitted {len(out)} bytes but header.total_size says {total_size}",
                file,
            )
        return bytes(out)

    def _derive_expected_mirror_keys(self, scenario: dict[str, Any], module_name: str) -> dict[str, str]:
        """Compute mirror state keys engine WILL write at runtime для this scenario.
        Returns {key: type} dict. Per plan Q3 namespace convention:
        - <module>.scenario_state (string)
        - <module>.scenario_elapsed_s (int)
        - <module>.last_error (int)
        - <module>.<track>_state (string), <track>_phase_name (string),
          <track>_phase_idx (int), <track>_elapsed_s (int)
        """
        expected: dict[str, str] = {
            f"{module_name}.scenario_state": "string",
            f"{module_name}.scenario_elapsed_s": "int",
            f"{module_name}.last_error": "int",
        }
        for track in scenario["tracks"]:
            tname = track["name"]
            expected[f"{module_name}.{tname}_state"] = "string"
            expected[f"{module_name}.{tname}_phase_name"] = "string"
            expected[f"{module_name}.{tname}_phase_idx"] = "int"
            expected[f"{module_name}.{tname}_elapsed_s"] = "int"
        return expected

    def _cross_validate_state_keys(self, scenario: dict[str, Any], manifest: dict[str, Any],
                                    module_name: str, file: str) -> None:
        """Per plan Q9: every state key engine WILL write MUST be declared у
        manifest.state з correct type. Mismatch → E0401/E0403."""
        expected = self._derive_expected_mirror_keys(scenario, module_name)
        declared_state = manifest.get("state", {})
        declared = set(declared_state.keys())

        # Check budget: SharedState key max 32 chars. Long names → E0402.
        for key in expected:
            if len(key) > 32:
                raise CompileError(
                    "E0402",
                    f"derived mirror key {key!r} exceeds 32-char SharedState budget. "
                    f"Reduce module name (current {len(module_name)} chars, max 12) "
                    f"and/or track name (max 8 chars).",
                    file,
                )

        missing = sorted(set(expected.keys()) - declared)
        if missing:
            sample = missing[:3]
            extra = "" if len(missing) <= 3 else f" (and {len(missing) - 3} more)"
            raise CompileError(
                "E0401",
                f"manifest.state missing mirror key declarations що engine writes runtime: "
                f"{sample}{extra}. Add these keys (з access:'read') to manifest.state.",
                file,
            )

        # Q5: type matching — engine writes specific types per derived keys.
        # If author declared wrong type, runtime SharedState.set() rejects silently
        # (set_failures counter increments, але no diagnostic to author).
        # Compile-time check catches drift between manifest declarations і engine emit.
        type_mismatches: list[str] = []
        for key, expected_type in expected.items():
            declared_decl = declared_state.get(key, {})
            declared_type = declared_decl.get("type")
            if declared_type != expected_type:
                type_mismatches.append(
                    f"  {key}: declared {declared_type!r}, engine writes {expected_type!r}"
                )
        if type_mismatches:
            raise CompileError(
                "E0403",
                f"manifest.state type mismatch — engine will write different types:\n"
                + "\n".join(type_mismatches[:5])
                + (f"\n  ... ({len(type_mismatches) - 5} more)" if len(type_mismatches) > 5 else ""),
                file,
            )

    # Built-in ContinuousBehaviors for MVP (per plan Q4 — registered у engine).
    # Domain modules can register additional via runtime ContinuousRegistry.
    # MVP ships 0 built-ins (foundation в ADR-0006 — engine domain-agnostic).
    _BUILTIN_CONTINUOUS: set[str] = set()

    def _validate_continuous_behaviors(self, phase: dict[str, Any], file: str) -> None:
        """Per Q12: phase.continuous references must resolve. Built-ins у MVP = 0.
        Domain modules register at runtime → warn (W0230) at compile, --strict
        elevates to E0231. Symmetric з W0220 unknown action handling."""
        cont_list = phase.get("continuous", [])
        for name in cont_list:
            if name in self._BUILTIN_CONTINUOUS:
                continue
            msg = (f"ContinuousBehavior {name!r} not у built-ins і compiler can't verify "
                   f"runtime registration. Domain module must register before scenario starts. "
                   f"Built-ins (currently empty у MVP per ADR-0006): {sorted(self._BUILTIN_CONTINUOUS)}")
            if self.strict:
                raise CompileError("E0231", msg, file)
            sys.stderr.write(f"{file}:0:0: warning[W0230]: {msg}\n")

    def _validate_action_invocation(self, action_inv: dict[str, Any], file: str) -> None:
        """Verify that referenced action name is у known_actions.json. Domain modules
        will register additional actions runtime; for compile-time, only built-ins
        validated. Cross-checks param count against descriptor."""
        name = action_inv.get("action")
        if not name:
            raise CompileError("E0220", "action invocation missing 'action' field", file)
        descriptor = self.registry.actions.get(name)
        if descriptor is None:
            # Domain modules register actions runtime — can't strictly require у compile.
            # Industry standard (TypeScript strict mode, GCC -Wall, ESLint) is warn-not-silent.
            # --strict flag elevates до E0226.
            msg = (f"action {name!r} not у known_actions.json. "
                   f"Will be looked up у runtime ActionRegistry — verify domain module "
                   f"registers it. Possible typo? Known: {sorted(self.registry.actions.keys())}")
            if self.strict:
                raise CompileError("E0226", msg, file)
            sys.stderr.write(f"{file}:0:0: warning[W0220]: {msg}\n")
            return
        params = action_inv.get("params", {})
        if not isinstance(params, dict):
            raise CompileError("E0221", f"action {name!r} params must be object, got {type(params).__name__}", file)
        n = len(params)
        pmin = descriptor.get("param_min", 0)
        pmax = descriptor.get("param_max", 999)
        if n < pmin or n > pmax:
            raise CompileError(
                "E0222",
                f"action {name!r} expects {pmin}..{pmax} params, got {n}: {list(params.keys())}",
                file,
            )

    def _validate_unique_names(self, scenario: dict[str, Any], file: str) -> None:
        """Track names must be unique within scenario; phase names within track.
        JSON Schema's uniqueItems doesn't apply to arrays of objects, so explicit check.

        Real bug protections:
        - Two tracks з тією ж name → mirror state keys collide
          (e.g., recipe_x.main_phase_name written by both)
        - Two phases з тією ж name within track → _resolve_target silently picks
          first match; subsequent phases unreachable
        """
        track_names: dict[str, int] = {}
        for ti, t in enumerate(scenario["tracks"]):
            name = t["name"]
            if name in track_names:
                raise CompileError(
                    "E0208",
                    f"duplicate track name {name!r} (also at index {track_names[name]}). "
                    f"Track names must be unique у scenario.",
                    file,
                )
            track_names[name] = ti

            phase_names: dict[str, int] = {}
            for pi, p in enumerate(t["phases"]):
                pname = p["name"]
                if pname in phase_names:
                    raise CompileError(
                        "E0208",
                        f"duplicate phase name {pname!r} у track {name!r} "
                        f"(also at index {phase_names[pname]}). "
                        f"Phase names must be unique within track.",
                        file,
                    )
                phase_names[pname] = pi

    def _resolve_target(self, target: str, phase: dict[str, Any], track: dict[str, Any], file: str) -> int:
        if target == "$complete":
            return MODR_TARGET_COMPLETE
        if target == "$abort":
            return MODR_TARGET_ABORT
        # Phase name within same track
        for idx, p in enumerate(track["phases"]):
            if p["name"] == target:
                return idx
        raise CompileError(
            "E0207",
            f"transition target {target!r} from phase {phase['name']!r} not found "
            f"у track {track['name']!r}. Valid targets: "
            f"{[p['name'] for p in track['phases']]} + ['$complete', '$abort']",
            file,
        )


# ── CLI ──


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0] if __doc__ else "")
    parser.add_argument("--modules-dir", type=Path, default=Path("modules"))
    parser.add_argument("--output-dir", type=Path, default=Path("data/scenarios"))
    parser.add_argument("--recipe", type=Path, help="compile single recipe manifest path (overrides --modules-dir)")
    parser.add_argument("--output", type=Path, help="explicit output path для --recipe mode")
    parser.add_argument("--known-actions", type=Path, default=Path("tools/known_actions.json"))
    parser.add_argument("--schema", type=Path, default=Path("tools/scenario_schema.json"))
    parser.add_argument("--quiet", action="store_true")
    parser.add_argument("--strict", action="store_true",
                        help="Elevate warnings (W0220 unknown action, W0230 unknown continuous) "
                             "to errors. Industry-standard pattern (TypeScript --strict, GCC -Werror). "
                             "Use у CI to detect typos і drift.")
    args = parser.parse_args(argv)

    try:
        registry = KnownActionRegistry.load(args.known_actions)
    except CompileError as e:
        print(e.format(), file=sys.stderr)
        return 1

    try:
        schema = json.loads(args.schema.read_text(encoding="utf-8"))
    except Exception as e:
        print(f"failed to read schema {args.schema}: {e}", file=sys.stderr)
        return 1

    compiler = ScenarioCompiler(registry, schema, strict=args.strict)

    if args.recipe:
        manifests = [args.recipe]
    else:
        manifests = sorted(args.modules_dir.glob("*/manifest.json"))

    args.output_dir.mkdir(parents=True, exist_ok=True)
    compiled_count = 0
    error_count = 0

    for mpath in manifests:
        try:
            manifest = json.loads(mpath.read_text(encoding="utf-8"))
        except Exception:
            continue  # skip; generate_ui.py handles malformed manifests

        if manifest.get("module_type") != "recipe" or "scenario" not in manifest:
            continue

        try:
            result = compiler.compile(mpath)
        except CompileError as e:
            print(e.format(), file=sys.stderr)
            error_count += 1
            continue

        out_path = args.output if args.recipe and args.output else (
            args.output_dir / f"{result.module_name}.modr"
        )
        out_path.write_bytes(result.blob)
        compiled_count += 1
        if not args.quiet:
            print(f"  + {out_path} ({result.size} bytes)")

    if not args.quiet:
        print(f"\n{compiled_count} scenarios compiled, {error_count} errors")

    return 1 if error_count > 0 else 0


if __name__ == "__main__":
    sys.exit(main())
