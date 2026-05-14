# ModESP v4 — Changelog

> 📖 **Українською:** [documentation/uk/CHANGELOG.md](../uk/CHANGELOG.md)

> Full project changelog.

## 2026-03-16

- **feat(i18n): multilingual interface (UK/EN/DE/PL):**
  - Architecture: separate language packs in LittleFS (`data/www/i18n/{lang}.json`)
  - Per-module translation files: `modules/*/i18n/{en,de,pl}.json`
  - The generator collects module + system translations → merged language pack (674 keys)
  - Frontend: lazy-load of the language pack on language change (`fetch('/i18n/{lang}.json')`)
  - Ukrainian — default (embedded in ui.json, no fetch)
  - `cycleLanguage()` — cyclic switching UK → EN → DE → PL
  - Removed `uiEn.js` (327 entries) — replaced by structured keys + reverse map
  - Adding a new language = translation files, no code changes
  - Professional refrigeration terminology: DE (Verdichter, Abtauung), PL (sprężarka, odszranianie)

- **feat(aws): AWS IoT Core integration (feature/aws-iot branch):**
  - Compile-time switch via Kconfig (MQTT default, AWS optional)
  - mTLS connection, telemetry delta-publish, commands via MQTT
  - Device Shadow (62 reported keys + delta apply)
  - IoT Jobs OTA (download → flash → reboot → validate)
  - NVS 32KB for cert+key storage, JSON unescape for PEM
  - WiFi deferred start (crash fix), JSMN_STATIC (linker fix)
  - Verified on real ESP32: mTLS + telemetry + Shadow + OTA Jobs
  - 15 commits on feature/aws-iot, merged to main

- **docs: documentation overhaul for portfolio:**
  - docs/FEATURES.md (EN) + FEATURES_UA.md (UA) — 13-section feature overview
  - docs/CLOUD_INTEGRATION.md — ModESP Cloud integration guide
  - docs/12_aws_iot.md — full AWS IoT Core documentation
  - README.md redesigned: key metrics, Technical Highlights for Reviewers
  - .rules/ portable core: 9 rule files for Claude Code

- **fix(bindings): show all unassigned roles in "Add equipment"**
  - Required roles (air_temp) now reappear after removal
  - Cleaned DS18B20 ROM addresses from factory bindings.json

- **fix(thermostat): setpoint range -50..+50 → -30..+20°C**
- **fix(ui): rename "Холодильна камера" → "Охолодження" (Cooling)**
- **fix(mqtt): persist prefix in NVS on _set_tenant (pending after reboot)**
- **License: Source Available (PolyForm Noncommercial 1.0.0)**

- **refactor(scenario): full rebuild of the `modesp_sequence` engine → `modesp_scenario`:**
  - Clean break after 38 monolith commits (4,415 LOC, 157 tests passing, but cohesion was lost).
  - A single `modesp_scenario` component with internal subdirectories (core/actions/continuous/arbiter/observers) — no overengineering into 6 separate components.
  - Namespace `modesp::sequence` → `modesp::scenario`. Class `SequenceEngine` → `Engine`.
  - Killed singletons: `ActionRegistry::instance()` and `ContinuousRegistry::instance()` — registries are now caller-owned, injected into Engine via constructor.
  - `IStateBackend` — thin interface (2 raw virtuals + non-virtual typed templated helpers); production wraps `modesp::SharedState` via an adapter in `main/`.
  - `IEngineObserver` — 3 hooks (`on_scenario_started`, `on_phase_entered`, `on_scenario_terminal`) with empty defaults; observers as `etl::span<IEngineObserver*>` in the constructor.
  - Mirror writes = **direct calls** (`mirror::publish` in `private/mirror.h`), not observer events.
  - NVS persistence = **the single production observer** (`NvsObserver`); the engine is no longer the owner of persistence logic.
  - NVS magic bump `'SQTK'` → `'SCTK'` — old pre-rebuild tokens are clean-rejected on boot.
  - `ResourceArbiter` — concrete engine-owned member (no `IResourceArbiter` interface).
  - HIL pytest: 6/6 passing on real ESP32 (single-instance, multi-instance, resource contention, global transition, power-cycle recovery, WebUI mirror updates).
  - Engine.cpp after lift: ~320 LOC. Total component: ~3,500-3,800 LOC (vs 4,415 before).

