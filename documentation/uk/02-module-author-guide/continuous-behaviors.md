# Безперервні поведінки

> 📖 **In English:** [documentation/en/02-module-author-guide/continuous-behaviors.md](../../en/02-module-author-guide/continuous-behaviors.md)

**Безперервна поведінка** — це контур керування, який виконується
кожний такт рушія, поки активна фаза рецепта — PID-контролер,
гістерезисний релейний контролер, генератор лінійної зміни, будь-що,
що видає вихід, що змінюється у часі. Якщо дії виконуються одноразово
(один раз під час entry/exit), а переходи дискретні (спрацьовують або
ні), то безперервні поведінки керовані тактами протягом усього часу
перебування у фазі.

Фреймворк постачає 3 стандартні примітиви (PID, гістерезис, лінійна
зміна). Доменні модулі можуть реєструвати користувацькі поведінки за
тим самим шаблоном. Ця сторінка документує вбудовані, їхні параметри і
як написати власну.

## Коли використовувати безперервні поведінки

| Потреба | Що взяти |
|---|---|
| Утримувати температуру на уставці | PID (аналоговий актуатор) або гістерезис (реле) |
| Плавна лінійна зміна уставки у часі | Лінійна зміна (ramp) |
| Запустити щось точно при вході/виході з фази | Дія (entry/exit) |
| Повторити перевірку кожен такт (тест переходу) | Умова у конструкції `when` |
| Безперервне керування зі зворотним зв'язком під час фази | Безперервна поведінка |

Поширений шаблон: фаза активує PID-контролер із цільовою уставкою; PID
працює кожен такт, регулюючи вихід актуатора, доки фаза не перейде далі,
і у цей момент PID деактивується.

## Вбудовані примітиви

Фреймворк постачає три у `modesp::scenario::primitives`. Зареєструйте їх
під час завантаження:

```cpp
modesp::scenario::ContinuousRegistry continuous_registry;
modesp::scenario::primitives::register_primitives(continuous_registry);
// Тепер "pid", "hysteresis", "ramp" доступні для рецептів.
```

(Вже зроблено в шаблоні `main.cpp` фреймворку. Перевірте
[main.cpp](../../../main/main.cpp) для фактичного підключення.)

### `pid` — PID-контролер замкненого контуру

Стандартний PID у паралельній формі з диференціюванням за вимірюванням
(без кидка диференціальної складової при зміні уставки) і умовним
анти-windup інтегруванням.

**Параметри** (передаються при активації):

| Параметр | Тип | Примітки |
|---|---|---|
| `input_key` | string | Ключ SharedState для виміряного значення (наприклад, `"equipment.air_temp"`). |
| `output_key` | string | Ключ SharedState для виходу керування (наприклад, `"equipment.req_heater_pwm"`). |
| `setpoint` | float | Бажане значення (інженерні одиниці). |
| `kp` | float | Пропорційний коефіцієнт. |
| `ki` | float | Інтегральний коефіцієнт (1/с). |
| `kd` | float | Диференціальний коефіцієнт (с). |
| `out_min` | float | Нижнє обмеження виходу. |
| `out_max` | float | Верхнє обмеження виходу. |

**Алгоритм:**
```
error      = setpoint - measurement
P_term     = kp × error
I_term     = ki × integral
D_term     = -kd × (measurement - prev_measurement) / dt
output     = P + I + D, обмежено до [out_min, out_max]
integral  += error × dt, ЛИШЕ якщо вихід не насичений проти напрямку інтегрування
```

Анти-windup: інтегрування призупиняється, коли вихід насичений І
помилка штовхала б його далі у напрямку насичення.

> 💡 **Порада:** для температурних контурів починайте з kp = кілька
> одиниць на градус, ki = kp / time_constant_seconds, kd = 0.
> Налаштовуйте емпірично. Якщо невпевнені, гістерезисний контролер
> поблажливіший.

### `hysteresis` — релейний з зоною нечутливості

Пороговий контролер — перемикає бінарний вихід, коли вимірювання
перетинає setpoint ± deadband. Без брязкоту реле, бо вихід зберігає
останнє значення, поки знаходиться у зоні нечутливості.

**Параметри:**

| Параметр | Тип | Примітки |
|---|---|---|
| `input_key` | string | Ключ виміряного значення. |
| `output_key` | string | Ключ бінарного виходу (`bool` у SharedState). |
| `setpoint` | float | Цільове значення. |
| `deadband` | float | Ширина гістерезису (симетрично навколо уставки). |
| `mode` | int | `0` = охолодження (вище → ON), `1` = нагрів (нижче → ON). |

**Алгоритм (mode = нагрів):**
- Нижче `setpoint - deadband`: вихід ON.
- Вище `setpoint + deadband`: вихід OFF.
- У зоні нечутливості: тримати останнє значення.

