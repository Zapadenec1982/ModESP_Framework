# 08 — Життєвий цикл (Build-Time + Runtime)

> 📖 **In English:** [documentation/en/03-framework-reference/scenario-engine/08_lifecycle.md](../../../en/03-framework-reference/scenario-engine/08_lifecycle.md)

Наскрізна історія: авторинг рецепту → компіляція → flash → boot →
виконання → відновлення. Проходить через кожен системний шар, що
торкається рецепту.

## Build-time

```
1. Автор пише маніфест:
   modules/<recipe>/manifest.json
   ├─ "module_type": "recipe"
   ├─ стандартні секції: state, ui, mqtt
   └─ НОВА секція: "scenario" з tracks/phases/transitions

2. CMake pre-build hooks (виконуються перед компіляцією C++):

   tools/generate_ui.py
     ↓ сканує modules/*/manifest.json
     ↓ читає секції "state", "ui", "mqtt", "features"
     ↓ генерує:
       - generated/state_meta.h        (усі задекларовані ключі SharedState)
       - data/ui.json                  (дерево віджетів WebUI)
       - generated/mqtt_topics.h       (теми pub/sub MQTT)
       - generated/module_register.h   (БЕЗ прив'язки для модулів типу recipe)

   tools/compile_scenario.py
     ↓ сканує modules/*/manifest.json, фільтруючи "module_type" == "recipe"
     ↓ для кожного: валідує секцію "scenario", хеш-резолвить дії,
       генерує бінарник
     ↓ data/scenarios/<recipe>.modr   (типовий розмір файлу 100 Б — 16 КБ)

3. Збірка ESP-IDF:
   - C++ компонент modesp_scenario збирає код engine
   - Образ розділу LittleFS пакує data/* (включно з data/scenarios/*.modr)
   - Фінальна прошивка: ELF + образ LFS
```

## Runtime — послідовність завантаження

```
вмикання живлення
  │
  ▼
ініціалізація NVS (завжди)
  │
  ▼
ConfigService читає board.json + bindings.json
  │
  ▼
HAL ініціалізує GPIO з BoardConfig
  │
  ▼
DriverManager створює драйвери
  │
  ▼
Phase 1 ініціалізація модулів: ErrorService, LoggerService, ConfigService, ...
  │
  ▼
Phase 2 підготовка модулів:
  - sequence_engine.set_state(&app.state())
  - modesp::scenario::builtins::register_builtins()      ← вбудовані дії/умови
  - sequence_engine.set_nvs_callbacks(write, read, ...)  ← підключення персистентності
  - app.modules().register_module(sequence_engine)
  - modesp_register_modules(app)                          ← бізнес-модулі, включно з
                                                            тими, що можуть реєструвати
                                                            власні дії
  │
  ▼
Phase 2 init_all:
  - викликається sequence_engine.on_init() → arbiter.clear_for_tests, скидання слотів
  - init бізнес-модулів → вони можуть викликати ActionRegistry::register_action
    у своєму on_init
  │
  ▼
Phase 3 модулі: HTTP, WebSocket, MQTT
  │
  ▼
Головний цикл @ 100 Hz:
  для кожного модуля: on_update(dt_ms)
    └─ sequence_engine.on_update(10):
         ├─ instance_tick(...) для кожного запущеного екземпляра
         ├─ publish_mirror_keys(...) для кожного завантаженого слоту
         └─ persist_scan(...) для кожного завантаженого слоту
```

## Runtime — завантаження і запуск рецепту

Бізнес-модуль ініціює рецепт (зазвичай за дією користувача або системною
подією):

```cpp
// 1. Завантажити
SequenceHandle h = engine.load_path("/data/scenarios/abs_test.modr");
if (h == 0) {
    ESP_LOGE(...); return;  // перевірте engine.last_error()
}

// 2. (Необов'язково) Відновлення з персистованого стану
if (engine.try_recover(h) == EngineError::OK) {
    // Слот тепер у стані PAUSED з відновленими phase_idx + elapsed_ms.
    // Користувач вирішує через кнопку WebUI: resume() або abort().
    // У цій точці рецепт НЕ виконується — необхідне ручне втручання.
} else {
    // Немає персистованого стану АБО відновлення не вдалося — старт з нуля
    EngineError err = engine.start(h);
    if (err != EngineError::OK) {
        // RESOURCE_CONTENDED або внутрішня помилка
        engine.unload(h);
        return;
    }
}

// 3. on_update engine тактує сценарій кожні 10 мс; зрештою:
//    - state(h) → COMPLETED → користувач бачить результат у WebUI
//    - state(h) → FAILED → перевірте стани доріжок, щоб дізнатися, яка провалилась
//    - state(h) лишається у RUNNING нескінченно, якщо рецепт не має термінального шляху
//      (ймовірно баг — запобіжник scenario_timeout_max_ms)
```

