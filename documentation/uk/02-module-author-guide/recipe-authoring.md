# Написання рецептів

> 📖 **In English:** [documentation/en/02-module-author-guide/recipe-authoring.md](../../en/02-module-author-guide/recipe-authoring.md)

**Рецепт** — це обмежений у часі процес — програма приготування, цикл
реактора, послідовність поливу, процедура відтайки — закодований як
декларативна машина станів у секції `scenario` вашого маніфесту. Конвеєр
збірки компілює його у бінарний `.modr`-блоб; рушій сценаріїв виконує
його у середовищі виконання. C++ не потрібен.

Ця сторінка пояснює, як структурувати сценарії — треки, фази, переходи,
правила завершення — і як конвеєр «рецепт-як-маніфест» потрапляє на
пристрій. Про вбудовані та користувацькі дії всередині фаз дивіться
[recipe-actions.md](recipe-actions.md). Про контролери PID, гістерезису
та лінійної зміни, що працюють всередині фаз, дивіться
[continuous-behaviors.md](continuous-behaviors.md).

## Рецепт = маніфест із секцією `scenario`

Модуль-рецепт має ту саму форму `manifest.json`, що й сервісний модуль,
окрім:

- `"module_type": "recipe"` повідомляє конвеєру збірки пропустити
  генерацію C++-коду.
- Нова секція `"scenario"` містить декларацію FSM.
- Жодного `CMakeLists.txt`, `src/`, `include/`. Лише маніфест.

```
modules/my_recipe/
└── manifest.json          ← увесь модуль
```

Під час збірки `tools/compile_scenario.py` читає секцію `scenario` і
формує `data/scenarios/<recipe_name>.modr` (двійковий файл із перевіркою
CRC). LittleFS включає його в образ; рушій завантажує під час виконання
через `engine.load_path("/data/scenarios/<recipe_name>.modr")`.

## Ментальна модель: треки і фази

```
   Scenario
   ├── Track 1 ("main")
   │   ├── Phase A → Phase B → Phase C → $complete
   │   └── (entry actions, transitions, exit actions per phase)
   ├── Track 2 ("watcher")
   │   └── Phase X → $complete
   └── (optional) global transitions to $abort
```

**Треки** — це паралельні машини станів, які виконуються одночасно.
Кожен трек має власну фазу і просувається незалежно. Треки спілкуються
через SharedState (записи з одного треку стають доступними для читання
іншим у наступному такті).

**Фази** — це вузли FSM треку. Фаза має:
- `entry` дії — виконуються при вході у фазу (по одній на такт);
- `transitions` — умови переходу до іншої фази;
- `exit` дії — виконуються при виході (перед застосуванням цілі переходу);
- Необов'язковий `timeout_ms` — автоматичний перехід до наступної фази
  після спливання часу;
- Необов'язковий `phase_resources` — захоплення ресурсів на час фази.

**Цілі** переходів:
- Інше ім'я фази (`"to": "phase_b"`) — просування у межах того ж треку;
- `"$complete"` — трек успішно завершився (стан треку → COMPLETED);
- `"$abort"` — трек завершився невдало (стан треку → FAILED, сценарій
  може перерватися).

## Мінімальний рецепт

```json
{
  "manifest_version": 1,
  "module": "my_recipe",
  "module_type": "recipe",
  "version": "1.0.0",
  "description": "Demo recipe",

  "state": {
    "my_recipe.scenario_state":   {"type": "string", "access": "read"},
    "my_recipe.scenario_elapsed_s": {"type": "int",  "access": "read"},
    "my_recipe.main_state":       {"type": "string", "access": "read"},
    "my_recipe.main_phase_name":  {"type": "string", "access": "read"},
    "my_recipe.main_phase_idx":   {"type": "int",    "access": "read"},
    "my_recipe.main_elapsed_s":   {"type": "int",    "access": "read"}
  },

  "scenario": {
    "default_phase_timeout_ms": 30000,
    "completion_rule": "all_tracks_complete",
    "tracks": [
      {
        "name": "main",
        "flags": ["main_track"],
        "phases": [
          {
            "name": "warmup",
            "entry": [
              {"action": "log", "params": {"msg": "warming up"}}
            ],
            "transitions": [
              {"to": "soak", "when": {"time_elapsed_ms": 5000}}
            ]
          },
          {
            "name": "soak",
            "entry": [
              {"action": "log", "params": {"msg": "soaking"}}
            ],
            "transitions": [
              {"to": "$complete", "when": {"time_elapsed_ms": 10000}}
            ]
          }
        ]
      }
    ]
  }
}
```

