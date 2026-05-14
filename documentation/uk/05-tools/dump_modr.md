# `dump_modr.py` — інспектор `.modr` binary

> 📖 **In English:** [documentation/en/05-tools/dump_modr.md](../../en/05-tools/dump_modr.md)

Human-readable inspector для compiled `.modr` recipe binaries. Analog
`objdump` для ELF або `protoc --decode` для protobuf. Не full
decompiler back до JSON (planned для Stage 2 у TypeScript alongside
WebUI editor) — це debugging utility.

REQUIRES: Python 3.8+. No external dependencies.

```
python tools/dump_modr.py path/to/recipe.modr
python tools/dump_modr.py --hex path/to/recipe.modr
```

`--hex` flag adds raw byte dump alongside structured view.

## Коли використовувати

- **HIL test failures** — inspect який binary compiler emitted щоб
  diagnose engine misbehaviour.
- **Schema vs binary mismatch** — regression-debug golden files при
  changing binary format.
- **Format version migrations** — compare layouts side-by-side.
- **Manual sanity checks** під active development engine.

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

Output reads top-to-bottom mirroring binary layout. Each section
prints absolute byte offset, sub-record count, і decoded fields.

## Special markers

- `$complete` — transition target 0xFFFF (scenario success).
- `$abort` — transition target 0xFFFE (scenario failure).
- `NO_OFFSET` — 0xFFFF placeholder, used для "no children" pointers.

## Format reference

Binary layout matches C++ headers:

| Section | Size constant у `modr_format.h` | Bytes |
|---|---|---|
| Header | `SIZE_HEADER` | 56 |
| Track | `SIZE_TRACK` | 16 |
| Phase | `SIZE_PHASE` | 20 |
| Transition | `SIZE_TRANSITION` | 12 |
| Action | `SIZE_ACTION` | 8 |
| Param entry | `SIZE_PARAM_ENTRY` | 8 |
| Resource decl | `SIZE_RESOURCE_DECL` | 4 |

Order у file: header → tracks → phases → transitions → actions →
params → resources → string pool → CRC32 footer.

## CRC validation

Tool verifies CRC32 на load. Якщо footer CRC не match computed CRC
of bytes [0..size_bytes), prints red error і still
dumps structure best-effort. Use це щоб catch corrupted files
(usually з bad partition flash).

## Hash decoding

Action І condition references use djb2_hash16 hashes. Tool tries
reverse їх using `tools/known_actions.json` — якщо hash matches
known action, prints name; otherwise leaves raw hex.

```
[0] action_hash=0x4d2a    # 'set_state'
[1] action_hash=0xff01    # <unknown — was the action removed from known_actions.json?>
```

Якщо бачите `<unknown>`, або recipe compiled проти newer
`known_actions.json` ніж checked-in version, або action renamed.

## Common pitfalls

**`magic mismatch`:** file не `.modr` (truncated, wrong format,
або wrong version). Re-run `compile_scenario.py` і check input.

**Truncated dump:** якщо `size_bytes` у header > actual file size,
файл incomplete. Probably failed flash або partial write.

**Output too long:** large recipes can scroll several screens. Pipe до
pager (`| less`) або redirect до file (`> dump.txt`).

## Що далі

- **[compile_scenario.md](compile_scenario.md)** — produces `.modr`
  files які цей tool inspects.
- **[03-framework-reference/components/modesp_scenario.md](../03-framework-reference/components/modesp_scenario.md)** —
  engine що loads `.modr` at runtime.

## Source

- [`tools/dump_modr.py`](../../../tools/dump_modr.py)
- [`components/modesp_scenario/include/modesp/scenario/modr_format.h`](../../../components/modesp_scenario/include/modesp/scenario/modr_format.h)
