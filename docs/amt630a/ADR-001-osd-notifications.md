# ADR-001 — Доставка системних сповіщень до OSD-рендерера AMT630A

- **Статус:** Прийнято
- **Дата:** 2026-06-14
- **Контекст проєкту:** ModESP Framework (ESP-IDF v5.5, C++17 + ETL, single main-loop task)
- **Замінює:** §6.3 п.3 design-доку (`AMT630A_driver_design.md`) — там було закладено *чистий SharedState-polling*

---

## Контекст

OSD-рендерер AMT630A має показувати overlay-банери системних сповіщень (alarm / SAFE_MODE / warning)
поверх живого меню: різного **пріоритету**, з **TTL** (показати N мс і зняти), з можливою **чергою**
(кілька сповіщень підряд, вище витісняє нижче). Сповіщення — це **подія в часі (edge)**, а не рівень.

Що вже є у фреймворку (підтверджено по коду):

- **`etl::message_bus<24>`** у `ModuleManager` — синхронна, zero-heap шина. `publish()` → `bus_.receive(msg)`
  негайно викликає `on_message()` усіх підписників на стеку відправника (`module_manager.cpp:150`).
- **Усі модулі вже підписані** — `ModuleAdapter` catch-all (`accepts()==true`), підписка автоматична при
  `register_module()`. `DisplayModule` реєструється через `modesp_register_modules(app)` → **вже на шині**.
- `BaseModule::on_message()` існує як віртуальний хук (`base_module.h:43`); `DisplayModule` його просто **не
  перевизначає** (а не «не має шини», як помилково стверджував §6.3).
- **Продюсери з severity вже є:** `ErrorService` шле `MsgSafeMode` / `MsgSystemError{ErrorSeverity}` (INFO..FATAL).
- **`SharedState`** — thread-safe KV з delta/version, zero-heap, але **last-write-wins**: дві швидкі події між
  поллами зливаються → проміжна (часто найважливіший alarm) губиться. Черги/пріоритетів немає.
- Усі продюсери (`ErrorService`, `scenario`) працюють у тому ж main-loop task'і — **ISR-джерел сповіщень немає**.

---

## Розглянуті варіанти

| Критерій | A) SharedState-polling | B) Чистий message-bus | C) Окрема FreeRTOS-черга | **D) Гібрид (bus + черга в споживачі)** |
|---|---|---|---|---|
| Lossless (edge-події) | ✗ коалесинг, alarm губиться | ~ доставляє, але без буфера | ✓ до глибини черги | ✓ для подій |
| Пріоритет / витіснення | ✗ немає | ✗ шина не ранжує | ✗ FIFO без пріоритетів | ✓ priority-queue у Display |
| TTL / авто-dismiss банера | ручний `notify_ms` | немає | немає | ✓ через `on_update(dt_ms)` |
| Decoupling продюсер↔OSD | ✓ найкращий | ✓ через типи | ✗ продюсер знає handle | ✓ через типи |
| Нова інфраструктура | нові ключі | 0 (шина є) | нова черга+lifecycle | 0 (шина є) + ETL-буфер у Display |
| Heap | 0 | 0 | статичний буфер | ~0 (ETL static у Display) |
| ISR-safe | ✗ mutex | ✗ синхронний виклик | ✓ `…FromISR` | n/a (ISR-джерел нема) |
| Консистентність з ModESP | висока (прецедент) | середня (тільки EquipmentBase слухає) | низька (чужий патерн) | висока (шина + дзеркало в SharedState) |

A втрачає alarm-и (level vs edge). B доставляє, але не чергує і не пріоритезує — недостатньо. C виправдана **лише**
за наявності ISR-/крос-таск джерела (зараз нема) і дублює наявну шину.

---

## РІШЕННЯ

**Обрано варіант D — гібрид поверх уже наявного `etl::message_bus`.**

