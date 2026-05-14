# `modesp_json` — JSON parsing і serialization utilities

> 📖 **In English:** [documentation/en/03-framework-reference/components/modesp_json.md](../../../en/03-framework-reference/components/modesp_json.md)

Thin wrapper навколо `jsmn` parser з helpers для common patterns
used у фреймворку — parsing HTTP POST bodies, JSON key extraction,
type-safe value coercion. Більшість module authors не interact із цим
component напряму; framework code (HTTP handlers, MQTT command parsing,
ConfigService) використовує його internally.

REQUIRES: `jsmn` (external).

## Що він provides

- `jsmn` re-exported (parser із fixed token allocation, no heap).
- Helpers для finding value by key у parsed token array.
- Type-coerced extraction (`extract_int`, `extract_float`, `extract_bool`,
  `extract_string`).

## Typical usage у HTTP handlers

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

    // Find "key" і extract його value
    float value = 0;
    if (modesp::json::extract_float(body, tokens, r, "key", value)) {
        // value тепер 23.5
        app.state().set("some.target", value);
    }

    httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}
```

## Helpers

```cpp
namespace modesp::json {

// Returns token index value для key, або -1.
int find_key(const char* body, jsmntok_t* tokens, int n_tokens, const char* key);

// Extract typed value у out parameter. Returns false при missing key
// або type mismatch.
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

`cJSON` — heap-heavy і more complete. `jsmn` — fixed-token tokeniser що
allocates на stack — much cheaper для small JSON payloads фреймворка
(HTTP request bodies, manifest snippets, MQTT command parsing). Для
CPU-constrained device handling REST traffic at modest rates,
zero-allocation parsing matters.

Configuration files load через `ConfigService` що uses окремий heavier
parser (manageable бо runs once при boot).

## Memory і resources

| Resource | Cost |
|---|---|
| jsmn parser state | ~16 bytes на stack |
| Token array (typical 16-32 tokens) | ~256-512 bytes на stack |

Zero heap. Per-call cost.

## Common pitfalls

**Token array too small:** complex JSON з many keys може overflow.
`jsmn_parse` returns `JSMN_ERROR_NOMEM`. Increase token buffer або parse
у chunks.

**Unicode escapes у strings:** jsmn parses них як raw bytes; downstream
code should validate UTF-8 якщо necessary.

**Trailing data:** parser stops при першому complete value. Trailing
chars ignored; для strict validation check `r > 0` AND що нічого не
remains.

## Що далі

- **[components/modesp_net.md](modesp_net.md)** — HTTP service що uses
  це heavily.
- **[components/modesp_mqtt.md](modesp_mqtt.md)** — MQTT command parsing.

## Source

- [`components/modesp_json/`](../../../../components/modesp_json/) — helpers.
- [`components/jsmn/`](../../../../components/jsmn/) — external parser.
