# `modesp_json` — JSON parsing і serialization utilities

> 📖 **Українською:** [documentation/uk/03-framework-reference/components/modesp_json.md](../../../uk/03-framework-reference/components/modesp_json.md)

Thin wrapper around the `jsmn` parser із helpers for the common patterns
used у the framework — parsing HTTP POST bodies, JSON key extraction,
type-safe value coercion. Most module authors don't interact із це
component directly; framework code (HTTP handlers, MQTT command parsing,
ConfigService) uses it internally.

REQUIRES: `jsmn` (external).

## What it provides

- `jsmn` re-exported (parser із fixed token allocation, no heap).
- Helpers для finding а value by key у а parsed token array.
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

## Helpers

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

## Why not `cJSON`?

`cJSON` is heap-heavy і more complete. `jsmn` is а fixed-token tokeniser
що allocates на stack — much cheaper for the framework's small JSON
payloads (HTTP request bodies, manifest snippets, MQTT command parsing).
For а CPU-constrained device handling REST traffic at modest rates,
zero-allocation parsing matters.

Configuration files load through `ConfigService` що uses а separate
heavier parser (manageable since it runs once at boot).

## Memory і resources

| Resource | Cost |
|---|---|
| jsmn parser state | ~16 bytes на stack |
| Token array (typical 16-32 tokens) | ~256-512 bytes на stack |

Zero heap. Per-call cost.

## Common pitfalls

**Token array too small:** complex JSON із many keys may overflow.
`jsmn_parse` returns `JSMN_ERROR_NOMEM`. Increase the token buffer or
parse у chunks.

**Unicode escapes у strings:** jsmn parses them as raw bytes; downstream
code should validate UTF-8 if necessary.

**Trailing data:** the parser stops at the first complete value. Trailing
chars are ignored; для strict validation check `r > 0` AND că ничого
не remains.

## Next steps

- **[components/modesp_net.md](modesp_net.md)** — HTTP service що uses
  це heavily.
- **[components/modesp_mqtt.md](modesp_mqtt.md)** — MQTT command parsing.

## Source

- [`components/modesp_json/`](../../../../components/modesp_json/) — helpers.
- [`components/jsmn/`](../../../../components/jsmn/) — external parser.
