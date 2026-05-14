# Швидкий старт

> 📖 **In English:** [docs/en/01-getting-started/quickstart.md](../../en/01-getting-started/quickstart.md)

Мета: прошити прошивку на основі ModESP на реальний ESP32, побачити WebUI
в реальному часі, завантажити й запустити еталонний сценарій `abs_test`
через HTTP API — менш ніж за 10 хвилин.

Цей посібник передбачає, що у вас уже встановлений ESP-IDF v5.0+. Якщо
ні — спершу прочитайте [installation.md](installation.md).

## Що знадобиться

- Плата розробника **ESP32-WROOM-32** (інші варіанти на кшталт S3 / C3
  працюють, але не відкалібровані під бюджет купи 65 КБ).
- **USB кабель** (з даними, а не лише для живлення).
- IP-адреса вашого домашнього Wi-Fi та готові SSID/пароль.
- Активоване середовище ESP-IDF у вашій оболонці (`$IDF_PATH`
  встановлений, інструменти в PATH, Python venv активний).

## Крок 1 — Збирання та прошивка

```bash
git clone https://github.com/Zapadenec1982/ModESP_Framework
cd ModESP_Framework

# Build (first run takes ~5 minutes; subsequent < 30 seconds incremental)
idf.py build

# Flash + monitor on COM port (Windows). Replace з your serial port.
idf.py -p COM15 flash monitor
```

На Linux/macOS порт — це `/dev/ttyUSB0` або `/dev/ttyACM0`. Натисніть
`Ctrl+]`, щоб вийти з монітора.

Ви маєте побачити логи завантаження, які закінчуються приблизно так:

```
I (12345) main: Phase 3: Initializing HTTP + WebSocket...
I (12350) wifi_service: connected, IP=192.168.1.85
I (12355) http: HTTP server started on port 80
```

Запам'ятайте IP-адресу — вона знадобиться.

## Крок 2 — Налаштування Wi-Fi (якщо ще не прошито)