## Приклад еволюції стану такт за тактом

Для мінімального дво-фазного рецепту з переходом на основі часу:

```
Tick 0: load_path() → state = LOADED
        engine.start() → state = RUNNING
                       → arbiter.acquire_scenario() (тут немає ресурсів — no-op)
                       → instance_start() — track 0 → RUNNING, phase_idx = 0

Tick 1: on_update(10):
        - instance_tick:
          - track_tick(0): phase_elapsed_ms = 10
                            запускаються entry-дії (по одній на такт)
        - publish_mirror_keys: пише "<recipe>.scenario_state" = "running"
        - persist_scan: стан змінився (LOADED→RUNNING) → спрацьовує write callback

Tick 2..10: доріжка 0 виконує entry-дії, зрештою оцінює переходи.

Tick 11: спрацьовує умова time_elapsed_ms (ms=100):
         - latch target = phase 1; running_exit_actions = true

Tick 12+: виконуються exit-дії (phase 0); застосовується перехід →
         - track.phase_idx = 1, entry_action_progress = 0
         - починаються entry-дії phase 1

...

Tick N: спрацьовує перехід phase 1 → $complete:
        - track 0 → COMPLETED
        - completion_rule (all_tracks_complete) виконано → сценарій → COMPLETED
        - arbiter.release_scenario() (ресурсів тут немає)
        - publish_mirror_keys: "scenario_state" = "completed"
        - persist_scan: стан змінився (RUNNING→COMPLETED) → спрацьовує write callback
```

## Сценарій збою

Втрата живлення посеред виконання. Стан у NVS:

```
До втрати живлення (на момент останньої персистентності):
  NVS["seqstate"]["t0"] = серіалізований seq_token з:
    - magic = 'SQTK'
    - scenario_id = djb2("abs_test")
    - scenario_state = RUNNING
    - tracks[0].phase_idx = 1
    - tracks[0].phase_elapsed_ms = 5000
    - ... CRC

Живлення відновлено. Послідовність завантаження проходить нормально до Phase 2 init_all.
```

`on_init` бізнес-модуля (або виділений сервіс відновлення) викликає:

```cpp
auto h = engine.load_path("/data/scenarios/abs_test.modr");
if (h == 0) return;

EngineError err = engine.try_recover(h);
if (err == EngineError::OK) {
    // engine.state(h) == PAUSED
    // engine.track_phase_idx(h, 0) == 1
    // engine.track_phase_elapsed_ms(h, 0) == 5000
    // Стан апаратури невідомий (під час втрати живлення була phase 1 —
    // нагрівач міг бути посеред циклу). Engine не перезапускає апаратуру.
    // Користувач вирішує:
    //   - resume(h): продовжити з місця зупинки (phase 1, 5 с минуло)
    //   - abort(h): примусово FAILED; обробник abort рецепту закриває апаратуру
}
```

Банер WebUI (Stage 1.5):
> Scenario "abs_test" recovered to PAUSED у phase 1. Hardware may не reflect
> recipe state. **Resume**, **Abort**, or **Unload**?

## Очищення і unload

```
Зрештою:
  engine.unload(h) → 
    - arbiter.release_scenario(h)
    - arbiter.release_phase(h, t) для кожної доріжки
    - скидання слоту (buffer_size = 0); state → IDLE
    - last_persisted_* зберігається у NVS (слот t<idx> лишається)

Якщо оновлення прошивки змінює рецепт з тією самою назвою:
  - На наступному try_recover: scenario_id токена збігається з id нового
    рецепту (djb2("abs_test") той самий), І track_count + phase_count
    також мають збігатися, АБО deserialize_token повертає INVALID_FILE
  - На розбіжності схеми: викликач трактує як відсутність даних,
    викликає start() для свіжого старту
  - Слот NVS зрештою перезаписується на наступній персистентності
```

## Дивіться також

- [00_overview.md](00_overview.md) — що + навіщо
- [01_architecture.md](01_architecture.md) — діаграма компонентів
- [04_state_machines.md](04_state_machines.md) — таблиці переходів станів
- [07_persistence.md](07_persistence.md) — деталі персистентності + відновлення
- [09_manifest_integration.md](09_manifest_integration.md) — деталі конвеєра маніфесту
- [usage/01_quickstart.md](usage/01_quickstart.md) — робочий стартовий приклад