Продюсери (`ErrorService` та будь-який модуль, що детектує alarm) **публікують подію** `MsgSystemNotice{level,
ttl_ms, text}` на наявну шину. `DisplayModule` **перевизначає `on_message`**, кладе подію у свою статичну
**priority-чергу банерів**, а логіку показу / витіснення / TTL веде у `on_update(dt_ms)`. Поточний активний банер
**дзеркалиться у SharedState** (`display.banner*`) — для WebUI/MQTT, тим самим ключ-каналом, що вже використовують
WS/MQTT.

**Чому D, а не закладений §6.3 polling:**

1. **Не вводить чужорідний механізм** — шина вже є, `DisplayModule` уже на ній (catch-all). Polling SharedState для
   *подій* — це обхід, який втрачає саме alarm-и (last-write-wins).
2. **Чергу й пріоритети ставимо туди, де їм місце — у споживача.** Тільки Display знає, скільки банерів влізе,
   скільки тримати, чи alarm витісняє info. Це UX-best-practice (Adobe Spectrum: вище витісняє нижче, не показувати
   кілька одночасно).
3. **Природна інтеграція з тіком.** `on_message` лише `push` (мс), уся логіка таймінгу — у `on_update(dt_ms)`, який
   уже крутиться 100 Гц; `dt_ms` — готовий лічильник зворотного відліку TTL.
4. **Zero-heap, відповідає правилам проєкту** (`etl::vector` фіксованого розміру, `etl::string<48>`).
5. **Дзеркало в SharedState зберігає консистентність** з наявним каналом WebUI/MQTT (`StatusText`, `protection.*`),
   не роблячи його єдиним джерелом для подій.