- **feat(scenario): Stage 2 — standard continuous primitives:**
  - `PidController`, `HysteresisController`, `RampProfile` in `continuous_primitives.h`.
  - `register_primitives(registry)` helper — registration in the caller-owned `ContinuousRegistry`.
  - ADR-0006 "no built-in continuous behaviors" updated: the stage-1 decision is superseded, primitives now ship with the framework, but domain modules can still register their own.

- **docs: strategic rewrite of the documentation under `documentation/` (clean-slate):**
  - A single quality standard fixed in `documentation/STYLE.md`.
  - Bilingual structure EN + UK; on the commit date ~150 pages, all ready.
  - Module Author Guide (13 pages): manifest, writing-a-module/driver, shared-state, ui-widgets, mqtt, persistence, recipe-authoring/actions, continuous-behaviors, debugging, best-practices.
  - Framework Reference: architecture + 8 components + 4 reference modules + 6 reference drivers + scenario-engine deep dive (11 + 8 ADRs + 3 usage + 2 examples) + web-ui.
  - Hardware: board.json schema, bindings, OTA flow, deployment.
  - 05 Tools: generate_ui, compile_scenario, dump_modr.
  - 06 Contributing: dev setup, host + HIL testing, C++ style, docs style.

- **docs(uk): full spelling cleanup of the Ukrainian pages (47 files):**
  - The previous UK pages were a mishmash like "Driver registers як `sensor` з `hardware_type:` ..." — that's not Ukrainian, it's surzhyk.
  - 9 parallel translator agents rewrote every page per the STYLE.md policy: clean Ukrainian prose, only technical identifiers (`code-format`) remain in English.
  - Standardised glossary: `driver→драйвер`, `scenario→сценарій`, `engine→рушій`, `binding→прив'язка`, `mirror keys→дзеркальні ключі`, `tick→такт`, etc.

- **docs: migration of the scenario-engine deep dive into `documentation/` + cleanup of legacy `docs/`:**
  - 25 deep dive pages (README + 00-10 architectural + 8 ADRs + 3 usage + 2 examples) moved from `docs/` into `documentation/` via `git mv` (history preserved).
  - 12 parallel agents rewrote each page: EN — clean English text (no Ukrainian leftovers), UK — real Ukrainian.
  - The old `docs/` directory removed entirely (13 duplicates + 3 READMEs); `docs/CHANGELOG.md` moved to `documentation/CHANGELOG.md`.
  - Fixed ~25 cross-references to `docs/...` in README, C++ headers, idf_component.yml, tools/, .rules/, and other doc pages.

- **docs(scenario-engine): technical review of the migrated documents:**
  - 3 auditor agents cross-checked 25 pages against the real code in `components/modesp_scenario/`.
  - Discovered ~30+ CRITICAL discrepancies: stale class name `SequenceEngine`, non-existent files `sequence_track.{h,cpp}`/`sequence_instance.{h,cpp}`, `::instance()` singletons that had been killed, `set_state`/`set_nvs_callbacks` setters that no longer exist, `'SQTK'` magic instead of `'SCTK'`, `MAX_SEQUENCES = 4` instead of 2, `MODR_MAX_SIZE = 16 KB` instead of 4 KB, descriptions of `publish_mirror_keys`/`persist_scan` that had been removed from the engine.
  - 6 parallel fixer agents corrected every finding across 22 files (11 EN + 11 UK). Bilingual EN/UK parity preserved.

## 2026-03-09

