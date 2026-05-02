# Scenario Test Fixtures

Golden binaries used by `tools/tests/test_modr_format.py`.

## Files

| File | Description |
|------|-------------|
| `minimal_v1.modr` | Smallest valid v1 scenario: 1 track ("m"), 1 phase ("p"), 1 unconditional transition to $complete. ~108 bytes. Used to validate header layout, alignment, CRC32 trailer. |

## Regeneration

Goldens are committed binaries. Update procedure (post Step 2 коли compile_scenario.py exists):

```bash
python tools/compile_scenario.py --regenerate-goldens
```

Use з explicit confirm prompt — overwriting goldens is significant change requiring review.

For Step 1 (current state): golden constructed manually у `test_modr_format.py::build_minimal_v1()`
matching `modr_format.h` struct layout. Bytes hand-encoded і CRC32 computed via zlib.

Any divergence between Python emitter і C++ struct layout → test failure → fix mismatch
before merging. This is the primary regression catch.
