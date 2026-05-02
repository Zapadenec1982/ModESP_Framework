#!/usr/bin/env python3
"""
dump_modr.py — human-readable inspector для compiled `.modr` binaries.

Analog `gcc + objdump` для ELF, `protoc --decode` для protobuf, `llvm-objdump`
для LLVM bitcode. NOT a full decompiler back to JSON (Stage 2 у TypeScript для
WebUI). Це debugging tool — pretty-prints binary structure для:
  - HIL test failures (inspect emitted binary structure)
  - Schema vs binary mismatches (golden file regression debugging)
  - Format version migrations (compare layouts)
  - Manual sanity checks under development

Per Q9 (Step 2b review): includes Python inspector у Stage 1; full TypeScript
decompiler з round-trip JSON у Stage 2 разом з WebUI editor.

USAGE:
    python tools/dump_modr.py path/to/recipe.modr
    python tools/dump_modr.py --hex path/to/recipe.modr   # +raw byte dump
"""

from __future__ import annotations

import argparse
import struct
import sys
import zlib
from pathlib import Path
from typing import Any

# Constants matching modr_format.h. Keep у sync.
MODR_MAGIC = 0x52444F4D
MODR_FORMAT_VERSION = 1
MODR_NO_OFFSET = 0xFFFF
MODR_TARGET_COMPLETE = 0xFFFF
MODR_TARGET_ABORT = 0xFFFE

SIZE_HEADER = 56
SIZE_TRACK = 16
SIZE_PHASE = 20
SIZE_TRANSITION = 12
SIZE_GLOBAL_TRANSITION = 8
SIZE_ACTION = 8
SIZE_PARAM_ENTRY = 8
SIZE_RESOURCE_DECL = 4
SIZE_PHASE_RESOURCE_CLAIM = 4

COMPLETION_RULE_NAMES = {0: "all_tracks_complete", 1: "any_track_complete", 2: "main_track_complete"}
TRANS_KIND_NAMES = {0: "TIME", 1: "COND", 2: "TIME_OR_COND", 3: "TIME_AND_COND", 4: "UNCONDITIONAL"}
PARAM_TYPE_NAMES = {0: "i32", 1: "f32", 2: "bool", 3: "str"}
GLOBAL_SCOPE_NAMES = {0: "abort_scenario", 1: "abort_only_main_track"}


def djb2_hash16(s: str) -> int:
    h = 5381
    for ch in s.encode("utf-8"):
        h = ((h << 5) + h + ch) & 0xFFFFFFFF
    return h & 0xFFFF


def read_string_from_pool(data: bytes, pool_off: int, str_idx: int) -> str:
    """Read length-prefixed string з pool at given byte offset."""
    if str_idx >= len(data):
        return f"<INVALID_OFFSET 0x{str_idx:04x}>"
    length = data[pool_off + str_idx]
    start = pool_off + str_idx + 1
    return data[start:start + length].decode("utf-8", errors="replace")


def format_offset(name: str, val: int) -> str:
    if val == MODR_NO_OFFSET:
        return f"{name}=NO_OFFSET"
    return f"{name}=0x{val:04x}"


def format_target(target: int) -> str:
    if target == MODR_TARGET_COMPLETE:
        return "$complete"
    if target == MODR_TARGET_ABORT:
        return "$abort"
    return f"phase[{target}]"


