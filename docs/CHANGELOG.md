# ModESP v4 — Changelog

> Повний changelog проекту.

## 2026-03-16

- **feat(i18n): мультимовний інтерфейс (UK/EN/DE/PL):**
  - Архітектура: окремі language packs в LittleFS (`data/www/i18n/{lang}.json`)
  - Per-module translation files: `modules/*/i18n/{en,de,pl}.json`
  - Генератор збирає module + system translations → merged language pack (674 keys)
  - Frontend: lazy-load мовного пакету при зміні мови (`fetch('/i18n/{lang}.json')`)
  - Українська — default (вбудована в ui.json, без fetch)
  - `cycleLanguage()` — циклічне перемикання UK → EN → DE → PL
  - Видалено `uiEn.js` (327 entries) — замінено на structured keys + reverse map
  - Додати нову мову = translation files, без зміни коду
  - Професійна термінологія ХО: DE (Verdichter, Abtauung), PL (sprężarka, odszranianie)

- **feat(aws): AWS IoT Core integration (feature/aws-iot branch):**
  - Compile-time switch через Kconfig (MQTT default, AWS optional)
  - mTLS connection, telemetry delta-publish, commands via MQTT
  - Device Shadow (62 reported keys + delta apply)
  - IoT Jobs OTA (download → flash → reboot → validate)
  - NVS 32KB для cert+key storage, JSON unescape для PEM
  - WiFi deferred start (crash fix), JSMN_STATIC (linker fix)
  - Verified on real ESP32: mTLS + telemetry + Shadow + OTA Jobs
  - 15 commits on feature/aws-iot, merged to main

- **docs: documentation overhaul for portfolio:**
  - docs/FEATURES.md (EN) + FEATURES_UA.md (UA) — 13-section feature overview
  - docs/CLOUD_INTEGRATION.md — ModESP Cloud integration guide
  - docs/12_aws_iot.md — AWS IoT Core повна документація
  - README.md redesigned: key metrics, Technical Highlights for Reviewers
  - .rules/ portable core: 9 rule files для Claude Code

- **fix(bindings): відображення всіх unassigned ролей в "Додати обладнання"**
  - Required ролі (air_temp) тепер з'являються після видалення
  - Очищено ROM адреси DS18B20 з factory bindings.json

- **fix(thermostat): діапазон уставки -50..+50 → -30..+20°C**
- **fix(ui): перейменування "Холодильна камера" → "Охолодження" (Cooling)**
- **fix(mqtt): збереження prefix в NVS при _set_tenant (pending after reboot)**
- **License: Source Available (PolyForm Noncommercial 1.0.0)**

## 2026-03-09

- **feat(wifi): AP→STA periodic reconnect probe:**
  - В AP mode ESP32 періодично пробує підключитися до збереженої WiFi мережі через WIFI_MODE_APSTA
  - AP продовжує працювати під час проби — клієнти не втрачають доступ
  - Backoff: 30s → 60s → 120s → 240s → 300s (cap), нескінченні спроби кожні 5 хв
  - Heap guard 50KB, timeout 15s, fast-fail при STA_DISCONNECTED
  - Guards: WiFi scan та deferred_reconnect скасовують probe
  - STA_START handler не викликає auto-connect при probing (усунуто "sta is connecting" помилки)
  - Вирішує проблему: ESP32 завантажується швидше за роутер → AP mode → авто-reconnect
  - Перевірено на реальному залізі: probe #1 (30s) успішно підключається, MQTT reconnect працює

- **feat(datalogger): логування всіх 10 типів аварій захисту:**
  - Додано 8 нових EventType (11-18): sensor1/2, continuous_run, pulldown, short_cycle, rapid_cycle, rate_rise, door
  - Раніше DataLogger логував тільки high_temp (5) і low_temp (6) — решта 8 аварій губилась
  - Fix: ALARM_CLEAR (7) ніколи не генерувався для high_temp — prev_ оновлювався ДО clear check
  - Fix: events_count в on_init() не включав POWER_ON подію
  - i18n мітки event.11..18 (UK + EN)
  - 108 host C++ tests, 454 assertions (було 105/370)

