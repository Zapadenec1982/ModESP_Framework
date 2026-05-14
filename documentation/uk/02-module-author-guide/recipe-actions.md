# Дії та умови рецептів

> 📖 **In English:** [documentation/en/02-module-author-guide/recipe-actions.md](../../en/02-module-author-guide/recipe-actions.md)

Фази у рецептах-сценаріях викликають **дії** (щось виконати) і
обчислюють **умови** (щось перевірити). Фреймворк постачає 3 вбудовані
дії та 10 вбудованих умов; доменні модулі реєструють користувацькі під
час завантаження. Ця сторінка — повний каталог плюс рецепт додавання
власних.

## Дії проти умов

| | Дія (Action) | Умова (Condition) |
|---|---|---|
| Призначення | Побічний ефект (log, set state, wait) | Булевий тест для спрацювання переходу |
| Повертає | `ActionStatus` (OK / PENDING / FAILED_*) | Використовує той самий ActionStatus — OK = true, FAILED_RECOVERABLE = false, FAILED_ABORT = некоректний |
| Використовується у | Масивах дій фази `entry` / `exit` | Конструкціях `when` у transitions і global_transitions |
| Реєстр | `ActionRegistry::register_action` | `ActionRegistry::register_condition` (окремий простір імен) |

`ActionRegistry` фреймворку тримає дві плоскі мапи (actions, conditions),
ключовані 16-бітним djb2-хешем імені. Колізії між просторами імен
дозволені за дизайном (`time_elapsed_ms` міг би бути і дією, і умовою,
хоча зараз — лише умова).

## Вбудовані дії (3)

### `log` — записати діагностичне повідомлення

```json
{"action": "log", "params": {"msg": "Phase A entered"}}
```

| Параметр | Тип | Примітки |
|---|---|---|
| `msg` | string | Повідомлення до ~64 символів. Логується на рівні INFO з іменем рецепта як тегом ESP_LOG. |

Завжди повертає OK. Корисно для маркерів етапів і налагодження потоку
рецепта.

### `set_state` — записати ключ SharedState

```json
{"action": "set_state", "params": {
  "key": "test.output_a",
  "type": "bool",
  "value": true
}}
```

| Параметр | Тип | Примітки |
|---|---|---|
| `key` | string | Ключ SharedState, ≤ 32 символів. |
| `type` | enum | `"i32"` / `"f32"` / `"bool"`. Рядки не підтримуються (використовуйте вбудовані помічники АБО користувацьку дію). |
| `value` | scalar | Відповідає `type`. Літерали JSON працюють (`true`, `42`, `3.14`). |

Повертає OK при успіху, FAILED_RECOVERABLE, якщо запис відхилено
(вичерпано місткість, перевищено довжину ключа).

Найпоширеніша дія для рецептів, що керують обладнанням — записати
`equipment.req_compressor = true`, `simple_thermo.setpoint = 30.0`,
прапорець несправності, лічильник прогресу тощо.

### `wait_ms` — чиста часова затримка

```json
{"action": "wait_ms", "params": {"ms": 5000}}
```

| Параметр | Тип | Примітки |
|---|---|---|
| `ms` | int | 0 до 86 400 000 (один день). |

Повертає PENDING, поки `phase_elapsed_ms >= ms`, потім OK.

> 💡 **Порада:** надавайте перевагу переходу
> `{"when": {"time_elapsed_ms": 5000}}` над дією `wait_ms`. Переходи
> ефективніші (рушій не повторно викликає обробник кожен такт) і
> читабельніші. `wait_ms` існує для випадків, коли потрібна чиста
> затримка між двома іншими діями у тому ж блоці entry фази.

## Вбудовані умови (10 leaf + 3 composite)

### Прості умови

#### `time_elapsed_ms`
```json
{"time_elapsed_ms": 5000}
```
Істинна, коли `phase_elapsed_ms >= 5000`. Використовується скрізь для
переходів за часом.

#### `state_key_eq` / `_ne`
```json
{"state_key_eq": {"key": "test.fault", "value": true}}
{"state_key_ne": {"key": "mode", "value": "off"}}
```
Рівність / нерівність. Зважає на тип — порівнює `int` з `int`, `string`
з `string`. Невідповідність типів повертає FAILED_ABORT (некоректний).