def dump_header(data: bytes) -> dict[str, Any]:
    """Parse і print header. Returns parsed fields як dict."""
    if len(data) < SIZE_HEADER:
        raise ValueError(f"file {len(data)} bytes — too short for header ({SIZE_HEADER})")

    fields = struct.unpack_from(
        "<IHHIHHBBBBBBHHHHHHHHHHHIII", data, 0
    )
    h = {
        "magic": fields[0],
        "format_version": fields[1],
        "flags": fields[2],
        "total_size": fields[3],
        "scenario_id": fields[4],
        "name_str_idx": fields[5],
        "track_count": fields[6],
        "cont_count": fields[7],
        "resource_count": fields[8],
        "global_trans_count": fields[9],
        "completion_rule": fields[10],
        "reserved_a": fields[11],
        "track_table_off": fields[12],
        "param_pool_off": fields[13],
        "param_pool_count": fields[14],
        "action_pool_off": fields[15],
        "action_pool_count": fields[16],
        "cond_pool_off": fields[17],
        "cond_pool_count": fields[18],
        "string_pool_off": fields[19],
        "global_trans_off": fields[20],
        "resource_off": fields[21],
        "reserved_b": fields[22],
        "default_phase_timeout_ms": fields[23],
        "scenario_timeout_max_ms": fields[24],
        "reserved_c": fields[25],
    }

    print("=== HEADER ===")
    print(f"  magic            = 0x{h['magic']:08x} ({'OK MODR' if h['magic'] == MODR_MAGIC else 'WRONG!'})")
    print(f"  format_version   = {h['format_version']}")
    print(f"  flags            = 0x{h['flags']:04x}", end="")
    flag_parts = []
    if h["flags"] & 0x01: flag_parts.append("CONTINUOUS_USED")
    if h["flags"] & 0x02: flag_parts.append("HAS_GLOBALS")
    if h["flags"] & 0x04: flag_parts.append("HAS_RESOURCES")
    if flag_parts:
        print(f" [{', '.join(flag_parts)}]")
    else:
        print(" []")
    print(f"  total_size       = {h['total_size']} bytes (file: {len(data)} bytes — {'OK' if h['total_size'] == len(data) else 'MISMATCH'})")
    name = read_string_from_pool(data, h["string_pool_off"], h["name_str_idx"])
    print(f"  scenario_id      = 0x{h['scenario_id']:04x} (djb2_hash16({name!r}) = 0x{djb2_hash16(name):04x})")
    print(f"  name             = {name!r}")
    print(f"  track_count      = {h['track_count']}")
    print(f"  cont_count       = {h['cont_count']}")
    print(f"  resource_count   = {h['resource_count']}")
    print(f"  global_trans_n   = {h['global_trans_count']}")
    print(f"  completion_rule  = {h['completion_rule']} ({COMPLETION_RULE_NAMES.get(h['completion_rule'], '?')})")
    print(f"  default_timeout  = {h['default_phase_timeout_ms']} ms")
    if h["scenario_timeout_max_ms"] == 0:
        print(f"  scenario_max_ms  = 0 (unlimited)")
    else:
        print(f"  scenario_max_ms  = {h['scenario_timeout_max_ms']} ms ({h['scenario_timeout_max_ms'] / 1000.0:.1f}s)")
    print()
    print("Pool offsets:")
    print(f"  track_table       0x{h['track_table_off']:04x}")
    print(f"  action_pool       0x{h['action_pool_off']:04x}  count={h['action_pool_count']}")
    print(f"  cond_pool         0x{h['cond_pool_off']:04x}  count={h['cond_pool_count']}")
    print(f"  param_pool        0x{h['param_pool_off']:04x}  count={h['param_pool_count']}")
    print(f"  global_trans      0x{h['global_trans_off']:04x}")
    print(f"  resource_decl     0x{h['resource_off']:04x}")
    print(f"  string_pool       0x{h['string_pool_off']:04x}")
    print()
    return h


def dump_tracks(data: bytes, h: dict[str, Any]) -> None:
    print("=== TRACKS ===")
    pool_off = h["string_pool_off"]
    for ti in range(h["track_count"]):
        off = h["track_table_off"] + ti * SIZE_TRACK
        name_idx, phases_off, initial, _ra, phase_count, flags, _rb, _rc = struct.unpack_from(
            "<HHHHBBHI", data, off
        )
        name = read_string_from_pool(data, pool_off, name_idx)
        flag_parts = []
        if flags & 0x01: flag_parts.append("MAIN")
        if flags & 0x02: flag_parts.append("LOOP")
        flag_str = ",".join(flag_parts) if flag_parts else "—"
        print(f"  Track[{ti}] {name!r}  phases={phase_count}  initial={initial}  flags=[{flag_str}]")
        # Dump phases
        for pi in range(phase_count):
            poff = phases_off + pi * SIZE_PHASE
            pn_idx, en_off, ex_off, tr_off, en_n, ex_n, tr_n, cmask, timeout, pr_off, pr_n, _r = struct.unpack_from(
                "<HHHHBBBBIHBB", data, poff
            )
            pname = read_string_from_pool(data, pool_off, pn_idx)
            print(f"    Phase[{pi}] {pname!r}  timeout_ms={timeout}  entry_actions={en_n}  "
                  f"exit_actions={ex_n}  transitions={tr_n}  cont_mask=0x{cmask:02x}  "
                  f"phase_resources={pr_n}")
            # Dump transitions
            for trni in range(tr_n):
                troff = tr_off + trni * SIZE_TRANSITION
                cidx, target, threshold, kind, _ra, _rb = struct.unpack_from(
                    "<HHIBBH", data, troff
                )
                kind_name = TRANS_KIND_NAMES.get(kind, f"?{kind}")
                cidx_str = "—" if cidx == MODR_NO_OFFSET else f"cond[{cidx}]"
                thresh_str = f"{threshold}ms" if threshold > 0 else "—"
                print(f"      → {format_target(target)}  kind={kind_name}  "
                      f"cond={cidx_str}  time_threshold={thresh_str}")