- **test(equipment,datalogger):** host unit tests для Equipment (16) та DataLogger (12+3 нових alarm tests):
  - MockSensorDriver + MockActuatorDriver для Equipment injection testing
  - Arbitration, anti-short-cycle, interlocks, EMA, has_* keys
  - Alarm edge-detect (all 10 types), alarm clear, simultaneous alarms

- **Архітектурний аналіз:** DataLogger events захардкоджені в 6 місцях (C++ enum, poll_events, prev_ поля, i18n, ChartWidget). Зафіксовано як ARCH-001 (manifest-driven events) + ARCH-002 (WebUI event labels) в ACTION_PLAN.md. MVP план готовий в plans/snug-fluttering-panda.md.

## 2026-03-08

- **Phase 17b:** 2-рівнева ескалація continuous run в Protection Module:
  - Level 1 (compressor_blocked): примусова зупинка компресора, вентилятори продовжують працювати
  - Level 2 (lockout): перманентна блокіровка після max_continuous_retries спрацювань, manual reset
  - Equipment Module: arbitration для compressor_blocked та lockout
  - 2 нових persist параметри: forced_off_min, max_continuous_retries
  - 4 нових state keys: lockout, compressor_blocked, continuous_run_count, forced_off_min, max_continuous_retries
  - 126 state keys, 63 STATE_META, 50 MQTT pub, 62 MQTT sub
  - 9 нових host tests (Phase 17b escalation)

- **3 bugfixes Protection Module:**
  - Fix 1: Pulldown matched baseline — evap_at_start vs evap_now (було: air_at_start vs evap_now)
  - Fix 2: Short cycle counter idle reset після 10× min_compressor_run OFF
  - Fix 3: alarm_code включає lockout (найвищий) та comp_blocked пріоритети
  - 6 нових host test subcases (2 pulldown + 2 short cycle + 3 alarm_code)
  - Всього: 63 host C++ tests, 312 assertions, 254 pytest

## 2026-03-07

- **Документаційна ревізія R1:** аудит 5 агентів, виявлені та виправлені невідповідності:
  - `docs/07_equipment.md`: додана повна секція Phase 17 Protection (10 моніторів, CompressorTracker,
    RateTracker, alarm priority 11 рівнів, 15 persist params, 4 features)
  - `docs/08_webui.md`: додана секція Premium Redesign R1 (GroupAccordion, bento-card, System/Network pages),
    виправлений bundle size (44KB→76KB)
  - `docs/11_protection.md`: виправлено persist params 14→15 (compressor_hours persist)
  - `README.md`: оновлені метрики STATE_META 53→61, MQTT pub 37→48, MQTT sub 52→60,
    статус Phase 14b→Phase 17, опис Protection (5→10 alarms), WebUI (44KB→76KB)

- **WebUI Premium Redesign:** повний ребрендинг інтерфейсу:
  - Dark theme redesign: нові CSS токени, bento-card dashboard, unified color system
  - Card icons: shield (Protection), flame (Defrost), thermometer (Thermostat), database (DataLogger)
  - Unit labels у Compressor Diagnostics card (хв, год, °C)
  - Wide card flag: Налаштування захисту + System Info як wide cards
  - Logical widget grouping: same-type widgets in columns
  - Duplicate card removal: Compressor Diagnostics, Alarm Status, defrost.state
  - sensor2 alarm guard: visible_when equipment.has_evap_temp
  - System page: wide status card at top, balanced layout (runtime/firmware info)
  - Network & System pages restructure: card patterns, icons, wide flags
  - Responsive accordions: desktop = open, mobile (< 768px) = collapsed (GroupAccordion)
  - Uptime format: HH:MM:SS замість секунд
  - bundle.js.gz: ~63KB, bundle.css.gz: ~13KB

## 2026-03-02

