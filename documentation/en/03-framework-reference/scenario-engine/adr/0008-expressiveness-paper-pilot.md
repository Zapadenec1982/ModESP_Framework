# ADR-0008: Expressiveness Paper Pilot Validation

> 📖 **Українською:** [documentation/uk/03-framework-reference/scenario-engine/adr/0008-expressiveness-paper-pilot.md](../../../../uk/03-framework-reference/scenario-engine/adr/0008-expressiveness-paper-pilot.md)

**Status:** Accepted
**Date:** 2026-05-02

## Context

Stage 1 plan Step 0.75 — a paper pilot before writing the binary-format header (Step 1). Goal: validate that the proposed JSON manifest format + binary `.modr` structure + tick-order semantics adequately express a realistic universal recipe BEFORE locking the specs in code.

If the paper recipe does not express cleanly → revise the spec while edits are cheap. The cost of change after Step 8 (loader written) is significant.

## Pilot recipe selected

**"Irrigation cycle"** (module name `recipe_irrig` — 12 chars, fits the ModESP 32-char SharedState key budget per existing convention). A universal pattern (greenhouse, hydroponics, agriculture, industrial humidity control). Selected over the alternatives:
- Lab batch process — too thermal-centric, does not exercise the wall-clock
- Generic batch reactor — domain-specific concepts (charge/discharge) confuse the universal demonstration

Greenhouse irrigation exercises:
- 3 tracks (zones A, B, C — independent cycles)
- Wall-clock conditions (water only in the morning and evening, not at noon)
- Cross-track sync (each zone has its own moisture sensor; coordinated through SharedState)
- Resource arbitration (shared water pump — only one zone waters at a time)
- Long-running scenario (24h cycle with multiple irrigations)
- Power-loss recovery (resume in the correct zone after reboot)

## Pilot recipe (paper authored)

```jsonc
{
  "manifest_version": 1,
  "module": "recipe_irrig",
  "version": "1.0.0",
  "module_type": "recipe",
  "priority": 5,
  "description": "Universal 3-zone irrigation with wall-clock scheduling",

  "state": {
    // Scenario-level mirror keys (engine writes)
    "recipe_irrig.scenario_state":     {"type":"string","access":"read"},
    "recipe_irrig.scenario_elapsed_s": {"type":"int",   "access":"read"},

    // Per-track mirror keys
    "recipe_irrig.zone_a_state":       {"type":"string","access":"read"},
    "recipe_irrig.zone_a_phase_name":  {"type":"string","access":"read"},
    "recipe_irrig.zone_a_elapsed_s":   {"type":"int",   "access":"read"},

    "recipe_irrig.zone_b_state":       {"type":"string","access":"read"},
    "recipe_irrig.zone_b_phase_name":  {"type":"string","access":"read"},
    "recipe_irrig.zone_b_elapsed_s":   {"type":"int",   "access":"read"},

    "recipe_irrig.zone_c_state":       {"type":"string","access":"read"},
    "recipe_irrig.zone_c_phase_name":  {"type":"string","access":"read"},
    "recipe_irrig.zone_c_elapsed_s":   {"type":"int",   "access":"read"}
  },

  "ui": {
    "page": "Irrigation",
    "icon": "watering_can",
    "cards": [{
      "title": "Zone state",
      "layout": "single",
      "visible_when": {"recipe_irrig.scenario_state": ["running","paused"]},
      "widgets": [
        {"key": "recipe_irrig.zone_a_phase_name", "widget": "value"},
        {"key": "recipe_irrig.zone_b_phase_name", "widget": "value"},
        {"key": "recipe_irrig.zone_c_phase_name", "widget": "value"}
      ]
    }]
  },

  "scenario": {
    "default_phase_timeout_ms": 7200000,    // 2 hours generic timeout
    "scenario_timeout_max_ms":  86400000,   // 24h hard cap (one daily cycle)
    "completion_rule": "all_tracks_complete",
    "resources": [
      {"resource": "equipment.water_pump", "exclusive": true}
    ],
    "global_transitions": [
      {"to": "$abort", "when": {"state_key_eq":{"key":"safety.water_leak","value":true}}, "priority": 255, "scope": "abort_scenario"},
      {"to": "$abort", "when": {"state_key_eq":{"key":"ui.user_abort","value":true}},     "priority": 200, "scope": "abort_scenario"}
    ],
    "tracks": [
      {
        "name": "zone_a",
        "flags": ["main_track"],
        "phases": [
          {"name": "wait_morning", "timeout_ms": 86400000,
           "entry": [{"action":"log","params":{"msg":"zone_a: awaiting morning window"}}],
           "transitions": [
             {"to": "watering", "when": {"all_of": [
               {"time_of_day_eq": {"hh": 6, "mm": 30}},
               {"state_key_lt": {"key": "sensor.zone_a_moisture", "value": 40}}
             ]}}
           ],
           "exit": []},
          {"name": "watering", "timeout_ms": 600000,    // 10 min max
           "entry": [
             {"action": "log", "params":{"msg":"zone_a: watering start"}},
             {"action": "set_state", "params":{"key":"equipment.req_pump","type":"bool","value":true}},
             {"action": "set_state", "params":{"key":"equipment.req_valve_a","type":"bool","value":true}}
           ],
           "transitions": [
             {"to": "wait_evening", "when": {"state_key_gt": {"key":"sensor.zone_a_moisture", "value": 75}}}
           ],
           "exit": [
             {"action": "set_state", "params":{"key":"equipment.req_pump","type":"bool","value":false}},
             {"action": "set_state", "params":{"key":"equipment.req_valve_a","type":"bool","value":false}}
           ]},
          {"name": "wait_evening", "timeout_ms": 86400000,
           "entry": [{"action":"log","params":{"msg":"zone_a: morning done, awaiting evening"}}],
           "transitions": [
             {"to": "watering_evening", "when": {"all_of": [
               {"time_of_day_eq": {"hh": 19, "mm": 0}},
               {"state_key_lt": {"key": "sensor.zone_a_moisture", "value": 50}}
             ]}}
           ],
           "exit": []},
          {"name": "watering_evening", "timeout_ms": 600000,
           "entry": [
             {"action": "log", "params":{"msg":"zone_a: evening watering"}},
             {"action": "set_state", "params":{"key":"equipment.req_pump","type":"bool","value":true}},
             {"action": "set_state", "params":{"key":"equipment.req_valve_a","type":"bool","value":true}}
           ],
           "transitions": [
             {"to": "$complete", "when": {"state_key_gt": {"key":"sensor.zone_a_moisture", "value": 80}}}
           ],
           "exit": [
             {"action": "set_state", "params":{"key":"equipment.req_pump","type":"bool","value":false}},
             {"action": "set_state", "params":{"key":"equipment.req_valve_a","type":"bool","value":false}}
           ]}
        ]
      }
      // zone_b and zone_c — analogous structure, different valve targets
    ]
  }
}
```