- **feat(wifi): AP→STA periodic reconnect probe:**
  - In AP mode the ESP32 periodically attempts to connect to the saved WiFi network via WIFI_MODE_APSTA
  - The AP keeps running during the probe — clients do not lose access
  - Backoff: 30s → 60s → 120s → 240s → 300s (cap), infinite retries every 5 min
  - Heap guard 50KB, timeout 15s, fast-fail on STA_DISCONNECTED
  - Guards: WiFi scan and deferred_reconnect cancel the probe
  - STA_START handler does not trigger auto-connect during probing (eliminates "sta is connecting" errors)
  - Solves the problem: ESP32 boots faster than the router → AP mode → auto-reconnect
  - Verified on real hardware: probe #1 (30s) connects successfully, MQTT reconnect works

- **feat(datalogger): logging of all 10 protection alarm types:**
  - Added 8 new EventType (11-18): sensor1/2, continuous_run, pulldown, short_cycle, rapid_cycle, rate_rise, door
  - Previously DataLogger logged only high_temp (5) and low_temp (6) — the remaining 8 alarms were lost
  - Fix: ALARM_CLEAR (7) was never generated for high_temp — prev_ was updated BEFORE the clear check
  - Fix: events_count in on_init() did not include the POWER_ON event
  - i18n labels event.11..18 (UK + EN)
  - 108 host C++ tests, 454 assertions (was 105/370)

- **test(equipment,datalogger):** host unit tests for Equipment (16) and DataLogger (12+3 new alarm tests):
  - MockSensorDriver + MockActuatorDriver for Equipment injection testing
  - Arbitration, anti-short-cycle, interlocks, EMA, has_* keys
  - Alarm edge-detect (all 10 types), alarm clear, simultaneous alarms

- **Architectural analysis:** DataLogger events are hardcoded in 6 places (C++ enum, poll_events, prev_ fields, i18n, ChartWidget). Recorded as ARCH-001 (manifest-driven events) + ARCH-002 (WebUI event labels) in ACTION_PLAN.md. The MVP plan is ready in plans/snug-fluttering-panda.md.

## 2026-03-08

- **Phase 17b:** 2-level escalation of continuous run in the Protection Module:
  - Level 1 (compressor_blocked): forced stop of the compressor, fans keep running
  - Level 2 (lockout): permanent lockout after max_continuous_retries trips, manual reset
  - Equipment Module: arbitration for compressor_blocked and lockout
  - 2 new persist parameters: forced_off_min, max_continuous_retries
  - 4 new state keys: lockout, compressor_blocked, continuous_run_count, forced_off_min, max_continuous_retries
  - 126 state keys, 63 STATE_META, 50 MQTT pub, 62 MQTT sub
  - 9 new host tests (Phase 17b escalation)

- **3 bugfixes Protection Module:**
  - Fix 1: Pulldown matched baseline — evap_at_start vs evap_now (was: air_at_start vs evap_now)
  - Fix 2: Short cycle counter idle reset after 10× min_compressor_run OFF
  - Fix 3: alarm_code includes lockout (highest) and comp_blocked priorities
  - 6 new host test subcases (2 pulldown + 2 short cycle + 3 alarm_code)
  - Total: 63 host C++ tests, 312 assertions, 254 pytest

## 2026-03-07

- **Documentation review R1:** audit by 5 agents, discrepancies discovered and fixed:
  - `docs/07_equipment.md`: full Phase 17 Protection section added (10 monitors, CompressorTracker,
    RateTracker, alarm priority 11 levels, 15 persist params, 4 features)
  - `docs/08_webui.md`: Premium Redesign R1 section added (GroupAccordion, bento-card, System/Network pages),
    fixed bundle size (44KB→76KB)
  - `docs/11_protection.md`: fixed persist params 14→15 (compressor_hours persist)
  - `README.md`: updated metrics STATE_META 53→61, MQTT pub 37→48, MQTT sub 52→60,
    status Phase 14b→Phase 17, Protection description (5→10 alarms), WebUI (44KB→76KB)