#### `state_key_gt` / `_lt` / `_ge` / `_le`
```json
{"state_key_gt": {"key": "equipment.air_temp", "value": 25.0}}
{"state_key_ge": {"key": "test.counter", "value": 10}}
```
Числові порівняння. Автоматично змішує int↔float (порівнює як float,
якщо хоча б один операнд — float).

#### `state_key_in_range`
```json
{"state_key_in_range": {"key": "equipment.air_temp", "min": 20, "max": 25}}
```
Включно: істинна, якщо `min <= key_value <= max`.

#### `state_key_changed`
```json
{"state_key_changed": {"key": "test.input"}}
```
Детекція фронту — істинна при першому обчисленні після зміни значення
ключа. **MVP-заглушка** — наразі завжди повертає false
(FAILED_RECOVERABLE). Stage 1.5 підключить відстеження фронтів на боці
рушія. Використовуйте ощадливо.

#### `time_of_day_eq`
```json
{"time_of_day_eq": {"hh": 14, "mm": 30}}
```
Збіг із годинником реального часу (з точністю до хвилини). Потребує
синхронізації SNTP; повертає false, якщо epoch < 86400 (час ще не
встановлений).

### Композитні умови

#### `all_of` — булеве AND
```json
{"all_of": [
  {"time_elapsed_ms": 1000},
  {"state_key_gt": {"key": "test.x", "value": 10}}
]}
```
Усі діти мають виконуватися. Коротке замикання при першому false. Діти
можуть бути простими або композитними.

#### `any_of` — булеве OR
```json
{"any_of": [
  {"time_elapsed_ms": 30000},
  {"state_key_eq": {"key": "user.skip", "value": true}}
]}
```
Перший дитячий вузол, що виконується, перемагає. Коротке замикання.

#### `not` — булеве NOT
```json
{"not": {"state_key_eq": {"key": "test.x", "value": 0}}}
```
Заперечує єдиний дочірній вузол. (Для заперечення кількох вузлів
використовуйте `not` із `any_of` або поєднуйте з `all_of`.)

Композити можуть вкладатися до **16 рівнів** (`MAX_CONDITION_DEPTH`).
І компілятор, і завантажувач відкидають глибші дерева.

## Семантика статусів дій

Дії повертають один із чотирьох статусів (`ActionStatus` enum):

| Статус | Значення | Реакція рушія |
|---|---|---|
| `OK` | Дія завершена | Перейти до наступної entry/exit дії; або, коли всі завершено, обчислити переходи. |
| `PENDING` | Викликати знову у наступному такті | Залишитися на цій дії; рушій повторює. Використовується `wait_ms`. |
| `FAILED_RECOVERABLE` | Дія не змогла виконатися | Пропустити решту entry/exit дій у цій фазі; перехід усе одно спрацьовує, або таймаут бере гору. |
| `FAILED_ABORT` | Некоректні аргументи / фатальна | Трек → FAILED. Якщо це головний трек, сценарій переривається. |

Умови повторно використовують той самий enum:
- `OK` = true (умова виконується);
- `FAILED_RECOVERABLE` = false (умова не виконується);
- `FAILED_ABORT` = некоректні аргументи (помилка часу компіляції).

## Додавання користувацьких дій і умов

Доменні модулі реєструють користувацькі дії / умови під час
завантаження — зазвичай у `on_init` модуля. Після реєстрації рецепти
можуть посилатися на них за іменем (так само, як на вбудовані).

### 1. Написати функцію-обробник

```cpp
// modules/my_thermo/src/my_thermo_module.cpp
#include "modesp/scenario/action_registry.h"
#include "modesp/scenario/action_param.h"

using namespace modesp::scenario;

static ActionStatus do_set_thermo_target(ActionContext& ctx) {
    // Перевірити кількість параметрів
    if (ctx.param_count != 1) return ActionStatus::FAILED_ABORT;

    // Знайти параметр "target"
    const ActionParam* p = nullptr;
    for (uint8_t i = 0; i < ctx.param_count; ++i) {
        if (ctx.params[i].key_hash == djb2_hash16("target")) {
            p = &ctx.params[i];
            break;
        }
    }
    if (!p || p->type != static_cast<uint8_t>(ParamType::F32)) {
        return ActionStatus::FAILED_ABORT;
    }

    // Зробити роботу
    if (ctx.state) {
        ctx.state->set("my_thermo.target", p->v.f);
    }
    return ActionStatus::OK;
}
```

