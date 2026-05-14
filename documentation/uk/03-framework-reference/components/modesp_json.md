# `modesp_json` — утиліти розбору і серіалізації JSON

> 📖 **In English:** [documentation/en/03-framework-reference/components/modesp_json.md](../../../en/03-framework-reference/components/modesp_json.md)

Тонка обгортка навколо розбирача `jsmn` зі службовими функціями для
типових шаблонів, що використовуються у фреймворку, — розбір тіл
HTTP POST, витягнення ключів JSON, типобезпечне приведення значень.
Більшість авторів модулів не взаємодіють з цим компонентом напряму;
код фреймворку (обробники HTTP, розбір команд MQTT, ConfigService)
використовує його внутрішньо.

ЗАЛЕЖНОСТІ: `jsmn` (зовнішній).

## Що він надає

- Реекспортований `jsmn` (розбирач з фіксованим виділенням токенів, без купи).
- Помічники для пошуку значення за ключем у розібраному масиві токенів.
- Витягнення з приведенням типу (`extract_int`, `extract_float`,
  `extract_bool`, `extract_string`).

## Типове використання у обробниках HTTP

```cpp
#include "modesp/net/json_helper.h"

// HTTP handler що parses {"key": 23.5} body
esp_err_t my_handler(httpd_req_t* req) {
    char body[256];
    int len = httpd_req_recv(req, body, sizeof(body) - 1);
    if (len <= 0) return ESP_FAIL;
    body[len] = '\0';

    jsmn_parser p;
    jsmntok_t tokens[16];
    jsmn_init(&p);
    int r = jsmn_parse(&p, body, len, tokens, 16);
    if (r < 1 || tokens[0].type != JSMN_OBJECT) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    // Find "key" і extract its value
    float value = 0;
    if (modesp::json::extract_float(body, tokens, r, "key", value)) {
        // value is now 23.5
        app.state().set("some.target", value);
    }

    httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}
```

## Помічники

```cpp
namespace modesp::json {

// Returns token index of value for key, or -1.
int find_key(const char* body, jsmntok_t* tokens, int n_tokens, const char* key);

// Extract typed value into out parameter. Returns false on missing key
// or type mismatch.
bool extract_int(const char* body, jsmntok_t* tokens, int n_tokens,
                 const char* key, int32_t& out);
bool extract_float(const char* body, jsmntok_t* tokens, int n_tokens,
                   const char* key, float& out);
bool extract_bool(const char* body, jsmntok_t* tokens, int n_tokens,
                  const char* key, bool& out);
bool extract_string(const char* body, jsmntok_t* tokens, int n_tokens,
                    const char* key, char* out, size_t out_cap);

}
```

## Чому не `cJSON`?

`cJSON` важкий для купи і більш повний. `jsmn` — це токенайзер з
фіксованим набором токенів, що виділяє пам'ять на стеку — значно
дешевший для невеликих корисних навантажень JSON у фреймворку (тіла
HTTP-запитів, фрагменти маніфестів, розбір команд MQTT). Для пристрою
з обмеженим CPU, що обробляє REST-трафік з помірною інтенсивністю,
розбір без виділень має значення.

Конфігураційні файли завантажуються через `ConfigService`, який
використовує окремий важчий розбирач (це прийнятно, бо він
запускається один раз при завантаженні).

## Пам'ять і ресурси

| Ресурс | Вартість |
|---|---|
| Стан розбирача jsmn | ~16 байт на стеку |
| Масив токенів (типово 16-32 токени) | ~256-512 байт на стеку |

Нуль купи. Вартість на виклик.

## Типові помилки

**Замалий масив токенів:** складний JSON з багатьма ключами може
переповнити його. `jsmn_parse` повертає `JSMN_ERROR_NOMEM`. Збільшіть
буфер токенів або розбирайте частинами.

**Unicode-екранування у рядках:** jsmn розбирає їх як сирі байти;
наступний код має валідувати UTF-8 за потреби.

**Дані у хвості:** розбирач зупиняється на першому повному значенні.
Хвостові символи ігноруються; для суворої валідації перевіряйте
`r > 0` І що нічого не залишилося.

## Що далі

- **[components/modesp_net.md](modesp_net.md)** — HTTP-сервіс, який
  активно це використовує.
- **[components/modesp_mqtt.md](modesp_mqtt.md)** — розбір команд MQTT.

## Джерела

- [`components/modesp_json/`](../../../../components/modesp_json/) — помічники.
- [`components/jsmn/`](../../../../components/jsmn/) — зовнішній розбирач.
