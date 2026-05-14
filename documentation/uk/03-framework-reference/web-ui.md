# Web UI — архітектура SPA на Svelte

> 📖 **In English:** [documentation/en/03-framework-reference/web-ui.md](../../en/03-framework-reference/web-ui.md)

WebUI — це **односторінкова програма на Svelte 4**, яка повністю
працює у браузері й спілкується з пристроєм через REST-ендпоінти
`/api/*` та WebSocket за адресою `/ws`. Вона **керується маніфестами**:
конвеєр збирання фреймворку формує `ui.json`, який пристрій віддає під
час виконання, а SPA рендерить сторінки, картки й віджети безпосередньо
з цієї схеми. Додавання нового ключа стану або віджета до маніфесту
модуля дає новий елемент UI після перезбирання — без жодних правок
у JavaScript.

Ця сторінка описує архітектуру SPA для контриб'юторів фреймворку та
авторів модулів, яким потрібно розуміти, як їхні декларації маніфесту
перетворюються на UI. Довідник самих віджетів — у
[02-module-author-guide/ui-widgets.md](../02-module-author-guide/ui-widgets.md).

ВИМАГАЄ: Node.js 18+ для розробки. Готовий бандл (`data/www/`)
постачається у розділі даних; перезбирати WebUI потрібно тільки за
необхідності.

## Стек

| Шар | Вибір | Примітки |
|---|---|---|
| Фреймворк | Svelte 4.2 | Реактивність часу компіляції, без віртуального DOM, мінімальний рантайм. |
| Бандлер | Rollup 3 | `@rollup/plugin-{commonjs,node-resolve,terser}`, CSS-only плагін, livereload під час розробки. |
| Стан | Сховища Svelte | `writable` + `derived` зі `svelte/store`. Без Redux, без Vuex. |
| Маршрутизація | Внутрішня | Ідентифікатор сторінки — це змінна в `App.svelte`; deep linking не використовується. |
| i18n | Лінива загрузка JSON | 4 мовні пакети (`uk` за замовчуванням, `en/de/pl` довантажуються на вимогу). |
| Розмір збірки | ~80 КБ gzip | Один бандл з усіма віджетами. |

## Розкладка файлів

```
webui/
├── package.json            ← конфігурація Rollup + залежності svelte
├── rollup.config.js
├── dev-server.js           ← локальна розробка з проксі /api на пристрій
├── scripts/deploy.js       ← gzip бандла → data/www/
├── public/                 ← статичні ресурси (icons, tokens.css)
└── src/
    ├── main.js             ← точка монтування Svelte
    ├── App.svelte          ← кореневий компонент, перемикання сторінок, модалка логіну
    ├── pages/
    │   ├── Dashboard.svelte
    │   ├── DynamicPage.svelte      ← рендерить сторінки з ui.json
    │   ├── BindingsEditor.svelte
    │   └── bindings/                ← підсторінки прив'язок (виявлення OneWire тощо)
    ├── components/
    │   ├── Layout.svelte
    │   ├── Card.svelte
    │   ├── WidgetRenderer.svelte   ← диспетчер: тип віджета → компонент
    │   ├── widgets/                 ← 25 компонентів віджетів
    │   ├── Icon.svelte
    │   ├── Clock.svelte
    │   ├── LoginModal.svelte
    │   └── Toast.svelte
    ├── stores/
    │   ├── state.js        ← дзеркало SharedState + насос WebSocket
    │   ├── ui.js           ← ui.json + перекладені сторінки
    │   ├── i18n.js         ← вибір мови + хелпер `t`
    │   ├── theme.js
    │   ├── toast.js
    │   ├── wifiForm.js
    │   └── mqttForm.js
    ├── lib/
    │   ├── api.js          ← apiGet/apiPost/apiUpload + HTTP Basic auth
    │   ├── websocket.js    ← клієнт WebSocket із автоперепідключенням
    │   ├── settings.js
    │   ├── visibility.js   ← інтерпретатор visible_when
    │   ├── icons.js
    │   ├── chart.js        ← SVG-графік для DataLogger
    │   └── downsample.js
    └── i18n/
        ├── uk.js           ← за замовчуванням (вбудовано)
        ├── en.js
        ├── de.js
        └── pl.js
```

## Сховища — рантаймова модель стану

У SPA сім сховищ Svelte. Вони повністю описують її стан. Компоненти
підписуються через реактивність `$store`.

### `state` — дзеркало SharedState