### 2. Зареєструвати під час завантаження

```cpp
bool MyThermoModule::on_init() {
    // Отримати реєстр з рушія — передається через підключення у main.cpp.
    // У типовій конфігурації реєстр — це file-static посилання, яке бачить ваш модуль.
    extern modesp::scenario::ActionRegistry scenario_actions;

    scenario_actions.register_action({
        djb2_hash16("set_thermo_target"),
        "set_thermo_target",
        &do_set_thermo_target,
        /*param_min=*/1, /*param_max=*/1
    });

    return true;
}
```

### 3. Оголосити у `tools/known_actions.json`

Компілятор валідує дії рецепта проти цього списку дозволених під час
збірки. Додайте свій запис:

```json
{
  "actions": {
    "set_thermo_target": {
      "hash": 21337,
      "param_min": 1,
      "param_max": 1,
      "params": {
        "target": {"type": "f32", "required": true}
      },
      "description": "Sets thermostat target temperature (custom)."
    }
  }
}
```

Запустіть `python tools/known_actions.py --verify`, щоб обчислити і
перевірити, що хеш збігається з `djb2_hash16("set_thermo_target")`.

### 4. Використати у рецепті

```json
{"action": "set_thermo_target", "params": {"target": 24.5}}
```

`compile_scenario.py` валідує ім'я дії і форму параметрів проти
`known_actions.json`. `engine.load()` валідує, що хеш дії існує у
зареєстрованому ActionRegistry під час виконання.

## Умови реєструються аналогічно

```cpp
static ActionStatus cond_thermo_at_target(ActionContext& ctx) {
    if (ctx.param_count != 1) return ActionStatus::FAILED_ABORT;
    // ... прочитати стан, порівняти, повернути OK / FAILED_RECOVERABLE ...
}

scenario_actions.register_condition({
    djb2_hash16("thermo_at_target"),
    "thermo_at_target",
    &cond_thermo_at_target,
    1, 1
});
```

Використання у `when` рецепта:

```json
{"to": "$complete", "when": {"thermo_at_target": {"tolerance": 0.5}}}
```

## Поля ActionContext

Що отримує ваш обробник:

```cpp
struct ActionContext {
    IStateBackend*       state;             // R/W SharedState через бекенд
    const ActionParam*   params;            // Масив (param_count) параметрів
    uint8_t              param_count;       // Кількість параметрів, оголошених у рецепті
    uint16_t             string_pool_size;  // Для розв'язання параметрів STR
    const char*          string_pool;       // Пул рядків з .modr
    uint32_t             scenario_elapsed_ms;
    uint32_t             phase_elapsed_ms;
    uint8_t              phase_idx;
    SequenceHandle       handle;            // Дескриптор екземпляра сценарію (1..MAX)
    TrackIdx             track;             // Індекс треку з 0
    const char*          recipe_name;       // Для діагностики
    const char*          track_name;
};
```

Не пишіть у `params` (вони const). Читайте стан через `state->get_raw`
або шаблонний `state->get<T>(key, out)`. Пишіть через
`state->set(key, value)`.

## Рядкові параметри

Якщо ваша дія приймає рядковий параметр:

```cpp
const ActionParam* key_p = /* пошук параметра "key" */;
if (key_p->type != static_cast<uint8_t>(ParamType::STR)) return FAILED_ABORT;

char buf[64];
uint16_t offset = key_p->v.s_idx;          // зміщення у пулі рядків
if (offset >= ctx.string_pool_size) return FAILED_RECOVERABLE;
uint8_t len = ctx.string_pool[offset];     // префіксований довжиною
if (offset + 1u + len > ctx.string_pool_size) return FAILED_RECOVERABLE;
std::memcpy(buf, &ctx.string_pool[offset + 1], len);
buf[len] = '\0';
// використовуйте `buf` як C-рядок.
```