Примусово вимикає вихід OFF при `on_deactivate` (безпечний стан).
Рецепти, що хочуть іншу поведінку, мають явно зробити `set_state` після
переходу далі.

### `ramp` — генератор лінійної зміни

Записує значення, яке лінійно інтерполюється від `start_value` до
`end_value` за `duration_ms`. Використовується для плавних переходів
уставки (керована крива нагріву, поступове відкриття клапана).

**Параметри:**

| Параметр | Тип | Примітки |
|---|---|---|
| `output_key` | string | Ключ, що отримує інтерпольоване значення (float). |
| `start_value` | float | Значення при t = 0. |
| `end_value` | float | Значення при t = duration_ms. |
| `duration_ms` | int | Загальна тривалість лінійної зміни. |

Лічильник минулого часу з насиченням — як тільки
`elapsed_ms >= duration_ms`, вихід тримається на `end_value`
безстроково. Стан (elapsed_ms) скидається при кожній повторній
активації.

## Використання безперервних поведінок у рецептах

> ⚠️ **Підключення у Stage 1.5:** бінарний формат резервує `cont_mask` у
> кожній фазі, але рушій ще не активує ContinuousBehaviors із фаз
> рецепта. Наразі єдиний спосіб керувати PID/гістерезисом/рампою —
> інстанціювати їх на стороні C++ у вашому доменному модулі і подавати
> параметри вручну. Stage 1.5 підключить активацію, керовану фазою,
> через біти `cont_mask`, що посилаються на зареєстровані поведінки.
> Інтерфейс нижче описує запланований синтаксис рецепта.

Запланований синтаксис рецепта:

```json
{
  "name": "active_phase",
  "continuous": [
    {
      "behavior": "pid",
      "params": {
        "input_key": "equipment.air_temp",
        "output_key": "equipment.req_heater_pwm",
        "setpoint": 22.0,
        "kp": 5.0, "ki": 0.1, "kd": 0.5,
        "out_min": 0, "out_max": 100
      }
    }
  ],
  "transitions": [...]
}
```

Семантика рушія:
- `on_activate(params, ctx)` викликається при вході у фазу. Поведінка
  читає параметри та ініціалізує стан.
- `on_tick(dt_ms, ctx)` викликається кожен такт рушія (~10 мс).
  Поведінка читає входи з SharedState, обчислює, пише виходи.
- `on_deactivate(ctx)` викликається при виході з фази. Поведінка може
  зробити очищення, скинути виходи тощо.

Якщо та сама поведінка згадана у послідовних фазах, рушій зберігає
екземпляр живим через межу (інтеграл PID переноситься). Якщо фаза
посилається на інші поведінки, рушій деактивує стару й активує нову на
межі переходу.

## Ручне використання (реальність Stage 1)

Поки активація з рецепта не реалізована, керуйте поведінками з вашого
доменного модуля:

```cpp
// У on_init модуля:
behavior_ = modesp::scenario::primitives::pid_factory();
// або: behavior_ = continuous_registry.create(djb2_hash16("pid"));

// У on_update — вручну керуйте активацією/тактом/деактивацією:
ActionContext ctx{};
ctx.state = &shared_state_backend;
ctx.params = build_params_array();
ctx.param_count = N;
ctx.string_pool = scenario.string_pool_data();  // або власний
ctx.string_pool_size = ...;

if (!activated_) {
    behavior_->on_activate(ctx.params, ctx.param_count, ctx.string_pool, ctx);
    activated_ = true;
}
behavior_->on_tick(dt_ms, ctx);

// Коли завершено:
behavior_->on_deactivate(ctx);
delete behavior_;
```

Це працює, але втрачає інтеграцію з рушієм сценаріїв (автоматичну
активацію/деактивацію на межах фаз). Використовуйте лише як міст до
Stage 1.5.

## Написання користувацької безперервної поведінки

Шаблон із трьох кроків: успадкувати ContinuousBehavior, зареєструвати
фабрику під час завантаження, посилатися у рецепті (Stage 1.5).

### 1. Успадкувати `ContinuousBehavior`

```cpp
// modules/my_thermo/include/my_thermo_pid_variant.h
#pragma once
#include "modesp/scenario/continuous_behavior.h"
#include "modesp/scenario/modr_format.h"   // djb2_hash16

class MyPidVariant : public modesp::scenario::ContinuousBehavior {
public:
    static constexpr const char* NAME = "my_pid_variant";

    void on_activate(const modesp::scenario::ActionParam* params, uint8_t n,
                     const char* string_pool,
                     modesp::scenario::ActionContext& ctx) override;
    void on_tick(uint32_t dt_ms, modesp::scenario::ActionContext& ctx) override;
    void on_deactivate(modesp::scenario::ActionContext& ctx) override;

    uint16_t hash() const override { return modesp::scenario::djb2_hash16(NAME); }
    const char* name() const override { return NAME; }

private:
    // ваш стан...
    char output_key_[32] = {0};
    float kp_ = 1.0f;
    // ...
};
```

