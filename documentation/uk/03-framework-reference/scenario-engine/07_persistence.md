# 07 — Персистентність у NVS

> 📖 **In English:** [documentation/en/03-framework-reference/scenario-engine/07_persistence.md](../../../en/03-framework-reference/scenario-engine/07_persistence.md)

Відновлення після перезавантаження живлення через 96-байтові токени,
що зберігаються у ESP-IDF NVS. Engine емітить крайові події життєвого
циклу сценарію через хуки `IEngineObserver`; `NvsObserver` слухає ці
краї, застосовує політику дроселювання у власному стані на слот і
викликає колбек, наданий викликачем, для запису токена. На завантаженні
рецепт перечитується з файлової системи; викликач звертається до
`engine.try_recover(handle, nvs_observer)`, який читає токен ТА
відновлює позицію фази, перш ніж сценарій знову увійде у стан PAUSED.

## Розкладка сховища

| Простір імен NVS | Формат ключа | Значення |
|---|---|---|
| `scnstate` | `t<slot>` (наприклад, `t0`, `t1`, ..., `t<MAX_SEQUENCES-1>`) | 96-байтовий blob `seq_token` |

`MAX_SEQUENCES = 2` за замовчуванням (Kconfig `CONFIG_MODESP_MAX_SEQUENCES`,
максимум 8) → ключі `t0`..`t1`. Ключі на екземпляр дозволяють незалежне
відновлення кількох паралельних сценаріїв.

## Формат токена (`seq_token`, 96 байтів)

Визначений у `nvs_token.h`. Розкладка відповідає плану Q7. POD без
доповнення за межами явних полів:

```c
struct seq_token {                       // загалом 96 байтів
    uint32_t magic;                      // [0..3]   = 'SCTK' (0x4B544353 LE)
    uint16_t version;                    // [4..5]   = SEQ_TOKEN_VERSION (1)
    uint16_t scenario_id;                // [6..7]   djb2(module_name) low16
    uint8_t  scenario_state;             // [8]      SequenceRuntime::State
    uint8_t  track_count;                // [9]
    uint16_t resource_owner_mask;        // [10..11] зарезервовано (Stage 1.5)
    uint32_t scenario_elapsed_ms;        // [12..15]
    uint32_t wall_clock_started_at;      // [16..19] unix-епоха на start, 0 якщо немає SNTP
    struct {                             // [20..67] 6 доріжок × 8 байтів
        uint8_t  state;                  //          TrackRuntime::State
        uint8_t  phase_idx;
        uint16_t reserved;
        uint32_t phase_elapsed_ms;
    } tracks[6];
    uint8_t  cont_state[16];             // [68..83] зарезервовано для ContinuousBehaviors (Stage 2)
    uint32_t reserved_a;                 // [84..87]
    uint32_t reserved_b;                 // [88..91]
    uint16_t crc16;                      // [92..93] CRC-CCITT від [0..91]
    uint16_t reserved_c;                 // [94..95]
};
```

CRC-CCITT (варіант XMODEM: poly 0x1021, init 0x0000, без reflection).
Обчислюється над байтами [0..91] включно; трейлер у [92..93].

## Політика запису (за планом Q7)

Engine більше не сканує зміни стану всередині `on_update`. Натомість
він синхронно емітить крайові події через хуки `IEngineObserver`
(`on_scenario_started`, `on_phase_entered`, `on_scenario_terminal`)
з шляху такту. `NvsObserver` реалізує ці хуки і застосовує таку
політику:

| Подія | Час персистентності | Обґрунтування |
|-------|---------------|-----------|
| `on_scenario_started` (LOADED→RUNNING) | **Негайно** | Критична для збоїв подія має пережити |
| `on_scenario_terminal` (COMPLETED, FAILED, включно з ABORTING→FAILED) | **Негайно** | Фінальний стан має бути зафіксований |
| `on_phase_entered` для головної доріжки (`MODR_TRACK_FLAG_MAIN`) | **Негайно** | Інваріанти головної доріжки збережено |
| `on_phase_entered` для бічної доріжки | Дроселюється до ≥1 с між записами | Захист від зношування flash |
| 5-хвилинна контрольна точка (Stage 1.5) | Періодично | Гарантує точність resume, якщо активна лише бічна доріжка |