```js
import { state, stateKey, setStateKey } from './stores/state.js';

// Підписка на всі ключі (рідко потрібно)
$: console.log($state['equipment.air_temp']);

// Підписка на один ключ (бажаний варіант)
const temp = stateKey('equipment.air_temp');
$: console.log($temp);

// Оптимістичне оновлення (записує локальне значення до підтвердження сервера)
setStateKey('thermo.setpoint', 22.5);
```

Реалізація у `stores/state.js`:

```js
export const state = writable({});
export const wsConnected = writable(false);

export function initWebSocket() {
  wsClient = createWebSocket((data) => {
    if ('_ws_connected' in data) {
      wsConnected.set(data._ws_connected);
      return;
    }
    state.update(s => ({ ...s, ...data }));
  });
}
```

Сховище стартує порожнім. `App.svelte` робить одноразовий
`GET /api/state` для початкового знімка, потім `initWebSocket()`
відкриває WS, який стрімить дельти. Кожне повідомлення WS зливається
у `state` через `state.update(s => ({ ...s, ...data }))`.

### `uiConfig` і `pages` — схема з пристрою

```js
import { uiConfig, pages, deviceName, navigateTo } from './stores/ui.js';
```

`loadUiConfig()` отримує `/api/ui` один раз під час старту. Результат
— це згенерований `ui.json`, який `tools/generate_ui.py` робить з
усіх маніфестів модулів. Структура:

```json
{
  "device_name": "ModESP",
  "pages": [
    {
      "id": "thermostat",
      "title": "Thermostat",
      "icon": "thermometer",
      "cards": [
        {
          "title": "State",
          "widgets": [
            {"key": "thermo.temperature", "widget": "value"},
            {"key": "thermo.setpoint",    "widget": "slider"}
          ]
        }
      ]
    }
  ],
  "state_meta": { /* тип, одиниці, min/max для кожного ключа */ }
}
```

`pages` — це `derived` сховище, яке застосовує завантажений мовний
пакет до заголовків, підписів, одиниць та списків опцій. Віджети
отримують перекладену копію без втрати посилань на свій сирий `key`.

### `language` і `langPack` — лінива i18n

`stores/i18n.js` експортує `writable` `language` (один з `'uk'`, `'en'`,
`'de'`, `'pl'`). Значення за замовчуванням — `'uk'`, бо генератор
кладе українські рядки безпосередньо в `ui.json`. Інші мови
довантажуються на вимогу:

```js
language.subscribe(lang => {
  loadLanguagePack(lang);   // fetch /i18n/{lang}.json, set langPack
});
```

Мовні пакети віддає пристрій з `data/www/i18n/<lang>.json`. Кожен
пакет — це плаский словник «ключ→значення». Пошук перекладу спочатку
пробує структурований ключ (`thermo.setpoint.description`), потім
повертається до прямої відповідності «рядок→рядок» (для зворотної
сумісності).

Додавання мови = покласти новий JSON-пакет у розділ даних +
розширити селектор мов. Перезбирати SPA не потрібно.

### Інші сховища

- `theme.js` — перемикач темної/світлої теми, зберігається у
  localStorage.
- `toast.js` — черга toast-сповіщень із автозникненням.
- `wifiForm.js`, `mqttForm.js` — «липкий» стан форм налаштувань, щоб
  користувач не втрачав введені облікові дані при переході між
  сторінками.

## Потік WebSocket

`lib/websocket.js`:

```js
export function createWebSocket(onMessage) {
  let retry = 1000;
  function connect() {
    ws = new WebSocket(`${proto}//${location.host}/ws`);
    ws.onopen = () => { retry = 1000; onMessage({ _ws_connected: true }); };
    ws.onmessage = (e) => onMessage(JSON.parse(e.data));
    ws.onclose = () => {
      onMessage({ _ws_connected: false });
      setTimeout(connect, retry);
      retry = Math.min(retry * 2, 30000);
    };
  }
  connect();
}
```

- **Автоперепідключення** з експоненційною витримкою: 1 с → 2 с →
  4 с → ... → межа 30 с.
- **Мета-події** `{ "_ws_connected": true/false }` проходять через
  той самий колбек, що й дані, тому передплатники можуть показувати
  індикатор відключення без окремого каналу.
- **Жодного вихідного трафіку** за замовчуванням — записи йдуть
  через `apiPost` на `/api/settings`. Це робить шлях WS одностороннім,
  а серверний буфер — малим.

Серверна сторона задокументована у
[components/modesp_net.md](components/modesp_net.md#wsservice--websocket-state-broadcast).

## Клієнт HTTP API (`lib/api.js`)

Чотири експорти: `apiGet`, `apiPost`, `apiUpload`, `needsLogin`.

### Автентифікація

Пристрій використовує HTTP Basic Auth (за замовчуванням
`admin/modesp`). SPA тримає облікові дані у `sessionStorage` під
ключем `modesp_auth`:

```js
export function setAuth(user, pass) {
  const creds = btoa(user + ':' + pass);
  sessionStorage.setItem(AUTH_KEY, creds);
}
```

`apiPost` додає заголовок `Authorization: Basic <creds>` до кожного
запиту. `apiGet` цього не робить — на стороні пристрою GET-запити
для отримання стану публічні (у конфігурації за замовчуванням
автентифікація потрібна лише для записів).

Відповідь 401 встановлює `writable` сховище `needsLogin`; кореневий
`App.svelte` слідкує за ним і показує `LoginModal`. Після успішного
входу модалка викликає `setAuth`, але невдалий запит не повторюється
автоматично — користувач просто повторює свою дію вручну.

### Завантаження файлів

`apiUpload(url, file, onProgress)` — це тонка обгортка над
`XMLHttpRequest`, бо `fetch` не дає прогрес завантаження. Використовується
для OTA-прошивки та завантаження сертифікатів.

## Модель сторінок

```
App.svelte
├── Layout.svelte                ← хедер, бічна панель, перемикання сторінок
│   └── currentPage = 'dashboard' | 'bindings' | <id-сторінки-з-ui.json>
└── (відрендерений маршрут)
    ├── Dashboard.svelte         ← фіксована перша сторінка, картки-зведення
    ├── BindingsEditor.svelte    ← фіксована сторінка конфігурації
    └── DynamicPage.svelte       ← усі сторінки з маніфестів
```

`DynamicPage` шукає запитаний ідентифікатор сторінки у сховищі `pages`
й обходить її картки/віджети, передаючи кожен віджет у
`WidgetRenderer`.

### `WidgetRenderer` — диспетчер віджетів

```svelte
<script>
  export let widget;
  // ... зіставлення widget.widget → компонент Svelte ...