Запуск: завантажте через `POST /api/scenario/load`, стартуйте через
`POST /api/scenario/start`. Рушій проганяє трек `main` через `warmup`
(5 с) → `soak` (10 с) → `$complete`. Дзеркальні ключі
`my_recipe.main_phase_name` тощо оновлюються в реальному часі.

## Обов'язкові дзеркальні ключі стану

Рушій записує дзеркальні ключі у SharedState кожен такт. Вони МУСЯТЬ
бути попередньо оголошені у секції `state` рецепта, інакше збірка
впаде з помилкою перехресної валідації.

На рівні сценарію:

| Ключ | Тип | Опис |
|---|---|---|
| `<recipe>.scenario_state` | string | `"idle"`/`"loaded"`/`"running"`/`"paused"`/`"aborting"`/`"completed"`/`"failed"` |
| `<recipe>.scenario_elapsed_s` | int | Секунди від старту сценарію |

На рівні треку:

| Ключ | Тип | Опис |
|---|---|---|
| `<recipe>.<track>_state` | string | Стан FSM треку |
| `<recipe>.<track>_phase_name` | string | Ім'я поточної фази |
| `<recipe>.<track>_phase_idx` | int | Індекс фази у треку (з 0) |
| `<recipe>.<track>_elapsed_s` | int | Секунди у поточній фазі |

Для рецепта з треками `main` і `watcher` оголосіть 2 + (2 × 4) = 10
дзеркальних ключів у секції state.

**Бюджет імен:** ключі SharedState ≤ 32 символів. Ім'я рецепта ≤ 12,
ім'я треку ≤ 8 символів. Приклад: `recipe_plov.watcher_phase_name` =
30 символів — вкладається.

## Поля рівня сценарію

```json
"scenario": {
  "default_phase_timeout_ms": 30000,
  "scenario_timeout_max_ms": 120000,
  "completion_rule": "all_tracks_complete",
  "resources": [],
  "global_transitions": [],
  "tracks": [...]
}
```