- **WebUI Premium Redesign:** full interface rebranding:
  - Dark theme redesign: new CSS tokens, bento-card dashboard, unified color system
  - Card icons: shield (Protection), flame (Defrost), thermometer (Thermostat), database (DataLogger)
  - Unit labels in Compressor Diagnostics card (min, h, °C)
  - Wide card flag: Protection settings + System Info as wide cards
  - Logical widget grouping: same-type widgets in columns
  - Duplicate card removal: Compressor Diagnostics, Alarm Status, defrost.state
  - sensor2 alarm guard: visible_when equipment.has_evap_temp
  - System page: wide status card at top, balanced layout (runtime/firmware info)
  - Network & System pages restructure: card patterns, icons, wide flags
  - Responsive accordions: desktop = open, mobile (< 768px) = collapsed (GroupAccordion)
  - Uptime format: HH:MM:SS instead of seconds
  - bundle.js.gz: ~63KB, bundle.css.gz: ~13KB

## 2026-03-02

- **TASK_17 Phase 1:** Compressor Safety in Protection Module:
  - 5 new alarm monitors: Short Cycle, Rapid Cycle, Continuous Run, Pulldown Failure, Rate-of-Change
  - CompressorTracker: ring buffer 30 starts, sliding 1h window, duty cycle, short_cycle_count
  - RateTracker: EWMA lambda=0.3, instant rate °C/min, rising duration accumulator
  - Motor hours: compressor_hours (float, persist, increment every 5 sec)
  - Diagnostics every 5 sec: starts_1h, duty%, run_time, last_cycle_run/off (track_change=false)
  - 2 features: compressor_protection (requires_roles: [compressor]), rate_protection ([compressor, air_temp])
  - Alarm code priority: 11 levels (err1 > rate_rise > high_temp > pulldown > short_cycle > rapid_cycle > low_temp > continuous_run > err2 > door > none)
  - Defrost interaction: rate blocked during heating phases + post_defrost_delay
  - Pulldown: equipment.evap_temp fallback to air_temp
  - 18 new state keys (5 alarm bools + 5 diagnostics + 1 hours + 7 settings)
  - manifest.json: 19 MQTT pub, 16 MQTT sub (protection module)
  - thermostat/manifest.json: "Compressor monitoring" card (7 settings) + diagnostics in "System status"
  - 10 new test cases (host doctest), 51 C++ tests total
  - Fix: int → int32_t in state_set for ESP32 Xtensa (ambiguous overload)
  - Fix: shared_state_host.cpp + base_module_host.cpp — track_change parameter
  - 122 state keys, 61 STATE_META, 48 MQTT pub, 60 MQTT sub, 254 pytest + 51 host C++

- **Sprint 1 Session 1.1a:** Delta WS Broadcasts + Critical Bugfixes:
  - SharedState: changed_keys_ vector + track_change parameter for all set() overloads
  - WsService: for_each_changed_and_clear() delta broadcast (~200B instead of ~3.5KB)
  - send_full_state_to(fd) for new clients
  - BaseModule: state_set(track_change=false) for timers/diagnostics
  - Modules: thermostat/defrost/system_monitor timers do not trigger WS broadcast
  - Fix DataLogger: removed `if (now < 1700000000) return` guard that blocked the ENTIRE on_update() without NTP
  - Fix DS18B20: removed auto-scan/SKIP_ROM, enforced MATCH_ROM only (critical for safety)
  - Fix Bindings WebUI: OneWireDiscovery shows ALL sensors + unbind button
  - Fix Bindings save: canSave is not blocked by missingRequired, confirm dialog instead of blocking
  - i18n: +5 keys (bind.confirm_missing, bind.confirm_alarm, bind.unbind, bind.no_free_roles, bind.found_total)
  - Bundle: 48.0KB JS gz + 8.1KB CSS gz

## 2026-03-01

- **Sprint 1 Session 1a:** Design Tokens — created `webui/src/styles/tokens.css` (single source
  of truth for design: spacing 4px base, typography 9-64px, border-radius, semantic status colors
  Industrial HMI, alarm/defrost/chart palette, touch targets 44px WCAG, layout sizes, shadows,
  transitions). Import in main.js, MIGRATION.md guide.
