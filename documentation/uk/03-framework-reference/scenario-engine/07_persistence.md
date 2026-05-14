# 07 — Персистентність у NVS

> 📖 **In English:** [documentation/en/03-framework-reference/scenario-engine/07_persistence.md](../../../en/03-framework-reference/scenario-engine/07_persistence.md)

Відновлення після перезавантаження живлення через 96-байтові токени,
що зберігаються у ESP-IDF NVS. Engine виявляє зміни стану щотакту,
обмежує частоту неургентних записів і викликає колбек, наданий
викликачем. На завантаженні рецепт перечитується з файлової системи;
викликач звертається до `try_recover()`, який читає токен ТА відновлює
позицію фази, перш ніж сценарій знову увійде у стан PAUSED.

## Розкладка сховища

| Простір імен NVS | Формат ключа | Значення |
|---|---|---|
| `seqstate` | `t<slot>` (наприклад, `t0`, `t1`, ..., `t<MAX_SEQUENCES-1>`) | 96-байтовий blob `seq_token` |

`MAX_SEQUENCES = 4` за замовчуванням → ключі `t0`..`t3`. Ключі на екземпляр
дозволяють незалежне відновлення кількох паралельних сценаріїв.

## Формат токена (`seq_token`, 96 байтів)

Визначений у `nvs_token.h`. Розкладка відповідає плану Q7. POD без
доповнення за межами явних полів:

```c
struct seq_token {                       // загалом 96 байтів
    uint32_t magic;                      // [0..3]   = 'SQTK' (0x4B545153 LE)
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

`SequenceEngine::persist_scan()` застосовує цю політику щотакту після
`instance_tick`:

| Подія | Час персистентності | Обґрунтування |
|-------|---------------|-----------|
| Зміна стану сценарію (LOADED→RUNNING, abort, complete, fail) | **Негайно** | Критична для збоїв подія має пережити |
| Просування фази на головній доріжці (доріжка з `MODR_TRACK_FLAG_MAIN`) | **Негайно** | Інваріанти головної доріжки збережено |
| Просування фази на бічній доріжці | Дроселюється до ≥1 с між записами | Захист від зношування flash |
| 5-хвилинна контрольна точка (Stage 1.5) | Періодично | Гарантує точність resume, якщо активна лише бічна доріжка |

Поля відстеження на слот у `Slot`:
- `last_persisted_state` — що було записано востаннє
- `last_persisted_phase_idx[6]` — індекси фаз на кожну доріжку
- `time_since_persist_ms` — насичуючий лічильник дроселювання

Виявлення змін порівнює поточний рантайм із last_persisted_*; персистентність
спрацьовує лише за наявності реальних змін.

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

Engine не викликає `nvs_set_blob` напряму — натомість викликає колбек,
наданий викликачем. Це утримує код engine незалежним від цільової
платформи (хост-тести надають in-memory моки).

```cpp
using NvsWriteFn = bool (*)(void* user, uint8_t slot,
                            const uint8_t* token, size_t len);
using NvsReadFn  = bool (*)(void* user, uint8_t slot,
                            uint8_t* token_buf, size_t* in_out_len);

engine.set_nvs_callbacks(write_fn, read_fn, user_ctx);
```

Підключення на цільовій платформі у `main.cpp`:

```cpp
static auto seq_nvs_write = [](void*, uint8_t slot,
                                const uint8_t* token, size_t len) -> bool {
    char key[8];
    std::snprintf(key, sizeof(key), "t%u", static_cast<unsigned>(slot));
    return modesp::nvs_helper::write_blob("seqstate", key, token, len);
};

static auto seq_nvs_read = [](void*, uint8_t slot,
                               uint8_t* buf, size_t* in_out_len) -> bool {
    char key[8];
    std::snprintf(key, sizeof(key), "t%u", static_cast<unsigned>(slot));
    size_t out_len = 0;
    bool ok = modesp::nvs_helper::read_blob("seqstate", key, buf,
                                             *in_out_len, out_len);
    if (ok) *in_out_len = out_len;
    return ok;
};

sequence_engine.set_nvs_callbacks(seq_nvs_write, seq_nvs_read, nullptr);
```

Колбеки викликаються лише із задачі оновлення engine — викликачеві не
потрібна синхронізація поверх тієї, яку надає сам NVS.

### Обробка збоїв колбеку

`write_fn` повертає `false`:
- Engine логує попередження (Stage 1.5 — наразі тихий фолбек)
- `last_persisted_*` слоту НЕ оновлюється → наступний такт повторить
- Каскадний персистентний збій може блокувати інші операції; рецепти,
  що покладаються на персистентність задля безпеки, мають моніторити
  стан бекенду

`read_fn` повертає `false`:
- `try_recover()` повертає `EngineError::NVS_ERROR`
- Викликач вирішує, чи робити abort сценарію, чи стартувати з нуля

## Потік відновлення

На завантаженні прошивки після reset:

```cpp
// Крок 1: Перезавантажити рецепт (бінарник рецепту живе у LittleFS і
// зберігається між перезавантаженнями незалежно від NVS)
auto handle = engine.load_path("/data/scenarios/abs_test.modr");
if (handle == 0) { /* рецепт відсутній */ return; }

// Крок 2: Спроба відновлення
EngineError err = engine.try_recover(handle);
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
1. Перевірка магії (`SEQ_TOKEN_MAGIC` == 'SQTK')
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
  `engine.cpp::persist_scan` і `try_recover`