| Поле | Обов'язкове | Примітки |
|---|---|---|
| `default_phase_timeout_ms` | так | Типовий таймаут фази (обов'язковий згідно з ADR-0007). Фази можуть перевизначати індивідуально. |
| `scenario_timeout_max_ms` | рекомендовано | Жорстке обмеження загальної тривалості сценарію. Рушій перериває при перевищенні. |
| `completion_rule` | так | `"all_tracks_complete"` / `"any_track_complete"` / `"main_track_complete"` — коли сам сценарій завершується? |
| `resources` | необов'язкове | Захоплення ресурсів на рівні сценарію (див. нижче). |
| `global_transitions` | необов'язкове | Умови, що обчислюються ПЕРШИМИ кожен такт, до переходів на рівні треку. |
| `tracks` | так | Масив визначень треків (1-6). |

## Треки

```json
{
  "name": "main",                          // ≤ 8 символів, snake_case
  "flags": ["main_track"],                 // необов'язкові прапорці
  "initial_phase": 0,                      // типове 0 (перша фаза)
  "phases": [...]                          // масив визначень фаз
}
```

| Прапорець | Ефект |
|---|---|
| `"main_track"` | Позначає головний трек. Використовується `completion_rule: "main_track_complete"` і детектором збою на рівні сценарію. Цей прапорець повинен мати рівно один трек. |
| `"loop_on_complete"` | Коли трек досягає `$complete`, повторно входить у початкову фазу. Використовується для довгограючих моніторів. |

## Фази

```json
{
  "name": "phase_a",                       // унікальне у межах треку, snake_case
  "timeout_ms": 10000,                     // необов'язкове, перевизначає default_phase_timeout_ms
  "phase_resources": [],                   // необов'язкове
  "entry": [...],                          // необов'язкове — дії при вході у фазу
  "transitions": [...],                    // обов'язкове — щонайменше 1
  "exit": []                               // необов'язкове — дії при виході з фази
}
```

### Дії entry і exit

Виконуються послідовно, по одній на такт. Рушій запускає `entry[0]` у
першому такті після початку фази, `entry[1]` — у наступному тощо. Коли
всі entry-дії завершилися, рушій починає обчислювати переходи.

Exit-дії запускаються при спрацюванні переходу, перед тим як цільова
фаза стає активною. Та сама посекторна послідовність.

```json
"entry": [
  {"action": "log",       "params": {"msg": "Phase A started"}},
  {"action": "set_state", "params": {"key": "test.led", "type": "bool", "value": true}}
],
"exit": [
  {"action": "set_state", "params": {"key": "test.led", "type": "bool", "value": false}}
]
```

Повний каталог дій — у [recipe-actions.md](recipe-actions.md).

### Переходи

```json
"transitions": [
  {"to": "phase_b", "when": {"time_elapsed_ms": 5000}},
  {"to": "$abort",  "when": {"state_key_gt": {"key": "test.fault", "value": 0}}}
]
```

Обчислюються у порядку оголошення кожен такт. Перший, що спрацював,
перемагає. Як тільки перехід спрацював, рушій виконує `exit`-дії
поточної фази, потім застосовує ціль.

Спеціальні цілі:
- `"$complete"` — трек успішно завершився (стан COMPLETED);
- `"$abort"` — трек завершився невдало (стан FAILED);
- `"phase_name"` — перехід до конкретної фази.

### Види переходів (форми `when`)

Конструкція `when` може мати такі форми:

**Лише час:**
```json
{"time_elapsed_ms": 5000}
```
Спрацьовує, коли `phase_elapsed_ms >= 5000`.

**Лише умова:**
```json
{"state_key_gt": {"key": "equipment.air_temp", "value": 25}}
```
Спрацьовує, коли умова обчислюється у true. Умови читають SharedState
у реальному часі.

**Час І умова** (вид `time_and_cond`, кодується як композит):
```json
{"all_of": [
  {"time_elapsed_ms": 5000},
  {"state_key_gt": {"key": "equipment.air_temp", "value": 25}}
]}
```
Обидві умови мають виконуватися.

**Час АБО умова** (вид `time_or_cond`):
```json
{"any_of": [
  {"time_elapsed_ms": 30000},
  {"state_key_eq": {"key": "user.skip", "value": true}}
]}
```
Будь-яка спрацьовує.

**Безумовний** (пропустіть `when`):
```json
{"to": "$complete"}
```
Спрацьовує негайно. Використовується після завершення entry-дій —
природне просування.

### Каталог умов

Дивіться [recipe-actions.md → Conditions](recipe-actions.md#conditions)
для повного списку. Поширені:

| Умова | Призначення |
|---|---|
| `time_elapsed_ms` | phase_elapsed_ms ≥ порогу |
| `state_key_eq` / `_ne` | точна відповідність / невідповідність |
| `state_key_gt` / `_lt` / `_ge` / `_le` | числові порівняння |
| `state_key_in_range` | перевірка включних меж |
| `state_key_changed` | детекція фронту (Stage 1.5) |
| `all_of` / `any_of` / `not` | композитна логіка |

## Правила завершення

Коли сам сценарій досягає термінального стану?

| Правило | Тригер |
|---|---|
| `all_tracks_complete` | Кожен трек повинен бути COMPLETED. |
| `any_track_complete` | Перший трек, що став COMPLETE, перемагає; решта треків перериваються. |
| `main_track_complete` | Має значення лише трек із прапорцем `main_track`. |

Обробка збоїв:
- Якщо головний трек переходить у FAILED, сценарій → FAILED незалежно
  від completion_rule.
- Якщо `completion_rule: "main_track_complete"` і головний трек впав →
  сценарій FAILED.
- Падіння інших треків не обов'язково валить сценарій (залежить від
  правила).

## Глобальні переходи

Обчислюються ПЕРШИМИ кожен такт, до логіки на рівні треку.
Використовуються для шаблонів «перервати, якщо виявлено несправність».

```json
"global_transitions": [
  {
    "to": "$abort",
    "when": {"state_key_eq": {"key": "test.fault", "value": true}},
    "priority": 255,
    "scope": "abort_scenario"
  }
]
```

| Поле | Примітки |
|---|---|
| `to` | Значущим є лише `"$abort"` (інші цілі ігноруються). |
| `when` | Той самий синтаксис, що й у переходах треку. |
| `priority` | 0-255. Вищий спрацьовує першим, якщо збігається кілька. |
| `scope` | `"abort_scenario"` (усі треки падають) або `"abort_only_main_track"` (падає лише головний, далі вирішує completion_rule). |

Використовуйте ощадливо — глобальні переходи призначені для швидких
перевірок безпеки. Більшість логіки живе у переходах треків.

## Ресурси (область сценарію і фази)

Ресурси не дають двом сценаріям одночасно керувати одним і тим же
актуатором.

**Область сценарію** (захоплюється при `start`, звільняється у кінці
сценарію):

```json
"resources": [
  {"resource_hash": "compressor", "exclusive": true}
]
```

Рушій намагається атомарно захопити всі ресурси при `start`. Якщо
будь-який з них уже зайнятий, `start` повертає `RESOURCE_CONTENDED` і
сценарій залишається у стані LOADED.

**Область фази** (захоплюється при вході у фазу, звільняється при
виході):

```json
{
  "name": "active_phase",
  "phase_resources": [
    {"resource_hash": "compressor", "exclusive": true}
  ],
  "transitions": [...]
}
```

Якщо ресурс фази зайнятий, трек переходить у стан
`WAITING_FOR_RESOURCE` і повторює спробу кожен такт. Таймаут фази при
цьому продовжує діяти.

Повні деталі: [scenario-engine/06_resource_arbitration.md](../03-framework-reference/scenario-engine/06_resource_arbitration.md).

## Синхронізація між треками

Треки тикають у порядку оголошення кожне оновлення рушія. У межах
одного такту:
- Трек 0 читає / пише стан.
- Трек 1 (пізніший) читає свіжі дані — включно зі змінами, які трек 0
  зробив у цьому самому такті.

Це **семантика порядку тактів** (ADR-0003). Оголошуйте треки-продюсери
перед треками-споживачами. Не покладайтеся на консистентність знімків
між треками.

Робочий шаблон: трек `watcher` чекає, поки `phase_name` головного
треку досягне певного значення, потім завершується.

```json
{
  "name": "watcher",
  "phases": [{
    "name": "watching",
    "transitions": [
      {"to": "$complete", "when": {"state_key_eq": {
        "key": "my_recipe.main_phase_name", "value": "phase_c"
      }}}
    ]
  }]
}
```

Рушій записує дзеркальний ключ `my_recipe.main_phase_name` після такту
головного треку. Watcher (оголошений після main) читає його і може
спрацювати перехід у тому ж такті.

## Параметри і динамічні значення

Автори рецептів можуть оголошувати **перевизначувані параметри**, які
оператор налаштовує перед запуском сценарію. Визначаються у секції
`scenario`:

```json
"parameters": {
  "warm_setpoint": {"type": "float", "default": 30.0, "min": 10, "max": 50},
  "soak_duration_ms": {"type": "i32", "default": 30000, "min": 5000, "max": 300000}
}
```

Посилання у діях — через `@param:<name>`:

```json
{"action": "set_state", "params": {
  "key": "equipment.req_setpoint",
  "type": "f32",
  "value": "@param:warm_setpoint"
}}
```

Компілятор перетворює `@param:warm_setpoint` в індекс таблиці
параметрів; рушій підставляє значення (з можливим перевизначенням під
час виконання) при виконанні фази.

Дивіться [scenario-engine/02_binary_format.md](../03-framework-reference/scenario-engine/02_binary_format.md)
для формату на проводі.

## Збірка і завантаження

```bash
# 1. Збірка прошивки (compile_scenario.py запускається як pre-build)
idf.py build

# 2. Файл .modr з'являється у:
#    build/data/scenarios/<recipe_name>.modr
#    і вбудовується у образ LittleFS

# 3. Прошивка:
idf.py -p COM15 flash

# 4. Завантаження і запуск на пристрої:
curl -u admin:modesp -X POST http://192.168.1.85/api/scenario/load \
     -d '{"path": "/data/scenarios/my_recipe.modr"}'
# → {"handle": 1}

curl -u admin:modesp -X POST http://192.168.1.85/api/scenario/start \
     -d '{"handle": 1}'
```

WebUI пропонує ті самі елементи керування на автогенерованій сторінці
рецепта (використовуйте картки `visible_when`, щоб показувати елементи
керування лише коли сценарій завантажений або працює).

## Робочий процес для написання нового рецепта

1. **Накидайте треки і фази на папері.** Що працює одночасно? Яка
   лінійна послідовність у межах кожного треку?
2. **Визначте дзеркальні ключі.** Ім'я рецепта + імена треків + ключі
   стану сценарію/треку. Вкладіться в бюджет (рецепт ≤ 12, трек ≤ 8).
3. **Запишіть секцію `state`** з дзеркальними ключами.
4. **Запишіть `scenario.tracks`** із entry-діями і переходами.
5. **Додайте `default_phase_timeout_ms`** і виберіть правило завершення.
6. **Скомпілюйте** — `python tools/compile_scenario.py modules/my_recipe`.
   Ітеруйте по помилках (компілятор вказує на конкретні рядки маніфесту).
7. **Прошийте і протестуйте на залізі** через ендпоінти `/api/scenario/*`.
8. **Ітеруйте** — налаштуйте тайминги, додайте умови, доопрацюйте UI.

## Типові помилки

**Відсутні оголошення дзеркальних ключів стану:** рушій намагається
записати `<recipe>.main_state`, але секція `state` маніфесту його не
оголошує. Компіляція падає з чітким повідомленням «mirror key X not
declared in state».

**Занадто довге ім'я рецепта:** бюджет — ≤ 12 символів усього.
`refrigeration_master` має 20 — не вкладається. Використовуйте коротші
коди: `refrig_v1`.

**`$abort` з переходу фази не перериває сценарій:** `$abort` на рівні
треку переводить трек у FAILED. Інші треки продовжують. Щоб перервати
весь сценарій, використовуйте `global_transitions` зі
`scope: "abort_scenario"`.

**Забутий таймаут фази:** якщо жодний перехід не спрацьовує і
`timeout_ms` не встановлено, фаза працює вічно (точніше, до
`scenario_timeout_max_ms`). Завжди встановлюйте таймаути, навіть якщо
умови мають завжди спрацьовувати — захист у глибину.

**Припущення про гонку між треками:** «Трек A пише, трек B читає — вони
відбуваються одночасно». Ні. Треки тикають послідовно. Оголошуйте
продюсерів перед споживачами; очікуйте затримки в один такт, якщо
порядок зворотний.

**Вбудовані умови, записані як у Python:** `{"state_key_eq": "test.x == 5"}`
не працює. Умови — це структурований JSON, а не вирази:
`{"state_key_eq": {"key": "test.x", "value": 5}}`.

## Що далі

- **[recipe-actions.md](recipe-actions.md)** — вбудовані дії (`log`,
  `set_state`, `wait_ms`) і реєстрація користувацьких.
- **[continuous-behaviors.md](continuous-behaviors.md)** — контролери
  PID, гістерезису, лінійної зміни, що працюють всередині фаз.
- **[scenario-engine/04_state_machines.md](../03-framework-reference/scenario-engine/04_state_machines.md)** —
  діаграми FSM на рівні треку і сценарію.
- **[scenario-engine/05_synchronization.md](../03-framework-reference/scenario-engine/05_synchronization.md)** —
  глибоке занурення у синхронізацію між треками з обґрунтуванням ADR-0003.
- **[scenario-engine/06_resource_arbitration.md](../03-framework-reference/scenario-engine/06_resource_arbitration.md)** —
  семантика захоплення ресурсів.

## Робочий приклад: `modules/abs_test`

Еталонний рецепт, що постачається з фреймворком. Два паралельні треки:
- `main`: phase_a → phase_b → phase_c → $complete (≈6 секунд);
- `watcher`: чекає на `main.main_phase_name == "phase_c"`, потім
  завершується.

Демонструє: entry-дії, умовні переходи з композитом `all_of`,
синхронізацію між треками через дзеркальні ключі, ціль `$complete`.

Джерело: [`modules/abs_test/manifest.json`](../../../modules/abs_test/manifest.json).
