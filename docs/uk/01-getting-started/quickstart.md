# Швидкий старт

> 📖 **In English:** [docs/en/01-getting-started/quickstart.md](../../en/01-getting-started/quickstart.md)

Мета: прошити ModESP на реальний ESP32, відкрити WebUI, завантажити і
запустити reference scenario `abs_test` через HTTP API — за менш ніж 10
хвилин.

Цей гайд припускає що у вас вже встановлений ESP-IDF v5.0+. Якщо ні —
спочатку прочитайте [installation.md](installation.md).

## Що знадобиться

- **ESP32-WROOM-32** dev board (інші варіанти типу S3 / C3 працюють але
  не калібровані під 65 KB heap budget).
- **USB кабель** (data, не лише power).
- IP вашого домашнього Wi-Fi і SSID/password під рукою.
- Активований ESP-IDF environment у вашій shell (`$IDF_PATH` встановлений,
  tools у PATH, Python venv активний).

## Крок 1 — Build і flash

```bash
git clone https://github.com/Zapadenec1982/ModESP_Framework
cd ModESP_Framework

# Build (перший раз ~5 хвилин; наступні incremental < 30 секунд)
idf.py build

# Flash + monitor на COM port (Windows). Замініть на ваш serial port.
idf.py -p COM15 flash monitor
```

На Linux/macOS порт зазвичай `/dev/ttyUSB0` або `/dev/ttyACM0`. Натисніть
`Ctrl+]` щоб вийти з monitor.

Має побачити boot логи що закінчуються чимось на кшталт:

```
I (12345) main: Phase 3: Initializing HTTP + WebSocket...
I (12350) wifi_service: connected, IP=192.168.1.85
I (12355) http: HTTP server started on port 80
```

Запам'ятайте IP — він знадобиться.

## Крок 2 — Налаштувати Wi-Fi (якщо ще не прошитий)

При першому boot пристрій піднімає Wi-Fi AP з ім'ям `ModESP-XXXXXX`
(суфікс MAC). Підключіться (пароль `12345678` за замовчуванням), відкрийте
[http://192.168.4.1/](http://192.168.4.1/) і введіть credentials вашого
домашнього Wi-Fi. Пристрій ребутає і приєднується до мережі.

Після приєднання watch monitor log щоб побачити присвоєний IP.

## Крок 3 — Відкрити WebUI

У браузері перейдіть на `http://<device-ip>/`. Має побачити landing-сторінку
ModESP з system pages (Dashboard, Network, Firmware, System) і сторінкою
"Тест" від bundled рецепту `abs_test`.

**Default credentials** для HTTP API і protected endpoints: `admin` /
`modesp`. Змінити можна на сторінці System → Auth.

## Крок 4 — Запустити reference scenario

`abs_test` — мінімальний two-track scenario що поставляється з прошивкою.
Main track циклить через `phase_a → phase_b → phase_c → $complete`; watcher
track чекає поки main увійде у `phase_c`, тоді завершується. Використовуйте
його щоб перевірити що scenario engine живий.

```bash
# Замініть 192.168.1.85 на ваш device IP по всьому файлу.
# Credentials default до admin/modesp (HTTP Basic Auth).

# 1. Завантажити .modr з LittleFS
curl -u admin:modesp -X POST http://192.168.1.85/api/scenario/load \
     -H "Content-Type: application/json" \
     -d '{"path": "/data/scenarios/abs_test.modr"}'
# → {"handle": 1}

# 2. Запустити
curl -u admin:modesp -X POST http://192.168.1.85/api/scenario/start \
     -H "Content-Type: application/json" \
     -d '{"handle": 1}'
# → {"ok": true}

# 3. Наглядати — mirror keys у /api/state live-update
curl -u admin:modesp http://192.168.1.85/api/state | python -m json.tool | grep abs_test
# {
#   "abs_test.scenario_state": "running",
#   "abs_test.scenario_elapsed_s": 3,
#   "abs_test.main_phase_name": "phase_b",
#   ...
# }

# 4. Зачекати ~7 секунд для completion, потім перевірити
curl -u admin:modesp "http://192.168.1.85/api/scenario/info?handle=1"
# → {"state":"completed", "elapsed_s":7, "tracks":[...]}

# 5. Unload щоб звільнити slot
curl -u admin:modesp -X POST http://192.168.1.85/api/scenario/unload \
     -H "Content-Type: application/json" \
     -d '{"handle": 1}'
```

Сторінка **Тест** у WebUI показує той самий state у реальному часі через
WebSocket updates — перейдіть на неї під час кроків 2-4 і дивіться як
`main_phase_name` / `watcher_phase_name` advance-ять live.

## Що щойно сталося?

- **`abs_test.modr`** скомпілювався при build через `tools/compile_scenario.py`
  з [`modules/abs_test/manifest.json`](../../../modules/abs_test/manifest.json)
  і запакувався у LittleFS image (`data/scenarios/`).
- **HTTP `/api/scenario/load`** викликав `Engine::load_path()` що читає
  файл, валідує magic + CRC + action hashes, паркує у slot.
- **HTTP `/api/scenario/start`** викликав `Engine::start()` що ініціалізує
  обидва tracks і transition-ить scenario у `running`.
- **Engine tick (100 Hz)** крокував обидва tracks через їхні phase
  machines. Mirror keys писалися кожен tick через `mirror::publish()` тому
  WebUI бачив live state.
- **Track 2 (watcher)** чекав на умову
  `state_key_eq{key:"abs_test.main_phase_name", value:"phase_c"}` —
  cross-track sync приклад використовуючи лише SharedState як rendezvous.

## Що далі

- Прочитайте **[concepts.md](concepts.md)** для чотирьох ключових ідей
  (manifest-driven, modules, scenarios, SharedState).
- Прочитайте **[Module Author Guide → overview.md](../02-module-author-guide/overview.md)**
  щоб почати писати власний модуль.
- Прочитайте **[scenario-engine/00_overview.md](../03-framework-reference/scenario-engine/00_overview.md)**
  для глибшого огляду recipe runtime.

## Troubleshooting

**Пристрій boot-ить але Wi-Fi ніколи не конектиться:** перевірте monitor
log на `wifi_service: SSID="..." not found` або auth failures. Повторно
введіть credentials через AP fallback (довге натискання boot button щоб
форсувати AP mode).

**`load` повертає 400 з `"error": "invalid_file"`:** .modr ймовірно не
включився у LittleFS image. Перевірте у `idf.py build` output чи
`data/scenarios/abs_test.modr` запакований (шукайте "Adding File:
scenarios\\abs_test.modr"). Якщо відсутній — перезапустіть
`python tools/compile_scenario.py` і rebuild.

**`load` повертає 401 Unauthorized:** немає HTTP Basic Auth — додайте
`-u admin:modesp` до вашої curl команди, або спочатку змініть credentials
через сторінку System → Auth.

**`start` повертає 400 `"resource_contended"`:** інший scenario тримає той
самий resource. `abs_test` не декларує resources тому це не повинно
траплятися; якщо трапилося — перевірте `/api/scenario/list` і unload
все ще завантажене.

**Mirror keys застрягли на `idle` після start:** scenario engine не
тикався. Перевірте у monitor `Phase 2: Initializing WiFi + business
modules...` і подальші registration messages включно з `scenario`. Якщо
відсутні — `scenario_engine` не зареєстрований у `main.cpp` (Phase 3
wiring відсутній — заведіть багу).
