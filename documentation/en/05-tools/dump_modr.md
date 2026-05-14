# `dump_modr.py` — `.modr` binary inspector

> 📖 **Українською:** [documentation/uk/05-tools/dump_modr.md](../../uk/05-tools/dump_modr.md)

Human-readable inspector for compiled `.modr` recipe binaries. Analog
of `objdump` for ELF або `protoc --decode` for protobuf. Not а full
decompiler back to JSON (planned for Stage 2 у TypeScript alongside the
WebUI editor) — це is а debugging utility.

REQUIRES: Python 3.8+. No external dependencies.

```
python tools/dump_modr.py path/to/recipe.modr
python tools/dump_modr.py --hex path/to/recipe.modr
```

The `--hex` flag adds а raw byte dump alongside the structured view.

## When to use it

- **HIL test failures** — inspect what binary the compiler emitted to
  diagnose engine misbehaviour.
- **Schema vs binary mismatch** — regression-debug golden files when
  changing the binary format.
- **Format version migrations** — compare layouts side-by-side.
- **Manual sanity checks** under active development of the engine.

## Output format

Sample (truncated):

```
== Header ==
magic       = 0x52444F4D ('MODR')
version     = 1
size_bytes  = 412
crc32       = 0xAABBCCDD
tracks_count = 2
phases_count = 4
...

== Tracks ==
[0] name='main'    flags=0x01 (MAIN)  phases=0x0040..0x0080
[1] name='watcher' flags=0x00         phases=0x0080..0x0094

== Phases ==
[0] track=0 name='phase_a' timeout=10000ms transitions=0x0100..0x010c
    actions: [log msg=...] [set_state key='test.output_a' bool=true]
[1] ...

== Transitions ==
[0] kind=TIME   target=phase[1] time_ms=1000
[1] kind=COND   target=phase[2] cond_hash=0xABCD
    params: [key='test.input_a' i32=10]
...

== String pool ==
0x0000: 'main'
0x0006: 'watcher'
0x000f: 'phase_a'
...
```

The output reads top-to-bottom mirroring the binary layout. Each section
prints its absolute byte offset, sub-record count, і decoded fields.

## Special markers

- `$complete` — transition target 0xFFFF (scenario success).
- `$abort` — transition target 0xFFFE (scenario failure).
- `NO_OFFSET` — 0xFFFF placeholder, used for "no children" pointers.

## Format reference

The binary layout matches the C++ headers:

| Section | Size constant у `modr_format.h` | Bytes |
|---|---|---|
| Header | `SIZE_HEADER` | 56 |
| Track | `SIZE_TRACK` | 16 |
| Phase | `SIZE_PHASE` | 20 |
| Transition | `SIZE_TRANSITION` | 12 |
| Action | `SIZE_ACTION` | 8 |
| Param entry | `SIZE_PARAM_ENTRY` | 8 |
| Resource decl | `SIZE_RESOURCE_DECL` | 4 |

Order у the file: header → tracks → phases → transitions → actions →
params → resources → string pool → CRC32 footer.

## CRC validation

The tool verifies CRC32 на load. If the footer CRC doesn't match the
computed CRC of bytes [0..size_bytes), it prints а red error і still
dumps the structure best-effort. Use це to catch corrupted files
(usually from а bad partition flash).

## Hash decoding

Action AND condition references use djb2_hash16 hashes. The tool tries
to reverse them using `tools/known_actions.json` — if а hash matches
а known action, it prints the name; otherwise it leaves the raw hex.

```
[0] action_hash=0x4d2a    # 'set_state'
[1] action_hash=0xff01    # <unknown — was the action removed from known_actions.json?>
```

If you see `<unknown>`, either the recipe was compiled against а newer
`known_actions.json` than what's checked in, або the action was renamed.

## Common pitfalls

**`magic mismatch`:** file isn't а `.modr` (truncated, wrong format,
або wrong version). Re-run `compile_scenario.py` and check input.

**Truncated dump:** if `size_bytes` у header > actual file size,
the file is incomplete. Probably а failed flash або partial write.

**Output too long:** large recipes can scroll several screens. Pipe to
а pager (`| less`) або redirect to file (`> dump.txt`).

## Next steps

- **[compile_scenario.md](compile_scenario.md)** — produces the `.modr`
  files це tool inspects.
- **[03-framework-reference/components/modesp_scenario.md](../03-framework-reference/components/modesp_scenario.md)** —
  engine that loads `.modr` at runtime.

## Source

- [`tools/dump_modr.py`](../../../tools/dump_modr.py)
- [`components/modesp_scenario/include/modesp/scenario/modr_format.h`](../../../components/modesp_scenario/include/modesp/scenario/modr_format.h)