- **TASK_17 Phase 1:** Compressor Safety in Protection Module:
  - 5 нових моніторів аварій: Short Cycle, Rapid Cycle, Continuous Run, Pulldown Failure, Rate-of-Change
  - CompressorTracker: ring buffer 30 starts, sliding 1h window, duty cycle, short_cycle_count
  - RateTracker: EWMA lambda=0.3, instant rate °C/min, rising duration accumulator
  - Motor hours: compressor_hours (float, persist, інкремент кожні 5 сек)
  - Діагностика кожні 5 сек: starts_1h, duty%, run_time, last_cycle_run/off (track_change=false)
  - 2 features: compressor_protection (requires_roles: [compressor]), rate_protection ([compressor, air_temp])
  - Alarm code priority: 11 рівнів (err1 > rate_rise > high_temp > pulldown > short_cycle > rapid_cycle > low_temp > continuous_run > err2 > door > none)
  - Defrost interaction: rate blocked під час heating-фаз + post_defrost_delay
  - Pulldown: equipment.evap_temp fallback на air_temp
  - 18 нових state keys (5 alarm bools + 5 diagnostics + 1 hours + 7 settings)
  - manifest.json: 19 MQTT pub, 16 MQTT sub (protection module)
  - thermostat/manifest.json: "Моніторинг компресора" card (7 settings) + diagnostics в "Стан системи"
  - 10 нових test cases (host doctest), всього 51 C++ тестів
  - Fix: int → int32_t в state_set для ESP32 Xtensa (ambiguous overload)
  - Fix: shared_state_host.cpp + base_module_host.cpp — track_change параметр
  - 122 state keys, 61 STATE_META, 48 MQTT pub, 60 MQTT sub, 254 pytest + 51 host C++

- **Sprint 1 Session 1.1a:** Delta WS Broadcasts + Critical Bugfixes:
  - SharedState: changed_keys_ вектор + track_change параметр для всіх set() overloads
  - WsService: for_each_changed_and_clear() delta broadcast (~200B замість ~3.5KB)
  - send_full_state_to(fd) для нових клієнтів
  - BaseModule: state_set(track_change=false) для таймерів/діагностики
  - Modules: thermostat/defrost/system_monitor timers не тригерять WS broadcast
  - Fix DataLogger: прибрано `if (now < 1700000000) return` guard що блокував ВСЮ on_update() без NTP
  - Fix DS18B20: прибрано auto-scan/SKIP_ROM, enforced MATCH_ROM only (критично для безпеки)
  - Fix Bindings WebUI: OneWireDiscovery показує ВСІ датчики + unbind кнопка
  - Fix Bindings save: canSave не блокується по missingRequired, confirm діалог замість блокування
  - i18n: +5 ключів (bind.confirm_missing, bind.confirm_alarm, bind.unbind, bind.no_free_roles, bind.found_total)
  - Bundle: 48.0KB JS gz + 8.1KB CSS gz

## 2026-03-01

- **Sprint 1 Session 1a:** Design Tokens — створено `webui/src/styles/tokens.css` (єдине джерело
  правди для дизайну: spacing 4px base, typography 9-64px, border-radius, semantic status colors
  Industrial HMI, alarm/defrost/chart palette, touch targets 44px WCAG, layout sizes, shadows,
  transitions). Import в main.js, MIGRATION.md guide.
- **Sprint 1 Session 1b:** Base Components Refactor:
  Card.svelte — variant prop (default/status/alarm), sessionStorage collapse state, CSS tokens.
  Toast.svelte — bottom-center (mobile-friendly), close button (×), slide-up animation, z-index 10000.
  toast.js — max 3 тости, error 5→8s, warn 4→5s, exported dismissToast().
  Layout.svelte — connection overlay через 5с WS disconnect (spinner + retry button + toast on reconnect).
  WidgetRenderer.svelte — min-height 44px (touch-min), var(--sp-3) gap.
  i18n: +3 ключі (conn.lost, conn.retry, conn.restored).
  Bundle: 47.1KB JS gz + 8.1KB CSS gz (55.2KB total).