</script>
{#if widget.widget === 'slider'}
  <SliderWidget {widget} />
{:else if widget.widget === 'value'}
  <ValueWidget {widget} />
{:else if ...}
{/if}
```

Зіставлення — це `switch` за полем `widget.widget`. Усі компоненти
віджетів мають однаковий інтерфейс: вони отримують об'єкт визначення
віджета як `widget` і знаходять своє значення (значення) через
`stateKey`.

## Каталог віджетів

25 компонентів у `src/components/widgets/`. Поділяються на чотири
групи:

**Відображення:** `Value`, `Indicator`, `StatusText`, `Chart`,
`MiniChart`. Тільки для читання — візуалізують один ключ стану.

**Введення:** `Slider`, `NumberInput`, `Toggle`, `Select`, `TextInput`,
`PasswordInput`, `DatetimeInput`. Двостороннє зв'язування — на
підтвердження надсилається `POST /api/settings`.

**Дія:** `Button`, `ActionsGrid`. Викликають конкретний HTTP-ендпоінт
без зв'язку зі станом.

**Спеціалізовані:** `WifiScan`, `WifiSave`, `ApSave`, `MqttSave`,
`CloudSave`, `TimeSave`, `TimezoneSelect`, `AuthSave`, `FirmwareUpload`,
`FileUpload`, `CertUpload`, `DefrostToggle`. Кожен поєднує невелику
форму та виділений ендпоінт, тож `WidgetRenderer` може вставити їх
у звичайну картку з простого запису в маніфесті, не описуючи у
маніфесті повну розкладку форми.

Див. [02-module-author-guide/ui-widgets.md](../02-module-author-guide/ui-widgets.md)
— який тип підходить до якого типу стану й які властивості приймає
кожен.

## Обчислення `visible_when`

Записи маніфесту можуть нести вираз `visible_when`, який ховає картку
або віджет, доки умова не виконається:

```json
{
  "title": "Defrost",
  "visible_when": {"thermo.defrost_supported": true},
  "widgets": [...]
}
```

`lib/visibility.js` обчислює ці вирази на стороні клієнта проти
сховища `$state`. Підтримувані оператори: рівність, `not`, `and`,
`or`, плюс скорочення для «список дозволених значень»
(`{"key": ["a", "b"]}` справджується, якщо значення — `"a"` або
`"b"`). Граматика навмисно обмежена — складні предикати належать у
C++ модулі, а не в маніфест.

## Збирання й робочий процес розробки

```bash
cd webui

# встановлення залежностей
npm install

# dev-сервер з hot reload (проксує /api/* і /ws на ESP32)
npm run dev          # ws://192.168.1.85/ws за замовчуванням, переозначення через env

# продакшен-бандл
npm run build        # видає bundle.js + bundle.css

# розгортання: gzip і копіювання у data/www/, готово для `idf.py flash_data`
npm run deploy
```

Готовий бандл закомічений у git (`data/www/`). Більшість авторів
модулів ніколи не перезбирають WebUI — додавання нового ключа стану
або віджета через маніфест працює з наявним бандлом.

WebUI перезбирається лише тоді, коли ви змінюєте самі Svelte-компоненти
(новий тип віджета, правка розкладки/теми, виправлення помилки в
`lib/`).

## Характеристики продуктивності

| Метрика | Значення |
|---|---|
| Розмір бандлу | ~80 КБ gzip (єдиний чанк) |
| Перший рендер | <1 с на типовому домашньому роутері |
| Трафік WS у штатному режимі | <1 КБ/с (лише дельти) |
| Пам'ять | ~5-8 МБ JS-купи у типовій сесії |
| Мобільна сумісність | Так — адаптивна розкладка, сенсорно зручні поля |

SPA навмисно зроблено єдиним бандлом. Code-splitting додав би HTTP
round-trip'и, які зашкодили б холодному старту на скромному
HTTP-сервері пристрою, а нинішній бандл достатньо малий, щоб такий
компроміс не окупився.

## Типові помилки

**Стан не оновлюється після запису:** SPA робить оптимістичні
оновлення через `setStateKey`. Якщо сервер відхилив (помилка
валідації), ви побачите своє значення на мить, після чого воно
відкотиться на наступній дельті WS. Подивіться на toast із текстом
помилки.

**Сторінка порожня після зміни маніфесту:** SPA кешує `ui.json` у
пам'яті, але перевантажує його при кожному оновленні сторінки. Жорстке
оновлення (Ctrl+Shift+R) форсує повторний фетч. Якщо нова сторінка
все одно не з'являється — перевірте `GET /api/ui` на пристрої
напряму: можливо, генератор тихо впав на перезбиранні.

**У мовному пакеті бракує рядків:** `tr()` повертається до
оригінального українського рядка, якщо ключа перекладу немає. Якщо
після перемикання мови текст усе ще українською — цього рядка просто
немає в активному мовному пакеті. Додайте його у
`data/www/i18n/<lang>.json` і виконайте `flash_data`.

**WS показує «відключено» через деякий час:** перевірте `/api/log`
на пристрої — якщо головна задача підвисла, цикл трансляції WS
відстає. SPA перепідключиться автоматично, щойно пристрій знову
почне відповідати.

**Модалка автентифікації з'являється після 401:** SPA пересилає всі
відповіді 401 через `needsLogin`. Після успішного входу користувач
має повторити свою дію вручну — невдалі запити не повторюються
автоматично, щоб потік автентифікації лишався передбачуваним.

## Чому Svelte, а не React/Vue

Три причини. **Розмір бандлу:** у Svelte немає рантайму з віртуальним
DOM; скомпільований вивід — це безпосередня маніпуляція DOM, тож
повноцінне SPA з 25 віджетами влізає у ~80 КБ gzip — прийнятно для
вбудованого HTTP-сервера. **Модель реактивності:** `$store`
елементарно пояснити C++-розробникам, які є основними контриб'юторами.
**Нуль ecosystem-churn:** Svelte 4 стабільний уже рік, і проєкту не
потрібні жодні фічі екосистеми React, яких у Svelte немає.

Компроміс — менша бібліотека сторонніх компонентів порівняно з React.
Фреймворк пише власні віджети з нуля, що добре поєднується з
маніфест-керованою філософією: каталог — це рівно те, що потрібно
фреймворку, не більше.

## Що далі

- **[02-module-author-guide/ui-widgets.md](../02-module-author-guide/ui-widgets.md)** —
  що оголосити в маніфесті, щоб віджет відрендерився.
- **[components/modesp_net.md](components/modesp_net.md)** — серверна
  сторона WebSocket + HTTP API.
- **[05-tools/generate_ui.md](../05-tools/generate_ui.md)** — як
  генерується схема `ui.json` з маніфестів.

## Джерела

- [`webui/src/App.svelte`](../../../webui/src/App.svelte) — корінь.
- [`webui/src/stores/`](../../../webui/src/stores/) — усі сховища.
- [`webui/src/lib/`](../../../webui/src/lib/) — API, WebSocket,
  visibility, утиліти графіків.
- [`webui/src/components/widgets/`](../../../webui/src/components/widgets/) —
  25 компонентів віджетів.
- [`webui/package.json`](../../../webui/package.json) — конфігурація
  Rollup.
