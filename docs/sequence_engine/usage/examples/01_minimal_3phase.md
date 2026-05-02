# Example 01: Minimal 3-Phase Single-Track Recipe

**Status:** placeholder. Заповнюється у Step 16 (reference recipe + integration).

## Заповнюється

Mirrors `modules/recipe_abstract_test/manifest.json` — найпростіший single-track recipe:
- 3 phases: phase_a → phase_b → phase_c → $complete
- 1 track: "main" (degenerate single-track scenario)
- Built-ins exercised: `log`, `set_state`, `time_elapsed_ms`, `state_key_gt`, `all_of`
- Walks through:
  - JSON manifest structure
  - Compile pipeline (build → `.modr` artifact)
  - Runtime (load handle → start → observe transitions)
  - Mirror state keys updates (visible у WebUI page)

Compilable JSON validated by `tools/tests/test_compile_scenario.py`.