Файл `builtin_actions.cpp` фреймворку (помічник `copy_string`) показує
цей шаблон дослівно. Stage 1.5 може загорнути це у вбудований
помічник у заголовку action_param.

## Помилки і діагностика

- **`compile_scenario.py` відкидає невідому дію:** додайте запис у
  `known_actions.json` І зареєструйте у своєму модулі.
- **`engine.load_buffer` повертає `UNKNOWN_ACTION` / `UNKNOWN_CONDITION`:**
  файл `.modr` посилається на ім'я, якого немає у середовищі виконання
  ActionRegistry. `on_init` модуля не запустився або не зареєстрував,
  або `known_actions.json` неузгоджений із фактичними викликами
  реєстрації.
- **Дія повертає FAILED_ABORT:** перевірте власний вивід ESP_LOG дії —
  більшість вбудованих логує причину. Найчастіше — неправильна
  кількість або тип параметрів.

## Коли писати користувацьку дію

**Підходить:**
- Доменно-специфічні записи у кілька ключів стану атомарно.
- Апаратні операції, які повинен оркеструвати рушій сценаріїв (старт
  циклу відтайки, тригер OTA).
- Логіка зі станом, що потребує збереження між тактами (лічильник,
  деба́унсер).
- Читання складних значень (сирий NTC → температура через калібрувальну
  таблицю).

**Не варто:**
- Прості записи стану — використовуйте `set_state` із одним значенням.
- Часові затримки — використовуйте перехід `time_elapsed_ms`, а не дію
  `wait_ms`.
- «Друкувати значення за умовою» — поєднайте умову `state_key_*` І дію
  `log` у фазі.

## Типові помилки

**Забути оновити `known_actions.json`:** модуль реєструється успішно,
рецепт компілюється з попередженням, `.modr` відхиляється під час
виконання. Завжди оновлюйте обидва місця, коли додаєте дії.

**Колізія хешів:** якщо два імена дій хешуються в один і той же uint16,
реєстр відхиляє другий. djb2_hash16 має хороший розподіл, але з 65535
бакетами і ~сотнями дій колізії залишаються рідкісними. Аудит
`known_actions.json` ловить їх на етапі рев'ю PR.

**Забути `param_min` / `param_max`:** реєстр приймає, але валідація
рецепта в compile_scenario.py може пропустити навіть з надто великою
або надто малою кількістю параметрів. Встановлюйте реалістичні межі.

**Побічні ефекти в умовах:** умови мають бути **чистими читаннями**.
Якщо ваша умова мутує стан, рушій обчислює її кілька разів за фазу
(один раз на перевірку переходу за такт) — побічні ефекти
накопичуються непередбачувано. Використовуйте дії для мутацій.

**Тривала робота у дії:** дії тикають на задачі рушія з частотою 100 Гц.
Робота довша за 5 мс блокує рушій. Якщо потрібна повільна робота,
напишіть сервісний модуль, який виконує її асинхронно, І тригерить
зміну ключа стану, яку можуть спостерігати умови рецепта.

## Що далі

- **[recipe-authoring.md](recipe-authoring.md)** — використання дій і
  умов у фазах та переходах.
- **[continuous-behaviors.md](continuous-behaviors.md)** — контролери
  PID / гістерезису / лінійної зміни, що працюють поряд із фазами
  (відрізняються від дій).
- **[scenario-engine/03_api_reference.md](../03-framework-reference/scenario-engine/03_api_reference.md)** —
  API ActionRegistry і Engine.
- **[scenario-engine/10_error_model.md](../03-framework-reference/scenario-engine/10_error_model.md)** —
  повна таксономія ActionStatus і таблиця реакцій рушія.

## Джерела

- [`components/modesp_scenario/include/modesp/scenario/action_registry.h`](../../../components/modesp_scenario/include/modesp/scenario/action_registry.h) — API реєстру.
- [`components/modesp_scenario/src/actions/builtin_actions.cpp`](../../../components/modesp_scenario/src/actions/builtin_actions.cpp) — реалізація вбудованих дій і умов.
- [`tools/known_actions.json`](../../../tools/known_actions.json) — каталог аудиту для валідації під час компіляції.