- **Sprint 1 Session 1b:** Base Components Refactor:
  Card.svelte — variant prop (default/status/alarm), sessionStorage collapse state, CSS tokens.
  Toast.svelte — bottom-center (mobile-friendly), close button (×), slide-up animation, z-index 10000.
  toast.js — max 3 toasts, error 5→8s, warn 4→5s, exported dismissToast().
  Layout.svelte — connection overlay after 5s WS disconnect (spinner + retry button + toast on reconnect).
  WidgetRenderer.svelte — min-height 44px (touch-min), var(--sp-3) gap.
  i18n: +3 keys (conn.lost, conn.retry, conn.restored).
  Bundle: 47.1KB JS gz + 8.1KB CSS gz (55.2KB total).
- Documentation refactoring: bringing it in line with the actual state of the code.
  Fixed: defrost.req.heater → defrost_relay in the data flow diagram (01_architecture),
  parameters min_off/on_time and startup_delay — minutes instead of seconds (05_cooling_defrost),
  old Danfoss abbreviations (COd→cond_fan_delay, dAd→delayed alarms, dFT/dit/dct/dSS/dSt/dEt→human-readable names),
  added missing HTTP endpoints (/api/wifi/ap, /api/time, /api/factory-reset) in CLAUDE.md and README,
  clarified board.json (currently KC868-A6), generator ~1644 lines.
- Phase 12a DONE: KC868-A6 board support. I2C bus + PCF8574 expander support in HAL.
  pcf8574_relay driver (actuator via I2C), pcf8574_input driver (sensor via I2C).
  board_kc868a6.json (6 relays PCF8574 @0x24, 6 inputs PCF8574 @0x22).
  100% backward compatible with the dev board.
- defrost_relay merger: heater + hg_valve → single defrost_relay role.
  EquipmentModule: defrost_relay_ instead of heater_/hg_valve_. One interlock instead of two.
  Defrost: req.defrost_relay instead of req.heater/req.hg_valve.
  Equipment manifest: defrost_relay role with driver ["relay", "pcf8574_relay"].
- Heap optimization: NVS batch API (batch_open/batch_close — one handle for flush_to_nvs),
  WS broadcast interval 1000→3000ms, float rounding (roundf 0.01°C),
  thermostat publish debounce (effective_setpoint, display_temp),
  heap guard 40KB (skip WS send), system.heap_largest diagnostics.
- Host C++ unit tests: tests/host/ with doctest (90 test cases for thermostat/defrost/protection).
- Documentation refactoring: all docs audited and updated to match actual code state.
  53 STATE_META, 37 MQTT pub, 52 MQTT sub, 6 drivers, 264 pytest + 90 doctest.

## 2026-02-24

- Phase 14b DONE: 6-channel dynamic DataLogger + ChartWidget. TempRecord 12→16 bytes (ch[6] array),
  ChannelDef compile-time table (air/evap/cond/setpoint/humidity/reserved), sync/sample/serialize loops.
  Manifest: +log_setpoint, +log_humidity, "Logging channels" card. ChartWidget: fully dynamic channels
  from API, PALETTE colors, toggle checkboxes, setpoint dual-mode. JSON v3 with dynamic channels.
  i18n: +chart.ch_setpoint, +chart.ch_humidity. 97 state keys, 48 STATE_META, 46 MQTT sub, 264 tests.
- Phase 7b-c DONE: WebUI Polish. Light/Dark theme toggle (stores/theme.js, CSS custom properties,
  localStorage persist, prefers-color-scheme). i18n UA/EN (stores/i18n.js, ~75 keys × 2 languages, derived $t store).
  Animations: page fly/fade transitions, staggered card entrance, card slide collapse, value flash on change.
  19 files updated. Bundle: 37.5→43.7KB gz (within 50KB limit).
- Phase 14a: Multi-channel DataLogger (air+evap+cond), TempRecord 8→12 bytes,
  TEMP_NO_DATA sentinel, JSON v2 with channels header, auto-migration of the old format.
  ChartWidget: multi-line chart (3 polylines), channel toggles, event text list (50 events),
  CSV export (client-side). Equipment: +has_cond_temp. Generator: +cond_temp FEATURE_TO_STATE.
  Fix: Cache-Control no-store (was max-age=86400 causing stale bundle).
  95 state keys, 46 STATE_META, 37 MQTT pub, 44 MQTT sub, 207 tests.