- Рефакторинг документації: приведення до реального стану коду.
  Виправлено: defrost.req.heater → defrost_relay в data flow діаграмі (01_architecture),
  параметри min_off/on_time і startup_delay — хвилини замість секунд (05_cooling_defrost),
  старі абревіатури Danfoss (COd→cond_fan_delay, dAd→delayed alarms, dFT/dit/dct/dSS/dSt/dEt→людські назви),
  додано відсутні HTTP endpoints (/api/wifi/ap, /api/time, /api/factory-reset) в CLAUDE.md і README,
  уточнено board.json (зараз KC868-A6), генератор ~1644 рядків.
- Phase 12a DONE: KC868-A6 board support. I2C bus + PCF8574 expander підтримка в HAL.
  pcf8574_relay driver (actuator через I2C), pcf8574_input driver (sensor через I2C).
  board_kc868a6.json (6 реле PCF8574 @0x24, 6 входів PCF8574 @0x22).
  100% backward compatible з dev board.
- defrost_relay merger: heater + hg_valve → єдиний defrost_relay role.
  EquipmentModule: defrost_relay_ замість heater_/hg_valve_. Один інтерлок замість двох.
  Defrost: req.defrost_relay замість req.heater/req.hg_valve.
  Equipment manifest: defrost_relay role з driver ["relay", "pcf8574_relay"].
- Heap optimization: NVS batch API (batch_open/batch_close — один handle для flush_to_nvs),
  WS broadcast interval 1000→3000ms, float rounding (roundf 0.01°C),
  thermostat publish debounce (effective_setpoint, display_temp),
  heap guard 40KB (skip WS send), system.heap_largest diagnostics.
- Host C++ unit tests: tests/host/ з doctest (90 test cases для thermostat/defrost/protection).
- Documentation refactoring: all docs audited and updated to match actual code state.
  53 STATE_META, 37 MQTT pub, 52 MQTT sub, 6 drivers, 264 pytest + 90 doctest.

## 2026-02-24

- Phase 14b DONE: 6-channel dynamic DataLogger + ChartWidget. TempRecord 12→16 bytes (ch[6] array),
  ChannelDef compile-time table (air/evap/cond/setpoint/humidity/reserved), sync/sample/serialize loops.
  Manifest: +log_setpoint, +log_humidity, "Канали логування" card. ChartWidget: fully dynamic channels
  from API, PALETTE colors, toggle checkboxes, setpoint dual-mode. JSON v3 with dynamic channels.
  i18n: +chart.ch_setpoint, +chart.ch_humidity. 97 state keys, 48 STATE_META, 46 MQTT sub, 264 тестів.
- Phase 7b-c DONE: WebUI Polish. Light/Dark theme toggle (stores/theme.js, CSS custom properties,
  localStorage persist, prefers-color-scheme). i18n UA/EN (stores/i18n.js, ~75 keys × 2 languages, derived $t store).
  Animations: page fly/fade transitions, staggered card entrance, card slide collapse, value flash on change.
  19 files updated. Bundle: 37.5→43.7KB gz (within 50KB limit).
- Phase 14a: Multi-channel DataLogger (air+evap+cond), TempRecord 8→12 bytes,
  TEMP_NO_DATA sentinel, JSON v2 з channels header, auto-migration old format.
  ChartWidget: multi-line chart (3 polylines), channel toggles, event text list (50 events),
  CSV export (client-side). Equipment: +has_cond_temp. Generator: +cond_temp FEATURE_TO_STATE.
  Fix: Cache-Control no-store (was max-age=86400 causing stale bundle).
  95 state keys, 46 STATE_META, 37 MQTT pub, 44 MQTT sub, 207 тестів.
- Phase 14 DONE: DataLogger module (append+rotate LittleFS, streaming chunked JSON,
  10 event types, 6 state keys) + ChartWidget (SVG polyline, min/max downsample, comp/defrost zones,
  tooltip, 24h/48h toggle). GET /api/log, GET /api/log/summary. downsample.js utility.
  Tech debt: TIMER_SATISFIED, Cache-Control, AUDIT-012 separate alarm delays, AUDIT-036 CLOSED.
  92 state keys, 44 STATE_META, 37 MQTT pub, 42 MQTT sub, 207 тестів. 5 modules, 9 pages.

