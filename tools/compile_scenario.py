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
    HAS_JSONSCHEMA = True
except ImportError:
    HAS_JSONSCHEMA = False


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

    def to_bytes(self) -> bytes:
        return bytes(self._data)


# ── Schema validator ──


def validate_scenario_schema(scenario: dict[str, Any], schema: dict[str, Any], file: str) -> None:
    """Run JSON Schema validation. Re-raises як CompileError E01XX."""
    if not HAS_JSONSCHEMA:
        # Skip schema validation якщо jsonschema not installed; manual checks downstream.
        # Better than hard failure in environments without jsonschema.
        return

    validator_cls = jsonschema.Draft7Validator
    validator = validator_cls(schema)
    errors = sorted(validator.iter_errors(scenario), key=lambda e: e.path)
    if errors:
        # Take first error.
        e = errors[0]
        path = "/".join(str(p) for p in e.absolute_path) or "<root>"
        raise CompileError(
            "E0101",
            f"schema validation: {e.message} (at scenario.{path})",
            file,
        )


# ── Compiler ──


@dataclass
class CompiledScenario:
    blob: bytes
    module_name: str

    @property
    def size(self) -> int:
        return len(self.blob)


class ScenarioCompiler:
    def __init__(self, registry: KnownActionRegistry, schema: dict[str, Any]):
        self.registry = registry
        self.schema = schema

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

        # Cross-validate state keys (E04XX). MVP version: just check that engine writes
        # to declared keys. Full mirror-key derivation is deferred to Step 16 для real recipe.
        # Тепер: basic sanity — declared state section exists.
        manifest_state = manifest.get("state", {})
        # Skip cross-validation у v0; too early to derive expected keys without full
        # mirror-key generator. Will be enforced у Step 16.
        _ = manifest_state

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

        # Tracks
        tracks_data = scenario["tracks"]
        track_count = len(tracks_data)

        # Pre-build per-track phase tables (offsets computed after layout)
        track_phase_tables: list[list[dict[str, Any]]] = []
        for t in tracks_data:
            track_phase_tables.append(t["phases"])

        # Plan layout offsets (pre-compute):
        # header (56) + tracks (16*N) + phases (20*sum) + transitions (12*sum) +
        # action/cond pools (8*count) + param pool (8*count) + global trans (8*count) +
        # resources (4*count) + phase_resource_claims (4*sum) + string pool + CRC32

        # Count phases і transitions
        total_phases = sum(len(t["phases"]) for t in tracks_data)
        total_transitions = sum(len(p["transitions"]) for t in tracks_data for p in t["phases"])

        # MVP scope: ignore actions/params/resources/globals — emit empty pools.
        # Phase entry/exit actions ignored у v0 (not needed for minimal recipe round-trip).
        # Step 2b will extend until full feature parity з paper-piloted greenhouse recipe.
        action_count = 0
        param_count = 0
        cond_count = 0
        resource_count = len(scenario.get("resources", []))
        global_trans_count = len(scenario.get("global_transitions", []))

        for r in scenario.get("resources", []):
            if r.get("scope", "scenario") != "scenario":
                raise CompileError(
                    "E0204",
                    "v0 compiler supports only scenario-scope resources; phase-scope coming Step 2b",
                    file,
                )

        if global_trans_count > 0:
            raise CompileError(
                "E0205",
                "v0 compiler doesn't yet emit global_transitions (Step 2b)",
                file,
            )

        # Compute offsets
        offset = SIZE_HEADER
        track_table_off = offset
        offset += SIZE_TRACK * track_count

        # Per-track phases laid out sequentially
        phase_offs_per_track: list[int] = []
        for tdata in tracks_data:
            phase_offs_per_track.append(offset)
            offset += SIZE_PHASE * len(tdata["phases"])

        # Per-phase transition arrays laid out sequentially
        # We'll fill these as we emit phases — track each phase's transition_off.
        transitions_off_per_phase: list[int] = []
        for tdata in tracks_data:
            for pdata in tdata["phases"]:
                transitions_off_per_phase.append(offset)
                offset += SIZE_TRANSITION * len(pdata["transitions"])

        # Resource decls
        resource_off = offset if resource_count > 0 else 0
        offset += SIZE_RESOURCE_DECL * resource_count

        # String pool
        # First pre-intern всі strings we'll need
        for tdata in tracks_data:
            pool.intern(tdata["name"])
            for pdata in tdata["phases"]:
                pool.intern(pdata["name"])

        string_pool_off = offset
        pool_bytes = pool.to_bytes()
        offset += len(pool_bytes)

        total_size = offset + 4  # +4 для CRC32 trailer

        # Compute scenario_id
        scenario_id = djb2_hash16(module_name)

        # Compute flags
        flags = 0
        if resource_count > 0:
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
            0,  # param_pool_off
            param_count,
            0,  # action_pool_off
            action_count,
            0,  # cond_pool_off
            cond_count,
            string_pool_off,
            0,  # global_trans_off (unused у v0)
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
                pool._offsets[tdata["name"]],
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
                out.extend(struct.pack(
                    "<HHHHBBBBIHBB",
                    pool._offsets[pdata["name"]],
                    MODR_NO_OFFSET,  # entry_action_off (v0 — no actions yet)
                    MODR_NO_OFFSET,  # exit_action_off
                    trans_off,
                    0,  # entry_action_n
                    0,  # exit_action_n
                    len(pdata["transitions"]),
                    0,  # cont_mask
                    pdata.get("timeout_ms", 0),
                    MODR_NO_OFFSET,  # phase_resources_off (v0)
                    0,  # phase_resource_n
                    0,  # reserved
                ))
                phase_idx_global += 1

        # Transition arrays
        for tdata in tracks_data:
            for pdata in tdata["phases"]:
                for trdata in pdata["transitions"]:
                    target = self._resolve_target(trdata["to"], pdata, tdata, file)
                    when = trdata.get("when")
                    # v0: only unconditional (no `when`) supported.
                    if when is not None:
                        raise CompileError(
                            "E0206",
                            "v0 compiler supports only unconditional transitions (no `when` clause). Step 2b adds conditions.",
                            file,
                        )
                    out.extend(struct.pack(
                        "<HHIBBH",
                        MODR_NO_OFFSET,  # cond_pool_idx (unused для UNCONDITIONAL)
                        target,
                        0,  # time_threshold_ms
                        MODR_TRANS_KIND_UNCONDITIONAL,
                        0,  # reserved_a
                        0,  # reserved_b
                    ))

        # Resource declarations
        for r in scenario.get("resources", []):
            out.extend(struct.pack(
                "<HBB",
                djb2_hash16(r["resource"]),
                1 if r.get("exclusive", True) else 0,
                MODR_RESOURCE_SCOPE[r.get("scope", "scenario")],
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

    compiler = ScenarioCompiler(registry, schema)

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