При першому завантаженні пристрій піднімає Wi-Fi точку доступу з ім'ям
`ModESP-XXXXXX` (суфікс MAC). Підключіться до неї (пароль за
замовчуванням `12345678`), потім відкрийте
[http://192.168.4.1/](http://192.168.4.1/) і введіть облікові дані
вашого домашнього Wi-Fi. Пристрій перезавантажиться й приєднається до
вашої мережі.

Після приєднання дивіться в лог монітора, щоб побачити присвоєну
IP-адресу.

## Крок 3 — Відкриття WebUI

У браузері перейдіть на `http://<device-ip>/`. Ви маєте побачити стартову
сторінку ModESP із системними сторінками (Dashboard, Network, Firmware,
System) та сторінкою "Тест" від вбудованого рецепту `abs_test`.

**Облікові дані за замовчуванням** для HTTP API та захищених точок:
`admin` / `modesp`. Змініть їх на сторінці System → Auth.

## Крок 4 — Запуск еталонного сценарію

`abs_test` — це мінімальний сценарій з двома треками, який постачається
разом з прошивкою. Головний трек циклічно проходить
`phase_a → phase_b → phase_c → $complete`; трек-спостерігач чекає, поки
головний увійде у `phase_c`, а потім завершується. Використовуйте його,
щоб перевірити, що рушій сценаріїв працює.

```bash
# Replace 192.168.1.85 з your device IP throughout.
# Credentials default to admin/modesp (HTTP Basic Auth).

# 1. Load the .modr from LittleFS
curl -u admin:modesp -X POST http://192.168.1.85/api/scenario/load \
     -H "Content-Type: application/json" \
     -d '{"path": "/data/scenarios/abs_test.modr"}'
# → {"handle": 1}

# 2. Start it
curl -u admin:modesp -X POST http://192.168.1.85/api/scenario/start \
     -H "Content-Type: application/json" \
     -d '{"handle": 1}'
# → {"ok": true}

# 3. Watch progress — mirror keys у /api/state are live-updated
curl -u admin:modesp http://192.168.1.85/api/state | python -m json.tool | grep abs_test
# {
#   "abs_test.scenario_state": "running",
#   "abs_test.scenario_elapsed_s": 3,
#   "abs_test.main_phase_name": "phase_b",
#   ...
# }

# 4. Wait ~7 seconds for completion, then check
curl -u admin:modesp "http://192.168.1.85/api/scenario/info?handle=1"
# → {"state":"completed", "elapsed_s":7, "tracks":[...]}

# 5. Unload to free the slot
curl -u admin:modesp -X POST http://192.168.1.85/api/scenario/unload \
     -H "Content-Type: application/json" \
     -d '{"handle": 1}'
```

Сторінка **Тест** у WebUI показує той самий стан у реальному часі через
оновлення WebSocket — перейдіть на неї під час кроків 2–4 і
спостерігайте, як `main_phase_name` / `watcher_phase_name` змінюються
наживо.

## Що щойно сталося?

- **`abs_test.modr`** був скомпільований під час збирання інструментом
  `tools/compile_scenario.py` з
  [`modules/abs_test/manifest.json`](../../../modules/abs_test/manifest.json)
  і запакований у образ LittleFS (`data/scenarios/`).
- **HTTP `/api/scenario/load`** викликав `Engine::load_path()`, який
  читає файл, перевіряє magic + CRC + хеші дій і паркує його у слот.
- **HTTP `/api/scenario/start`** викликав `Engine::start()`, який
  ініціалізує обидва треки й переводить сценарій у стан `running`.
- **Такт рушія (100 Гц)** просунув обидва треки через їхні фазові
  машини. Дзеркальні ключі записувалися щотакту функцією
  `mirror::publish()`, тож WebUI бачив стан у реальному часі.
- **Трек 2 (спостерігач)** чекав на умову
  `state_key_eq{key:"abs_test.main_phase_name", value:"phase_c"}` —
  приклад крос-трекової синхронізації, що використовує лише SharedState
  як точку рандеву.

## Що далі

- Прочитайте **[concepts.md](concepts.md)**, щоб ознайомитися з чотирма
  ключовими ідеями (керування маніфестами, модулі, сценарії,
  SharedState).
- Прочитайте **[Module Author Guide → overview.md](../02-module-author-guide/overview.md)**,
  щоб почати писати власний модуль.
- Прочитайте **[scenario-engine/00_overview.md](../03-framework-reference/scenario-engine/00_overview.md)**,
  щоб глибше зрозуміти середовище виконання рецептів.

## Типові помилки

**Пристрій завантажується, але Wi-Fi так і не підключається:**
перевірте лог монітора на наявність `wifi_service: SSID="..." not found`
або помилок автентифікації. Введіть облікові дані повторно через
аварійну точку доступу (довге натискання кнопки boot, щоб примусово
перейти в режим AP).

**`load` повертає 400 з `"error": "invalid_file"`:** ймовірно, `.modr`
не потрапив до образу LittleFS. Перевірте у виводі `idf.py build`, чи
запакований `data/scenarios/abs_test.modr` (шукайте "Adding File:
scenarios\\abs_test.modr"). Якщо його немає, перезапустіть
`python tools/compile_scenario.py` і пересоберіть.

**`load` повертає 401 Unauthorized:** відсутня HTTP Basic Auth —
додайте `-u admin:modesp` до своєї команди curl або спершу змініть
облікові дані на сторінці System → Auth.

**`start` повертає 400 `"resource_contended"`:** інший сценарій тримає
той самий ресурс. `abs_test` не оголошує ресурсів, тож такого не має
ставатися; якщо сталося — перевірте `/api/scenario/list` і вивантажте
все, що залишилося завантаженим.

**Дзеркальні ключі застрягли на `idle` після старту:** рушій сценаріїв
не отримав такту. Перевірте в моніторі `Phase 2: Initializing WiFi +
business modules...`, а далі — повідомлення про реєстрацію, серед яких
має бути `scenario`. Якщо його немає, `scenario_engine` не зареєстровано
в `main.cpp` (відсутня прошивка Phase 3 — заведіть баг).