## Findings: is it expressive?

### What works well

1. **Wall-clock + sensor-conditional triggering:**
   `time_of_day_eq` + `state_key_lt(moisture)` composed via `all_of` — a natural composition. The recipe author easily reasons "water at 6:30 IF moisture is below 40".

2. **Multi-track parallelism:**
   3 zones — each its own track, each with its own phase progression. They share a scenario timeline (start together at boot). They synchronize implicitly via the `equipment.water_pump` resource (declared exclusive — only one zone watering at a time).

3. **Resource arbitration:**
   The pump is declared exclusive. If zone_a is in the "watering" phase, zone_b's transition to "watering" will wait until the pump is free. **However** — the current spec only handles arbitration AT START, not during a running scenario. This is a gap → see "Findings: gaps" below.

4. **Cross-track sync via mirror keys:**
   When needed, zone_b can check `recipe_irrig.zone_a_phase_name == "watering"` and wait. Tick-order naturally allows this because the zones are declared in order.

5. **Power-loss recovery:**
   At t=8h into the 24h cycle, zone_a is in "wait_evening", zone_b is in "watering_evening", zone_c is in "wait_morning" → the token saves all three phase_idx + elapsed values. After boot, the Engine restores them and enters PAUSED. The user sees a "scenario recovered" banner and resumes manually.

6. **Global transitions for safety:**
   Water leak detected → all tracks abort, exit actions run (pump OFF). A universal pattern.

7. **Mandatory phase timeouts:**
   Each phase has an explicit `timeout_ms`. `wait_morning` has a 24h timeout — sensible (the recipe completes within a day). `watering` has 10 min (safety bound).

### Findings: gaps in our specification

