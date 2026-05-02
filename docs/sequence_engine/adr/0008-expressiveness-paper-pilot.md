# ADR-0008: Expressiveness Paper Pilot Validation

**Status:** Accepted
**Date:** 2026-05-02

## Context

Stage 1 plan Step 0.75 — paper pilot перед написанням binary format header (Step 1). Goal: validate that proposed JSON manifest format + binary `.modr` structure + tick-order semantics adequately express realistic universal recipe BEFORE locking specs through code.

Якщо paper recipe не expresses elegantly → revise spec while edits cheap. Cost-of-change post-Step 8 (loader written) є significant.

## Pilot recipe selected

**"Greenhouse irrigation cycle"** — universal pattern (greenhouse, hydroponics, agriculture, even industrial humidity control). Selected over alternatives:
- Lab batch process — too thermal-centric, не exercises wall-clock
- Generic batch reactor — domain-specific concepts (charge/discharge) confuse universal demonstration

Greenhouse irrigation exercises:
- 3 tracks (zones A, B, C — independent cycles)
- Wall-clock conditions (water только morning + evening, not noon)
- Cross-track sync (each zone has own moisture sensor; coordinated through SharedState)
- Resource arbitration (shared water pump — only one zone waters at a time)
- Long-running scenario (24h cycle з multiple irrigations)
- Power-loss recovery (resume у correct zone after reboot)

## Pilot recipe (paper authored)

```jsonc
{
  "manifest_version": 1,
  "module": "recipe_greenhouse_irrigation",
  "version": "1.0.0",
  "module_type": "recipe",
  "priority": 5,
  "description": "Universal 3-zone irrigation з wall-clock scheduling",

  "state": {
    // Scenario-level mirror keys (engine writes)
    "recipe_greenhouse_irrigation.scenario_state":     {"type":"string","access":"read"},
    "recipe_greenhouse_irrigation.scenario_elapsed_s": {"type":"int",   "access":"read"},

    // Per-track mirror keys
    "recipe_greenhouse_irrigation.zone_a_state":       {"type":"string","access":"read"},
    "recipe_greenhouse_irrigation.zone_a_phase_name":  {"type":"string","access":"read"},
    "recipe_greenhouse_irrigation.zone_a_elapsed_s":   {"type":"int",   "access":"read"},

    "recipe_greenhouse_irrigation.zone_b_state":       {"type":"string","access":"read"},
    "recipe_greenhouse_irrigation.zone_b_phase_name":  {"type":"string","access":"read"},
    "recipe_greenhouse_irrigation.zone_b_elapsed_s":   {"type":"int",   "access":"read"},

    "recipe_greenhouse_irrigation.zone_c_state":       {"type":"string","access":"read"},
    "recipe_greenhouse_irrigation.zone_c_phase_name":  {"type":"string","access":"read"},
    "recipe_greenhouse_irrigation.zone_c_elapsed_s":   {"type":"int",   "access":"read"}
  },

  "ui": {
    "page": "Полив",
    "icon": "watering_can",
    "cards": [{
      "title": "Стан зон",
      "layout": "single",
      "visible_when": {"recipe_greenhouse_irrigation.scenario_state": ["running","paused"]},
      "widgets": [
        {"key": "recipe_greenhouse_irrigation.zone_a_phase_name", "widget": "value"},
        {"key": "recipe_greenhouse_irrigation.zone_b_phase_name", "widget": "value"},
        {"key": "recipe_greenhouse_irrigation.zone_c_phase_name", "widget": "value"}
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
      // zone_b і zone_c — analogous structure, different valve targets
    ]
  }
}
```

## Findings: чи expressive?

### ✅ What works well

1. **Wall-clock + sensor-conditional triggering:**
   `time_of_day_eq` + `state_key_lt(moisture)` через `all_of` — natural composition. Recipe author easily reasons "water at 6:30 IF moisture below 40".

2. **Multi-track parallelism:**
   3 zones — each own track, each own phase progression. They share scenario timeline (start together at boot). They synchronize implicitly через `equipment.water_pump` resource (declared exclusive — only one zone watering at a time).

3. **Resource arbitration:**
   Pump declared exclusive. Якщо zone_a у "watering" фазі, zone_b's transition to "watering" буде wait until pump free. **Wait** — but my current spec only handles arbitration AT START, not during running scenario. This is gap → see "Findings: gaps" below.

4. **Cross-track sync via mirror keys:**
   Якщо потрібно — zone_b може check `recipe_greenhouse_irrigation.zone_a_phase_name == "watering"` і wait. Tick-order naturally allows це because zones declared у order.

5. **Power-loss recovery:**
   At t=8h into 24h cycle, zone_a у "wait_evening", zone_b у "watering_evening", zone_c у "wait_morning" → token saves all three phase_idx + elapsed. After boot, engine restores і enters PAUSED. User sees "scenario recovered" banner, resumes manually.

6. **Global transitions для safety:**
   Water leak detected → all tracks abort, exit actions run (pump OFF). Universal pattern.