- Phase 14 DONE: DataLogger module (append+rotate LittleFS, streaming chunked JSON,
  10 event types, 6 state keys) + ChartWidget (SVG polyline, min/max downsample, comp/defrost zones,
  tooltip, 24h/48h toggle). GET /api/log, GET /api/log/summary. downsample.js utility.
  Tech debt: TIMER_SATISFIED, Cache-Control, AUDIT-012 separate alarm delays, AUDIT-036 CLOSED.
  92 state keys, 44 STATE_META, 37 MQTT pub, 42 MQTT sub, 207 tests. 5 modules, 9 pages.

## 2026-02-23

- Phase 13a DONE: Runtime UI visibility (visible_when + requires_state). Manifests: constraints
  with disabled_hint, visible_when on defrost/thermostat/protection cards/widgets. Generator: resolve_constraints()
  preserves ALL options + requires_state (FEATURE_TO_STATE mapping), visible_when passthrough, V19 validation.
  Svelte: isVisible() utility, SelectWidget per-option disabled, DynamicPage visible_when. Equipment: +3 has_* keys
  (has_cond_fan, has_door_contact, has_evap_temp). 84 state keys, 178 tests. Runtime: Bindings→Save→Restart→enabled.
- Phase 11b COMPLETE: SEARCH_ROM (Maxim AN187 binary search), GET /api/onewire/scan endpoint
  (scan bus → devices with temperature + assigned status), WebUI OneWire Discovery in BindingsEditor
  (scan button, device list, role assignment). HttpService: set_hal() injection for scan.

## 2026-02-22

- Phase 11b DONE: Multi DS18B20 (MATCH_ROM + CRC8 validation), NTC/ADC driver (B-parameter),
  DigitalInput C++ driver (50ms debounce). HAL: Binding.address, GpioInputConfig, AdcChannelConfig.
  config_service: parse address/gpio_inputs/adc_channels. DriverManager: digital_input + ntc pools.
  Equipment: condenser_temp (NTC/DS18B20) + door_contact (DigitalInput). 5 drivers.
  81 state keys (was 80), 39 STATE_META, 34 MQTT pub, 38 MQTT sub. 206 tests green.

## 2026-02-21

- Phase 11a DONE: Night Setback (4 modes, SNTP schedule, DI, manual),
  Post-defrost alarm suppression (0-120 min timer), Display during defrost (real/frozen/-d-).
  Equipment: night_input role + digital input binding. Thermostat: effective_setpoint, display_temp,
  is_night_active(). Protection: post_defrost_delay, suppress_high flag. Dashboard: display_temp + NIGHT badge.
  80 state keys (was 70), 39 STATE_META, 33 MQTT pub, 38 MQTT sub, 13 menu items. 206 tests green.

## 2026-02-20

- Phase 10.5 DONE: Features System + Select Widgets. Manifests: features/constraints/options
  in thermostat/defrost/protection. Generator: FeatureResolver, select widgets, disabled+reason,
  FeaturesConfigGenerator → features_config.h (5th artifact). C++: has_feature() in BaseModule,
  guards in thermostat/defrost/protection. digital_input driver manifest. board.json: 4 relays, 1 DI, 2 ADC.
  Validation V14-V18. 209 pytest tests green (43 new in test_features.py + 4 binding fixtures).
- BUG-012: NVS positional keys → hash-based (djb2). Auto-migration p0..p32 → sXXXXXXX.
  BUG-023: POST /api/settings uses meta->type instead of decimal point heuristic. Float persist fixed.
  AUDIT-014..017: manifest range fixes. AUDIT-038..040: security (CORS, traversal, old files removed).