### 2. Реалізувати і зареєструвати

```cpp
// modules/my_thermo/src/my_thermo_pid_variant.cpp
#include "my_thermo_pid_variant.h"

void MyPidVariant::on_activate(const modesp::scenario::ActionParam* params,
                                uint8_t n, const char* string_pool,
                                modesp::scenario::ActionContext& ctx) {
    // Прочитати ваші параметри...
}

void MyPidVariant::on_tick(uint32_t dt_ms, modesp::scenario::ActionContext& ctx) {
    // Обчислити і записати output_key...
}

void MyPidVariant::on_deactivate(modesp::scenario::ActionContext& ctx) {
    // Очищення...
}

// Фабрика повертає новий екземпляр, виділений у купі.
// Викликаюча сторона (рушій) видаляє при деактивації фази.
static modesp::scenario::ContinuousBehavior* my_pid_factory() {
    return new MyPidVariant();
}
```

```cpp
// modules/my_thermo/src/my_thermo_module.cpp — у on_init:
bool MyThermoModule::on_init() {
    extern modesp::scenario::ContinuousRegistry continuous_registry;
    continuous_registry.register_factory(
        modesp::scenario::djb2_hash16("my_pid_variant"),
        "my_pid_variant",
        &my_pid_factory
    );
    return true;
}
```

### 3. Посилання у рецепті (Stage 1.5)

```json
"continuous": [
  {"behavior": "my_pid_variant", "params": {...}}
]
```

До Stage 1.5 інстанціюйте вручну через фабрику:

```cpp
auto* drv = continuous_registry.create(djb2_hash16("my_pid_variant"));
// використовуйте вручну, як описано вище.
```

## Інтерфейс ContinuousBehavior

```cpp
class ContinuousBehavior {
public:
    virtual ~ContinuousBehavior() = default;

    /// Викликається при вході у фазу, де встановлено біт cont_mask цієї поведінки.
    virtual void on_activate(const ActionParam* params, uint8_t param_count,
                             const char* string_pool, ActionContext& ctx) = 0;

    /// Викликається кожен такт рушія, поки активна (~10 мс).
    virtual void on_tick(uint32_t dt_ms, ActionContext& ctx) = 0;

    /// Викликається при виході з фази (наступна фаза не використовує цю поведінку).
    virtual void on_deactivate(ActionContext& ctx) = 0;

    /// Необов'язкова персистентність у NVS для відновлення після збою.
    virtual size_t serialize(uint8_t* buf, size_t cap) const { return 0; }
    virtual bool deserialize(const uint8_t* buf, size_t len) { return true; }

    /// Ідентичність для зіставлення у реєстрі.
    virtual uint16_t hash() const = 0;
    virtual const char* name() const = 0;
};
```

| Метод | Примітки |
|---|---|
| `on_activate` | Зчитати параметри у стан екземпляра. Налаштувати інтегратори / акумулятори. Вихід можна записати негайно або відкласти до першого такту. |
| `on_tick` | Гарячий шлях — має бути швидким (< 1 мс зазвичай). Прочитати входи, обчислити, записати виходи. `dt_ms` — час від попереднього такту (зазвичай 10 мс). |
| `on_deactivate` | Очищення, безпечний стан виходу, лог фінального стану. Не звільняйте власну пам'ять — екземпляром володіє рушій. |
| `serialize`/`deserialize` | Необов'язкова можливість Stage 1.5 для відновлення стану інтегратора після втрати живлення. Типово — заглушка. |

## Розв'язання параметрів

Так само, як у діях — масив `ActionParam[]` із `key_hash`/`type`/`value`.
Шаблон читання (взятий із `continuous_primitives.cpp`):

```cpp
namespace {
const ActionParam* find_param(const ActionParam* params, uint8_t n,
                              uint16_t key_hash) {
    for (uint8_t i = 0; i < n; ++i) {
        if (params[i].key_hash == key_hash) return &params[i];
    }
    return nullptr;
}

bool param_to_float(const ActionParam* p, float& out) {
    if (!p) return false;
    if (p->type == static_cast<uint8_t>(ParamType::F32)) { out = p->v.f; return true; }
    if (p->type == static_cast<uint8_t>(ParamType::I32)) { out = static_cast<float>(p->v.i); return true; }
    return false;
}
}

void MyBehavior::on_activate(const ActionParam* params, uint8_t n,
                              const char* sp, ActionContext& ctx) {
    param_to_float(find_param(params, n, djb2_hash16("setpoint")), setpoint_);
    param_to_float(find_param(params, n, djb2_hash16("kp")), kp_);
    // ...
}
```