Стан дроселювання на слот живе **у `NvsObserver`**, а не у `Slot`
engine. Observer тримає насичуючий лічильник `time_since_persist_ms_[]`
на індекс слоту (просувається з `on_tick`) і використовує його
всередині `throttle_check()`, щоб гейтити неургентні записи. Engine
не відстежує «востаннє записане» — він лише емітить краї; observer
вирішує, що і коли персистувати.

### Розрахунок ресурсу зношування

ESP-IDF NVS = wear-leveled flash. Найгірша швидкість запису:
- Дистиляційний рецепт із 6 доріжками зі змінами фаз кожні ~30 с
- Бічні доріжки дроселюються до 1 запис/с → ≤ 5 записів/с сукупно
- 5 × 60 × 60 × 24 = 432 тис. записів/день теоретичний максимум

Реалістичні рецепти набагато розрідженіші:
- 8-годинний кулінарний рецепт із ~50 змінами фаз загалом → ~50 записів на запуск
- 5 запусків/день → 250 записів/день на слот

NVS розрахований на 100 тис. циклів/сектор. При 250 записів/день →
400 днів безперервної роботи на сектор. Wear-leveling розподіляє запис
між декількома секторами простору імен. Реалістичний термін у полі: багато років.

Якщо швидкість запису стає проблемою, збільшіть інтервал дроселювання
або додайте явну логіку контрольних точок у рецепті (наприклад,
персистентність лише на головних межах фаз).

## Контракт колбеку

`NvsObserver` не викликає `nvs_set_blob` напряму — натомість викликає
колбеки, передані до його **конструктора**. Це утримує код observer
незалежним від цільової платформи (хост-тести надають in-memory моки).
Сам engine не має NVS-хуків: він приймає observer через параметр
конструктора `etl::span<IEngineObserver*>` і емітить крайові події;
observer володіє колбеками.

```cpp
using NvsWriteFn = bool (*)(void* user, uint8_t slot,
                            const uint8_t* token, size_t len);
using NvsReadFn  = bool (*)(void* user, uint8_t slot,
                            uint8_t* token_buf, size_t* in_out_len);

// Конструюється з колбеками; інжектується у engine через observer span.
NvsObserver nvs_obs{write_fn, read_fn, user_ctx};
IEngineObserver* obs_list[] = {&nvs_obs};
Engine engine{state_backend, actions, continuous, obs_list};
nvs_obs.bind_engine(engine);  // обов'язково до engine.start()
```

Підключення на цільовій платформі у `main.cpp`:

```cpp
static auto seq_nvs_write = [](void*, uint8_t slot,
                                const uint8_t* token, size_t len) -> bool {
    char key[8];
    std::snprintf(key, sizeof(key), "t%u", static_cast<unsigned>(slot));
    return modesp::nvs_helper::write_blob("scnstate", key, token, len);
};

static auto seq_nvs_read = [](void*, uint8_t slot,
                               uint8_t* buf, size_t* in_out_len) -> bool {
    char key[8];
    std::snprintf(key, sizeof(key), "t%u", static_cast<unsigned>(slot));
    size_t out_len = 0;
    bool ok = modesp::nvs_helper::read_blob("scnstate", key, buf,
                                             *in_out_len, out_len);
    if (ok) *in_out_len = out_len;
    return ok;
};

static modesp::scenario::NvsObserver nvs_obs{seq_nvs_write, seq_nvs_read, nullptr};
```

Колбеки викликаються лише із задачі оновлення engine (синхронно
всередині події observer) — викликачеві не потрібна синхронізація
поверх тієї, яку надає сам NVS.

### Обробка збоїв колбеку

`write_fn` повертає `false`:
- Observer логує попередження (Stage 1.5 — наразі тихий фолбек)
- Лічильник дроселювання observer на слот НЕ скидається → наступна
  крайова подія повторить
- Каскадний персистентний збій може блокувати інші операції; рецепти,
  що покладаються на персистентність задля безпеки, мають моніторити
  стан бекенду

`read_fn` повертає `false`:
- `engine.try_recover(h, nvs_obs)` повертає `EngineError::NVS_ERROR`
- Викликач вирішує, чи робити abort сценарію, чи стартувати з нуля

## Потік відновлення

На завантаженні прошивки після reset:

```cpp
// Крок 1: Перезавантажити рецепт (бінарник рецепту живе у LittleFS і
// зберігається між перезавантаженнями незалежно від NVS)
auto handle = engine.load_path("/data/scenarios/abs_test.modr");
if (handle == 0) { /* рецепт відсутній */ return; }

// Крок 2: Спроба відновлення (observer передається явно — engine не
// робить ID-каст observers; recovery — це round-trip операція читання,
// що не вписується в модель fire-and-forget подій)
EngineError err = engine.try_recover(handle, nvs_obs);
if (err == EngineError::OK) {
    // Слот тепер у стані PAUSED з відновленими phase_idx + phase_elapsed_ms.
    // Сценарій НЕ авто-резюмується. Користувач вирішує через WebUI:
    //   - resume(handle) → продовжити з місця зупинки
    //   - abort(handle)  → примусово FAILED, звільнити всі ресурси
    //   - unload(handle) → відкинути без послідовності abort
} else if (err == EngineError::NVS_ERROR) {
    // Немає персистованого токена (свіже встановлення або ніколи не
    // персистовано) — слот лишається у LOADED. start() рестартує
    // сценарій з початку.
} else {
    // Токен пошкоджено (CRC), невірний scenario_id (рецепт змінився)
    // або поля поза діапазоном. Очистити слот NVS ТА трактувати як
    // відсутність даних:
    erase_nvs_slot(handle);
    // стан engine незмінний — слот досі LOADED
}
```

### Ланцюг валідації відновлення

`deserialize_token` виконує в порядку:
1. Перевірка магії (`SEQ_TOKEN_MAGIC` == 'SCTK' / `0x4B544353` LE; старі
   токени `'SQTK'` від `modesp_sequence` відкидаються)
2. Перевірка версії (`version` == `SEQ_TOKEN_VERSION`)
3. Перевірка CRC16 над [0..91]
4. `scenario_id` збігається із заголовком завантаженого рецепту
   (відкидає токен від попереднього рецепту з тим самим слотом)
5. `track_count` збігається з кількістю доріжок рецепту (відкидає зміни схеми)
6. `phase_idx` на кожну доріжку < `phase_count` рецепту (відкидає зміни
   схеми, які могли б спричинити OOB-читання)

Будь-який збій перевірки повертає конкретний `EngineError` (INVALID_FILE,
UNSUPPORTED_VERSION, CRC_MISMATCH).

### Спостережуваність відновлення

(Покращення Stage 1.5 — наразі не реалізовано):
- Engine записує `scenario.engine_recovery_pending = true`, коли
  відновлення вдалося, але сценарій очікує дії користувача
- WebUI використовує `visible_when: {scenario.engine_recovery_pending: [true]}`,
  щоб показати банер із кнопками resume/abort/unload
- Лог повідомлення ESP_LOGW: "Scenario %s recovered у state %s; may control
  resources [...]. Resume або abort to restore module state."

## Міграція схеми

Версія токена (`SEQ_TOKEN_VERSION`) інкрементується лише при несумісних
змінах. Правила сумісності назад/вперед:

1. **Додавати поля ТІЛЬКИ в кінець структури.** Зарезервовані слоти
   (`reserved_a`, `reserved_b`, `cont_state` тощо) є позначеними точками росту.
2. **НІКОЛИ не видаляти поля.** Позначити як застаріле; старі токени
   все одно розбираються.
3. **НІКОЛИ не перевикористовувати позиції полів.** Нова семантика =
   нове поле в кінці.
4. **Major bump (version → 2):** запускає помилку `UNSUPPORTED_VERSION`;
   викликач трактує як відсутність даних, стартує з нуля.

Токени — це похідний стан (авторитетним джерелом є рецепти в LittleFS).
«Інструменти» міграції не потрібні — при оновленні прошивки зі зміною
формату токена приймайте втрату незавершених сценаріїв. Документуйте як
відоме обмеження.

## Дивіться також

- [02_binary_format.md](02_binary_format.md) — байтова розкладка `.modr` (рецепту)
- [03_api_reference.md](03_api_reference.md#persistence-stage-1) — публічний API колбеків
- [10_error_model.md](10_error_model.md) — коди помилок відновлення
- [adr/0001-binary-format-not-constexpr.md](adr/0001-binary-format-not-constexpr.md) — дизайн формату токена
- Джерела: `components/modesp_scenario/src/nvs_token.cpp`,
  `nvs_observer.cpp` (політика дроселювання + persist_slot),
  `engine.cpp::try_recover`