- AUDIT Phase 10: 10 critical fixes. C++: relay min_switch_ms role-based (compressor only),
  EM publishes actual relay state via get_state(), EM-level compressor anti-short-cycle timer
  (COMP_MIN_OFF_MS=180s, COMP_MIN_ON_MS=120s), JSON string escaping in http_service + ws_service.
  WebUI: ButtonWidget state key fallback (manual defrost works), icons (flame, shield-alert,
  alert-triangle, thermometer), Dashboard uses equipment.compressor + defrost/alarm tiles,
  StatusText defrost phase colors, alarm banner in Layout, apiPost error handling.

## 2026-02-18

- SharedState capacity 64→96 (MODESP_MAX_STATE_ENTRIES). 69 manifest keys + ~15 system keys overflowed 64.
- Phase 9.4 DONE: Defrost module (modules/defrost/). 7-phase state machine,
  3 types (natural/heater/hot gas), 4 initiations (timer/demand/combo/manual).
  13 persist params + 2 runtime persist (interval_timer, defrost_count). 27 state keys, 10 MQTT publish.
  Generator fix: read-only persist keys are now included in state_meta.h (writable=false, persist=true).
  4 modules, 69 state keys, 9 pages. 79 tests green.
- Phase 9.3 DONE: Protection module (modules/protection/). 5 alarm monitors
  (HAL, LAL, ERR1, ERR2, Door). Delayed alarms (dAd), defrost blocking, auto-clear + manual reset.
  5 persist parameters, 14 state keys, 8 MQTT publish. 79 tests green. 3 modules, 42 state keys.
- Phase 9.2 DONE: Thermostat v2 — full spec_v3 logic. Asymmetric differential,
  state machine (STARTUP→IDLE→COOLING→SAFETY_RUN), evaporator fan (3 FAn modes), condenser fan
  (COd delay), Safety Run, 11 persist parameters, 18 state keys. 79 tests green.
- Phase 9.1 DONE: Equipment Manager (modules/equipment/). Single owner of HAL drivers.
  Arbitration: Protection > Defrost > Thermostat. Interlocks: heater↔compressor, heater↔HG valve.
  Thermostat refactoring: req.compressor instead of direct relay, reads equipment.air_temp.
  generate_ui.py: cross-module widget key resolution (inputs → global state map). 79 tests green.

## 2026-02-17

- Phase 7a DONE: Svelte WebUI (webui/). Svelte 4 + Rollup. 14 widget components,
  Dashboard (tile-based, temp color zones, compressor pulse), Layout (sidebar + bottom tabs),
  DynamicPage (renders any ui.json page). Bundle: 17KB gzipped. Deploy: npm run deploy → data/www/.
- Phase 6.5 DONE: PersistService (CRITICAL, Phase 1). SharedState persist callback (OUTSIDE the mutex).
  state_meta.h: persist+default_val. POST /api/settings: state_meta validation (writable, min/max clamp).
  Thermostat: hardcoded config.setpoint replaced by SharedState read. 79 pytest tests green.
- Inputs validation in generate_ui.py: _validate_inputs() in ManifestValidator, 6 rules from docs/10 §3.2a.
  73 pytest tests green. Thermostat without inputs (the only module) — works as before.
- Phase 6 DONE: MQTT WebUI page added (generate_ui.py + app.js). mqtt.broker in SharedState.
  WiFi PS bug fixed (DS18B20 esp_wifi_set_ps removed). Thermostat: temperature→SharedState,
  settings sync, gauge→value. OTA + MQTT endpoints added to the HTTP API table. Milestone M2 REACHED.
- Driver manifests (ds18b20, relay) + DriverManifestValidator + cross-validation module↔driver.
  board.json: relays→gpio_outputs. C++ HAL updated (BoardConfig.gpio_outputs). Bindings page in WebUI.
  generate_ui.py: ~900 lines, --drivers-dir, 66 tests green.
- Removed HTMLGenerator from generate_ui.py (820→755 lines, 4 artifacts instead of 5). WebUI is now static (data/www/)
- Added documentation rules. WiFi: fixed (STA works, not only AP)

## 2026-02-16

- Created