## Шаблони доступу до SharedState

Поведінки читають ключі-входи і пишуть ключі-виходи через `ctx.state`.
Так само, як дії, але викликаються кожен такт — переконайтеся, що
читання/запис швидкі:

```cpp
void MyBehavior::on_tick(uint32_t dt_ms, ActionContext& ctx) {
    if (!ctx.state) return;
    if (input_key_[0] == '\0' || output_key_[0] == '\0') return;

    float input;
    modesp::StateValue v;
    if (!ctx.state->get_raw(input_key_, v)) return;
    if (auto pf = etl::get_if<float>(&v)) input = *pf;
    else if (auto pi = etl::get_if<int32_t>(&v)) input = static_cast<float>(*pi);
    else return;

    // Обчислити вихід...
    float output = compute(input, dt_ms);

    ctx.state->set(output_key_, output);
}
```

Зберігайте ключі у стані екземпляра під час `on_activate`, а не кожен
такт.

## Модель пам'яті

Екземпляри `ContinuousBehavior` **виділяються у купі** своєю фабрикою і
звільняються рушієм при `on_deactivate` (або вивантаженні екземпляра).
Фреймворк порушує власне правило «без купи» тут, тому що:

1. Безперервні поведінки створюються рідко — при вході у фазу. Не
   гарячий шлях.
2. Стан різниться для кожної поведінки — узагальнене попереднє
   виділення у рушії марнує RAM.
3. Бюджет купи на ESP32 (~65 KB вільного) легко вміщає ~10 KB
   екземплярів поведінок одночасно через усі запущені сценарії.

Якщо вам потрібна поведінка, що виділяє додаткову купу (буфер,
підоб'єкти), тримайте її обмеженою — це виправдано лише для відомих
пікових розмірів.

## Типові помилки

**Забути встановити вихід:** поведінка читає вхід, обчислює, але ніколи
не записує вихід. Ключ-вихід стану залишається на початковому значенні
назавжди. Завжди закінчуйте `on_tick` викликом
`ctx.state->set(output_key_, ...)`.

**Читання ключа, що ще не записаний:** якщо ваш `input_key` посилається
на сенсор, який запускається лише після затримки, `on_activate` може
запуститися до першого зчитування. Перевіряйте через `ctx.state->get_raw`
І перевірки `is_healthy`; повертайтеся до безпечних типових значень.

**Інтегральний windup без анти-windup:** PID з обмеженим виходом, але
без обмеження інтегратора, накопичує величезні значення, коли вихід
насичується. Вбудований PID має анти-windup — ваша користувацька
поведінка теж повинна. Шаблон:

```cpp
if (!output_saturated_against_error_sign) {
    integral_ += error * dt;
}
```

**Важка робота у on_tick:** реальний такт 100 Гц — бюджет < 1 мс.
Уникайте записів у NVS, блокуючого I/O, складного парсингу. Попередньо
обчислюйте калібрувальні таблиці у `on_activate`.

**Забути обробити dt_ms = 0:** перший такт у деяких тестових середовищах
може прийти з dt_ms = 0. Захиститеся від ділення на нуль у
диференціальних складових.

**Перенесення стану через повторну активацію:** якщо вашу поведінку
активовано, потім деактивовано, потім знову активовано (той самий
екземпляр), стан зберігається, поки ви явно не скинете його у
`on_activate`. Типова поведінка вбудованого PID — «зберігати інтеграл».
Виберіть послідовну поведінку і задокументуйте її.

## Що далі

- **[recipe-authoring.md](recipe-authoring.md)** — синтаксис фази, що
  посилається на безперервні поведінки (Stage 1.5).
- **[recipe-actions.md](recipe-actions.md)** — каталог дій і умов.
- **[scenario-engine/03_api_reference.md](../03-framework-reference/scenario-engine/03_api_reference.md)** —
  API ContinuousRegistry.
- **[components/modesp_scenario.md](../03-framework-reference/components/modesp_scenario.md)**
  *(заплановано)* — внутрішня будова рушія сценаріїв.

## Джерела

- [`components/modesp_scenario/include/modesp/scenario/continuous_behavior.h`](../../../components/modesp_scenario/include/modesp/scenario/continuous_behavior.h) — інтерфейс.
- [`components/modesp_scenario/include/modesp/scenario/continuous_primitives.h`](../../../components/modesp_scenario/include/modesp/scenario/continuous_primitives.h) — оголошення PID/Hysteresis/Ramp.
- [`components/modesp_scenario/src/continuous/continuous_primitives.cpp`](../../../components/modesp_scenario/src/continuous/continuous_primitives.cpp) — реалізації.