**Принцип підписки (уточнено за зворотним зв'язком):** `DisplayModule` фільтрує `on_message` **виключно** на
`msg_id::SYSTEM_NOTICE` (явний короткий список). Він **НЕ** показує `MsgSafeMode`/`MsgSystemError`/`MsgModuleTimeout`
автоматично — вони падають у `BaseModule::on_message` (no-op). Рішення «це треба на екран» належить **ПРОДЮСЕРУ**
(зокрема бізнес-логіці продуктових модулів), який свідомо публікує `MsgSystemNotice`. На екрані — лише повідомлення,
на які дисплей **явно підписаний**, а не всі алярми шини. Якщо потрібен банер по SAFE_MODE — його публікує
`ErrorService`, додатково шлючи `MsgSystemNotice` (рішення продюсера, не дефолт дисплея).

---

## Дизайн

### Тип події (продюсери) — `service_messages.h`

```cpp
enum class NoticeLevel : uint8_t { INFO = 0, WARN = 1, ALARM = 2 };  // дзеркалить ErrorSeverity

struct MsgSystemNotice : etl::message<msg_id::SYSTEM_NOTICE> {  // новий id у діапазоні сервісів 50-99
    NoticeLevel     level;
    uint16_t        ttl_ms;        // 0 = тримати до явного зняття
    etl::string<48> text;          // zero-heap payload
};
```

Продюсер (приклад — `ErrorService` поряд із наявними `MsgSystemError`/`MsgSafeMode`):

```cpp
publish(MsgSystemNotice{NoticeLevel::ALARM, 5000, "SAFE MODE: sensor fault"});
```

### Споживач — `DisplayModule`

```cpp
void DisplayModule::on_message(const etl::imessage& msg) override {
    if (msg.get_message_id() == modesp::msg_id::SYSTEM_NOTICE) {
        const auto& n = static_cast<const MsgSystemNotice&>(msg);
        banners_.push(n);                 // priority за level; повна → дропати НАЙНИЖЧИЙ, не новий alarm
    }
    BaseModule::on_message(msg);          // не «з'їдати» інші повідомлення (catch-all router!)
}

void DisplayModule::on_update(uint32_t dt_ms) override {
    // 1) меню як зараз (poll_button + engine_.tick + diff-render)
    // 2) активний банер: remaining_ -= min(remaining_, dt_ms); якщо ≤0 → active_=false
    // 3) preemption: якщо черга непорожня і top.level > active_level_ (або банера нема) → promote()
    // 4) renderer_->notify(active_text, active_level) — overlay W0; зняття → renderer_->notify(nullptr,0)
    // 5) дзеркало стану (track_change=true): WebUI/MQTT бачать поточний банер
    state_set("display.banner",       active_text_.c_str());
    state_set("display.banner_level", (int32_t)active_level_);
}
```

- **Черга:** `etl::vector<MsgSystemNotice, 8> banners_` (або `etl::priority_queue`) — статична, N=8.
  Політика переповнення: **дроп найнижчого пріоритету**, ніколи не дропати новий ALARM.
- **Де пріоритети:** виключно в `DisplayModule` (споживач), а не на шині й не в SharedState.
- **Рендер:** `AMT630ARenderer::notify(text, level)` — окреме OSD-вікно W0 (фікс addr `000h`, найвищий
  пріоритет шару), per-window scale `FB32`, колір із палітри (`ALARM`=червоний, `WARN`=жовтий), напівпрозорий
  фон поверх живого відео. Знімається передачею порожнього тексту.

### SharedState-ключі (дзеркало, НЕ транспорт)

| Ключ | Тип | Призначення |
|---|---|---|
| `display.banner` | `string<32>` | поточний текст банера (для WebUI/MQTT) |
| `display.banner_level` | `int32` | 0/1/2 = INFO/WARN/ALARM |

Це дзеркало для зовнішніх споживачів — OSD керується чергою, а не цими ключами.

---

## Наслідки

**Позитив:**
- Без втрат і з пріоритетами для критичних сповіщень; decoupling продюсер↔OSD збережено.
- Нуль нової інфраструктури транспорту — лише новий `msg_id::SYSTEM_NOTICE`, struct і override `on_message`.
- Host-тестується тривіально: інжектувати `on_message(MsgSystemNotice{…})`, перевірити вибір банера/таймери
  (FreeRTOS піднімати не треба — як у `test_equipment.cpp`).
- Узгоджено з наявним каналом WebUI/MQTT через дзеркало в SharedState.

**Негатив / на що зважати:**
- `on_message` **синхронний** на стеку продюсера → у ньому **тільки `push`**, жодного I²C/рендеру (інакше блок
  продюсера + ризик реентрабельності `publish`).
- Catch-all router: Display отримує **всі** повідомлення → обов'язково фільтрувати по `get_message_id()` і
  викликати `BaseModule::on_message(msg)`.
- TTL рахувати накопиченням `dt_ms` (монотонний один таск), не мішати з `esp_timer`.
- **Не ISR-safe** (свідомо): якщо колись з'явиться апаратний alarm-пін через GPIO ISR — відступити до варіанта C
  (`xQueueSendFromISR` у статичну чергу, Display дренує її в `on_update`), решта логіки D незмінна.

---

## Що змінити в §6.3 design-доку (`AMT630A_driver_design.md`)

§6.3 **п.3 «Сповіщення»** наразі стверджує: «`DisplayModule` не має шини повідомлень … транспорт — НЕ через
`msg_id`, а через SharedState-polling». Це **фактично невірно** і має бути замінено:

1. **Виправити твердження:** `DisplayModule` *успадковує* `on_message` від `BaseModule` і **вже підписаний** на
   `etl::message_bus` через catch-all `ModuleAdapter` (реєструється `modesp_register_modules`). Перевизначення
   просто відсутнє — додати його.
2. **Замінити транспорт:** SharedState-polling (`display.notify*`) → **подія `MsgSystemNotice` на наявну шину**
   (новий `msg_id::SYSTEM_NOTICE` у діапазоні 50-99); priority-черга + TTL у `DisplayModule`.
3. **SharedState лишається дзеркалом** (`display.banner`, `display.banner_level`) для WebUI/MQTT, а не єдиним
   джерелом для OSD-подій.
4. Узгодити §6.2 `AMT630ARenderer::notify(text, level)` — сигнатура збігається; додати «зняття = порожній текст».
5. (Опц.) §5(a) — `level` → індекс палітри (`ALARM=color1 червоний`, `WARN`=жовтий, `INFO`=зелений/білий).