7. **Mandatory phase timeouts:**
   Each phase has explicit `timeout_ms`. `wait_morning` has 24h timeout — sensible (recipe completes within day). `watering` has 10 min (safety bound).

### ❌ Findings: gaps в нашій specification

1. **Resource arbitration scope ambiguous.** ISA-88 §5.3 typically says: scenario claims resource at START, holds для entire scenario duration. But greenhouse use case wants: each zone claims pump during "watering" phase only, releases after. Multiple zones share pump throughout day.

   **Resolution:** **enhance plan Q8** — add concept of **phase-level resource claims** (in addition to scenario-level):
   - Scenario-level resources: claimed at start, released at end (current spec)
   - Phase-level resources: claimed at phase entry, released at phase exit (NEW)
   - Both can be declared у scenario header або per-phase

   **Action:** Update Q8 spec у plan + Step 10 (resource_arbiter) implementation. Document у ADR-0005.

2. **`time_of_day_eq` semantics ambiguous for "match window".**
   Current spec: HH:MM matching. Якщо engine ticks at 100Hz і time_of_day == 6:30 holds for ENTIRE minute (60s), transition fires once on first tick where match. Subsequent ticks within same minute — transition ALREADY fired from this phase, no re-fire.
   
   But what if scenario booted at 6:35 (already past trigger)? `time_of_day_eq(6:30)` was true at 6:30 but scenario не у correct phase. Recipe waits до next day 6:30.
   
   **Resolution:** **clarify spec** — `time_of_day_eq` matches на full minute granularity, fires once per minute when condition first becomes true within phase. Document edge case "missed trigger window" — recipe author's responsibility to handle (e.g., check moisture-only as fallback).
   
   **Action:** Add note до plan Q3 (built-in conditions) і `usage/02_writing_recipes.md`.

3. **Phase timeout vs cumulative scenario timeout interplay.**
   `wait_morning` has 24h timeout. `scenario_timeout_max_ms = 86400000` (24h). Якщо all phases reach individual timeouts of 10 min each, scenario could run 30 min total — well below cap. But якщо `wait_morning` actually waits 24h (rare boundary), scenario hits cap — acceptable.
   
   **Resolution:** Spec already correct; document interaction у `usage/02_writing_recipes.md`. No change needed.

4. **Recipe validation rule: any path to `$complete`?**
   Compiler should validate that кожен track has reachable path to `$complete` (or `$abort` with handler). Greenhouse scenario: zone_a `wait_evening` only transitions on time-of-day + moisture; if neither triggers within phase timeout, implicit transition to `$abort` saves us. Compiler should warn якщо all transitions are conditional and no fallback exists.
   
   **Action:** Add to compile_scenario.py validation (Step 2). Already implicit through mandatory phase timeouts (ADR-0007).

5. **Zone parameters not explicit.**
   Greenhouse recipe currently hardcodes thresholds (40%, 75%, 80%). User likely wants configurable.
   
   Solution: parameters у scenario (per Q1 binary format `modr_param_entry`). Recipe declares:
   ```jsonc
   "params": {
     "moisture_low":  {"type":"f32","default":40, "min":20, "max":60, "overridable":true},
     "moisture_high": {"type":"f32","default":75, "min":60, "max":90, "overridable":true}
   }
   ```
   Then conditions reference them: `state_key_lt({key:"sensor.zone_a_moisture", value:"@param:moisture_low"})`.
   
   **Resolution:** **enhance plan Q1** — confirm параметри referenceable from conditions/actions через `@param:<name>` syntax. Document.
   
   **Action:** Update Q1 і Q3 у plan. Add to `usage/02_writing_recipes.md`. Likely small — parameter resolver during recipe load.

### Conclusion

Spec is **80% adequate** для realistic recipes. 2 substantive enhancements identified before Step 1:

**Required spec updates (DO before Step 1):**
- Q8: Add **phase-level resource claims** (in addition to scenario-level). Update ADR-0005.
- Q1/Q3: Confirm **parameter referencing in conditions/actions** (`@param:<name>` syntax). Document як part of Q1.

**Documentation enhancements (DO у Step 2 і usage docs):**
- `time_of_day_eq` "match window" semantics
- Recipe validation: reachable paths to `$complete`
- Phase timeout vs scenario timeout interplay

**Format adequate to proceed.** Greenhouse pilot expressed cleanly з 2 small enhancements. **No major rework needed.**

## Decision

Proceed to Step 1 (modr_format.h) AFTER applying the two required spec updates above.

Refinements integrated as Step 0.85 patch: update plan Q1 (parameters in conditions) + Q8 (phase-level resources) + ADR-0005, then proceed.

## References

- Plan `.claude/plans/quirky-imagining-lake.md` Step 0.75 (paper pilot)
- This pilot recipe will eventually become `usage/examples/05_greenhouse_irrigation.md` у Stage 1.5
- Spec updates: pending Step 0.85