def dump_pools(data: bytes, h: dict[str, Any]) -> None:
    pool_off = h["string_pool_off"]

    if h["action_pool_count"] > 0:
        print()
        print("=== ACTION POOL ===")
        for i in range(h["action_pool_count"]):
            off = h["action_pool_off"] + i * SIZE_ACTION
            ahash, pidx, pn, flg, _r = struct.unpack_from("<HHBBH", data, off)
            print(f"  Action[{i}]  hash=0x{ahash:04x}  param_idx={pidx if pidx != MODR_NO_OFFSET else 'NONE'}  param_n={pn}  flags=0x{flg:02x}")

    if h["cond_pool_count"] > 0:
        print()
        print("=== COND POOL ===")
        for i in range(h["cond_pool_count"]):
            off = h["cond_pool_off"] + i * SIZE_ACTION
            chash, pidx, pn, flg, _r = struct.unpack_from("<HHBBH", data, off)
            print(f"  Cond[{i}]  hash=0x{chash:04x}  param_idx={pidx if pidx != MODR_NO_OFFSET else 'NONE'}  param_n={pn}  flags=0x{flg:02x}")

    if h["param_pool_count"] > 0:
        print()
        print("=== PARAM POOL ===")
        for i in range(h["param_pool_count"]):
            off = h["param_pool_off"] + i * SIZE_PARAM_ENTRY
            khash, ptype, pflags, value = struct.unpack_from("<HBBI", data, off)
            type_name = PARAM_TYPE_NAMES.get(ptype, f"?{ptype}")
            # Decode value based on type
            if ptype == 0:  # i32
                value_str = str(struct.unpack("<i", struct.pack("<I", value))[0])
            elif ptype == 1:  # f32
                value_str = str(struct.unpack("<f", struct.pack("<I", value))[0])
            elif ptype == 2:  # bool
                value_str = "true" if value else "false"
            elif ptype == 3:  # str
                value_str = repr(read_string_from_pool(data, pool_off, value & 0xFFFF))
            else:
                value_str = f"raw=0x{value:08x}"
            print(f"  Param[{i}]  key_hash=0x{khash:04x}  type={type_name}  flags=0x{pflags:02x}  value={value_str}")


def dump_globals_and_resources(data: bytes, h: dict[str, Any]) -> None:
    if h["global_trans_count"] > 0:
        print()
        print("=== GLOBAL TRANSITIONS ===")
        for i in range(h["global_trans_count"]):
            off = h["global_trans_off"] + i * SIZE_GLOBAL_TRANSITION
            cidx, _ra, kind, priority, scope, _rb = struct.unpack_from("<HHBBBB", data, off)
            scope_name = GLOBAL_SCOPE_NAMES.get(scope, f"?{scope}")
            print(f"  Global[{i}]  cond=cond[{cidx}]  priority={priority}  scope={scope_name}  kind={TRANS_KIND_NAMES.get(kind, '?')}")

    if h["resource_count"] > 0:
        print()
        print("=== SCENARIO RESOURCES ===")
        for i in range(h["resource_count"]):
            off = h["resource_off"] + i * SIZE_RESOURCE_DECL
            rhash, exclusive, scope = struct.unpack_from("<HBB", data, off)
            print(f"  Resource[{i}]  hash=0x{rhash:04x}  exclusive={bool(exclusive)}  scope={scope}")


def dump_string_pool(data: bytes, h: dict[str, Any]) -> None:
    print()
    print("=== STRING POOL ===")
    off = h["string_pool_off"]
    end = h["total_size"] - 4  # exclude CRC trailer
    cursor = off
    idx = 0
    while cursor < end:
        length = data[cursor]
        s = data[cursor + 1:cursor + 1 + length].decode("utf-8", errors="replace")
        print(f"  [0x{cursor - off:04x}]  {s!r}")
        cursor += 1 + length
        idx += 1
        if idx > 100:  # safety
            print("  ... (truncated, > 100 entries)")
            break


def dump_crc(data: bytes) -> None:
    print()
    print("=== CRC32 TRAILER ===")
    crc_stored = struct.unpack_from("<I", data, len(data) - 4)[0]
    crc_computed = zlib.crc32(data[:-4]) & 0xFFFFFFFF
    status = "OK" if crc_stored == crc_computed else "MISMATCH"
    print(f"  stored:    0x{crc_stored:08x}")
    print(f"  computed:  0x{crc_computed:08x}")
    print(f"  status:    {status}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0] if __doc__ else "")
    parser.add_argument("file", type=Path, help="path to .modr file")
    parser.add_argument("--hex", action="store_true", help="show raw byte dump too")
    args = parser.parse_args(argv)

    if not args.file.exists():
        print(f"file not found: {args.file}", file=sys.stderr)
        return 1

    data = args.file.read_bytes()
    print(f"File: {args.file}  ({len(data)} bytes)")
    print()

    try:
        h = dump_header(data)
        dump_tracks(data, h)
        dump_pools(data, h)
        dump_globals_and_resources(data, h)
        dump_string_pool(data, h)
        dump_crc(data)
    except (struct.error, ValueError) as e:
        print(f"\nERROR parsing binary: {e}", file=sys.stderr)
        return 2

    if args.hex:
        print()
        print("=== RAW BYTES ===")
        for i in range(0, len(data), 16):
            chunk = data[i:i + 16]
            hex_part = " ".join(f"{b:02x}" for b in chunk)
            ascii_part = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
            print(f"  {i:04x}:  {hex_part:<48}  {ascii_part}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