## 2026-02-23

- Phase 13a DONE: Runtime UI visibility (visible_when + requires_state). Manifests: constraints
  з disabled_hint, visible_when на defrost/thermostat/protection cards/widgets. Generator: resolve_constraints()
  зберігає ВСІ options + requires_state (FEATURE_TO_STATE mapping), visible_when passthrough, V19 validation.
  Svelte: isVisible() utility, SelectWidget per-option disabled, DynamicPage visible_when. Equipment: +3 has_* keys
  (has_cond_fan, has_door_contact, has_evap_temp). 84 state keys, 178 тестів. Runtime: Bindings→Save→Restart→enabled.
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
  Generator fix: read-only persist keys now included in state_meta.h (writable=false, persist=true).
  4 modules, 69 state keys, 9 pages. 79 тестів зелені.
- Phase 9.3 DONE: Protection module (modules/protection/). 5 alarm monitors
  (HAL, LAL, ERR1, ERR2, Door). Delayed alarms (dAd), defrost blocking, auto-clear + manual reset.
  5 persist параметрів, 14 state keys, 8 MQTT publish. 79 тестів зелені. 3 modules, 42 state keys.
- Phase 9.2 DONE: Thermostat v2 — повна логіка spec_v3. Асиметричний диференціал,
  state machine (STARTUP→IDLE→COOLING→SAFETY_RUN), вент. випарника (3 режими FAn), вент. конденсатора
  (затримка COd), Safety Run, 11 persist параметрів, 18 state keys. 79 тестів зелені.
- Phase 9.1 DONE: Equipment Manager (modules/equipment/). Єдиний власник HAL drivers.
  Арбітраж: Protection > Defrost > Thermostat. Інтерлоки: тен↔компресор, тен↔клапан ГГ.
  Thermostat рефакторинг: req.compressor замість direct relay, читає equipment.air_temp.
  generate_ui.py: cross-module widget key resolution (inputs → global state map). 79 тестів зелені.

## 2026-02-17

- Phase 7a DONE: Svelte WebUI (webui/). Svelte 4 + Rollup. 14 widget components,
  Dashboard (tile-based, temp color zones, compressor pulse), Layout (sidebar + bottom tabs),
  DynamicPage (renders any ui.json page). Bundle: 17KB gzipped. Deploy: npm run deploy → data/www/.
- Phase 6.5 DONE: PersistService (CRITICAL, Phase 1). SharedState persist callback (ПОЗА mutex).
  state_meta.h: persist+default_val. POST /api/settings: state_meta валідація (writable, min/max clamp).
  Thermostat: hardcoded config.setpoint замінено на SharedState read. 79 pytest тестів зелені.
- Inputs validation в generate_ui.py: _validate_inputs() в ManifestValidator, 6 правил з docs/10 §3.2a.
  73 pytest тести зелені. Thermostat без inputs (єдиний модуль) — працює як раніше.
- Phase 6 DONE: MQTT WebUI page додана (generate_ui.py + app.js). mqtt.broker в SharedState.
  WiFi PS bug виправлений (DS18B20 esp_wifi_set_ps видалений). Thermostat: temperature→SharedState,
  settings sync, gauge→value. OTA + MQTT endpoints додані в HTTP API таблицю. Milestone M2 ДОСЯГНУТО.
- Driver manifests (ds18b20, relay) + DriverManifestValidator + cross-валідація module↔driver.
  board.json: relays→gpio_outputs. C++ HAL оновлений (BoardConfig.gpio_outputs). Bindings page в WebUI.
  generate_ui.py: ~900 рядків, --drivers-dir, 66 тестів зелені.
- Видалено HTMLGenerator з generate_ui.py (820→755 рядків, 4 артефакти замість 5). WebUI тепер статичний (data/www/)
- Додано правила документування. WiFi: виправлено (STA працює, не тільки AP)

## 2026-02-16

- Створено