1. **Resource arbitration scope is ambiguous.** ISA-88 §5.3 typically says: a scenario claims a resource at START and holds it for the entire scenario duration. But the greenhouse use case wants each zone to claim the pump only during its "watering" phase and release it afterwards. Multiple zones share the pump throughout the day.

   **Resolution:** **enhance plan Q8** — add the concept of **phase-level resource claims** (in addition to scenario-level):
   - Scenario-level resources: claimed at start, released at end (current spec)
   - Phase-level resources: claimed at phase entry, released at phase exit (NEW)
   - Both can be declared in the scenario header or per phase

   **Action:** Update the Q8 spec in the plan + Step 10 (resource_arbiter) implementation. Document in ADR-0005.

2. **`time_of_day_eq` semantics ambiguous for the "match window".**
   Current spec: HH:MM matching. If the Engine ticks at 100Hz and `time_of_day == 6:30` holds for the ENTIRE minute (60s), the transition fires once on the first tick where it matches. Subsequent ticks within the same minute — the transition has ALREADY fired from this phase, so no re-fire.

   But what if the scenario booted at 6:35 (already past the trigger)? `time_of_day_eq(6:30)` was true at 6:30 but the scenario was not in the right phase. The recipe waits until the next day's 6:30.

   **Resolution:** **clarify the spec** — `time_of_day_eq` matches at full-minute granularity, fires once per minute when the condition first becomes true within the phase. Document the edge case "missed trigger window" — the recipe author's responsibility to handle (e.g., fall back to a moisture-only check).

   **Action:** Add a note to plan Q3 (built-in conditions) and `usage/02_writing_recipes.md`.

3. **Phase timeout vs cumulative scenario timeout interplay.**
   `wait_morning` has a 24h timeout. `scenario_timeout_max_ms = 86400000` (24h). If all phases reach their individual 10-minute timeouts, the scenario could run 30 min total — well below the cap. But if `wait_morning` actually waits 24h (a rare boundary), the scenario hits the cap — acceptable.

   **Resolution:** The spec is already correct; document the interaction in `usage/02_writing_recipes.md`. No change needed.

4. **Recipe validation rule: does every track have a path to `$complete`?**
   The compiler should validate that each track has a reachable path to `$complete` (or `$abort` with a handler). Greenhouse scenario: `zone_a` `wait_evening` only transitions on time-of-day + moisture; if neither triggers within the phase timeout, the implicit transition to `$abort` saves us. The compiler should warn if all transitions are conditional and no fallback exists.

   **Action:** Add to `compile_scenario.py` validation (Step 2). Already implicit through mandatory phase timeouts (ADR-0007).

5. **Zone parameters are not explicit.**
   The greenhouse recipe currently hardcodes thresholds (40%, 75%, 80%). The user likely wants them configurable.

   Solution: parameters in the scenario (per Q1 binary format `modr_param_entry`). The recipe declares:
   ```jsonc
   "params": {
     "moisture_low":  {"type":"f32","default":40, "min":20, "max":60, "overridable":true},
     "moisture_high": {"type":"f32","default":75, "min":60, "max":90, "overridable":true}
   }
   ```
   Then conditions reference them: `state_key_lt({key:"sensor.zone_a_moisture", value:"@param:moisture_low"})`.

   **Resolution:** **enhance plan Q1** — confirm parameters are referenceable from conditions/actions via `@param:<name>` syntax. Document.

   **Action:** Update Q1 and Q3 in the plan. Add to `usage/02_writing_recipes.md`. Likely small — a parameter resolver during recipe load.

### Conclusion

The spec is **80% adequate** for realistic recipes. Two substantive enhancements were identified before Step 1:

**Required spec updates (DO before Step 1):**
- Q8: Add **phase-level resource claims** (in addition to scenario-level). Update ADR-0005.
- Q1/Q3: Confirm **parameter referencing in conditions/actions** (`@param:<name>` syntax). Document as part of Q1.

**Documentation enhancements (DO in Step 2 and the usage docs):**
- `time_of_day_eq` "match window" semantics
- Recipe validation: reachable paths to `$complete`
- Phase timeout vs scenario timeout interplay

**Format adequate to proceed.** The greenhouse pilot expressed cleanly with two small enhancements. **No major rework needed.**

## Decision

Proceed to Step 1 (modr_format.h) AFTER applying the two required spec updates above.

Refinements are integrated as a Step 0.85 patch: update plan Q1 (parameters in conditions) + Q8 (phase-level resources) + ADR-0005, then proceed.

## References

- Plan `.claude/plans/quirky-imagining-lake.md` Step 0.75 (paper pilot)
- This pilot recipe will eventually become `usage/examples/05_irrigation_cycle.md` in Stage 1.5
- Spec updates: pending Step 0.85
