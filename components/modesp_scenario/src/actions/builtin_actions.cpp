/**
 * @file builtin_actions.cpp
 * @brief Implementation of built-in actions і conditions.
 *
 * Catalog: see builtin_actions.h docstring.
 * Wire format: matches tools/known_actions.json hash values.
 *
 * Composite conditions (all_of, any_of, not) — NOT here. Engine handles them
 * inline під час phase tick (instance/track FSM) because they require access
 * to cond_pool для recursive evaluation.
 *
 * **Lift from `modesp_sequence/builtin_actions.cpp` (Phase 1):**
 *   - `register_builtins()` тепер takes `ActionRegistry&` (singleton killed).
 *   - State access through `ctx.state` is now `IStateBackend*` (was
 *     `modesp::SharedState*`). API is identical у basic shape — `get_raw`
 *     і `set_raw` replace `get(StateKey)` / `set(StateKey, StateValue)`.
 */

#include "modesp/scenario/builtin_actions.h"
#include "modesp/scenario/action_registry.h"
#include "modesp/scenario/action_param.h"
#include "modesp/scenario/i_state_backend.h"
#include "modesp/types.h"  // StateValue variant

#include <cstring>
#include <ctime>

#ifdef HOST_BUILD
    #include <cstdio>
    #define BUILTIN_LOG(tag, msg) std::fprintf(stderr, "[%s] %s\n", tag, msg)
#else
    #include "esp_log.h"
    #define BUILTIN_LOG(tag, msg) ESP_LOGI(tag, "%s", msg)
#endif

namespace modesp::scenario::builtins {

namespace handlers {

// ── Helper: lookup param by name hash ──
//
// Linear scan — params are short arrays (1-3 typical), faster than hash map
// для that size + zero-heap.
static const ActionParam* find_param(ActionContext& ctx, uint16_t key_hash) {
    for (uint8_t i = 0; i < ctx.param_count; ++i) {
        if (ctx.params[i].key_hash == key_hash) {
            return &ctx.params[i];
        }
    }
    return nullptr;
}

// Helper: copy interned string up to buffer size. Returns true on success.
static bool copy_string(ActionContext& ctx, uint16_t offset, char* buf, size_t buf_size) {
    if (offset >= ctx.string_pool_size) return false;
    uint8_t length = static_cast<uint8_t>(ctx.string_pool[offset]);
    if (offset + 1 + length > ctx.string_pool_size) return false;
    if (length + 1 > buf_size) return false;
    std::memcpy(buf, &ctx.string_pool[offset + 1], length);
    buf[length] = '\0';
    return true;
}

// ────────────────────────────────────────────────────────────────────────
// ACTIONS
// ────────────────────────────────────────────────────────────────────────

/// log {msg: string} — log diagnostic message. Always returns OK.
static ActionStatus do_log(ActionContext& ctx) {
    if (ctx.param_count != 1) return ActionStatus::FAILED_ABORT;
    const ActionParam* p = find_param(ctx, djb2_hash16("msg"));
    if (p == nullptr) return ActionStatus::FAILED_ABORT;
    if (p->type != static_cast<uint8_t>(ParamType::STR)) return ActionStatus::FAILED_ABORT;

    char buf[64];  // log messages bounded — long content → use state
    if (!copy_string(ctx, p->v.s_idx, buf, sizeof(buf))) {
        return ActionStatus::FAILED_RECOVERABLE;
    }
    BUILTIN_LOG(ctx.recipe_name ? ctx.recipe_name : "scenario", buf);
    return ActionStatus::OK;
}

/// set_state {key: str, type: i32-enum, value: typed} — write to state backend.
/// type field is int code from MODR_PARAM_TYPE_*.
static ActionStatus do_set_state(ActionContext& ctx) {
    if (ctx.param_count != 3) return ActionStatus::FAILED_ABORT;
    if (ctx.state == nullptr) return ActionStatus::FAILED_ABORT;

    const ActionParam* key_p = find_param(ctx, djb2_hash16("key"));
    const ActionParam* type_p = find_param(ctx, djb2_hash16("type"));
    const ActionParam* val_p  = find_param(ctx, djb2_hash16("value"));
    if (!key_p || !type_p || !val_p) return ActionStatus::FAILED_ABORT;
    if (key_p->type != static_cast<uint8_t>(ParamType::STR)) return ActionStatus::FAILED_ABORT;
    if (type_p->type != static_cast<uint8_t>(ParamType::I32)) return ActionStatus::FAILED_ABORT;

    char keybuf[64];  // state keys ≤ 32 chars; buffer headroom
    if (!copy_string(ctx, key_p->v.s_idx, keybuf, sizeof(keybuf))) {
        return ActionStatus::FAILED_RECOVERABLE;
    }

    // Construct StateValue based на declared type, regardless of value param's
    // actual type field (compiler should match, але defense-in-depth).
    int32_t type_code = type_p->v.i;
    modesp::StateValue sv;
    switch (type_code) {
        case 0:  // I32
            sv = static_cast<int32_t>(val_p->v.i);
            break;
        case 1:  // F32
            sv = val_p->v.f;
            break;
        case 2:  // BOOL
            sv = val_p->v.b;
            break;
        default:
            return ActionStatus::FAILED_ABORT;  // unsupported type
    }

    bool ok = ctx.state->set_raw(keybuf, sv);
    return ok ? ActionStatus::OK : ActionStatus::FAILED_RECOVERABLE;
}

/// wait_ms {ms: i32} — return PENDING until phase_elapsed_ms >= ms.
static ActionStatus do_wait_ms(ActionContext& ctx) {
    if (ctx.param_count != 1) return ActionStatus::FAILED_ABORT;
    const ActionParam* p = find_param(ctx, djb2_hash16("ms"));
    if (p == nullptr) return ActionStatus::FAILED_ABORT;
    if (p->type != static_cast<uint8_t>(ParamType::I32)) return ActionStatus::FAILED_ABORT;
    int32_t target_ms = p->v.i;
    if (target_ms < 0) return ActionStatus::FAILED_ABORT;
    return (ctx.phase_elapsed_ms >= static_cast<uint32_t>(target_ms))
        ? ActionStatus::OK
        : ActionStatus::PENDING;
}

// ────────────────────────────────────────────────────────────────────────
// CONDITIONS
//
// Convention: ActionStatus::OK = "condition holds (true)";
// ActionStatus::FAILED_RECOVERABLE = "condition does not hold (false)".
// Engine treats OK як trigger-fire, FAILED_RECOVERABLE як no-trigger.
// FAILED_ABORT = malformed args (compile-time bug or corrupted .modr).
// ────────────────────────────────────────────────────────────────────────

/// time_elapsed_ms {ms: i32}
static ActionStatus do_time_elapsed_ms(ActionContext& ctx) {
    if (ctx.param_count != 1) return ActionStatus::FAILED_ABORT;
    const ActionParam* p = find_param(ctx, djb2_hash16("ms"));
    if (!p || p->type != static_cast<uint8_t>(ParamType::I32)) {
        return ActionStatus::FAILED_ABORT;
    }
    int32_t threshold = p->v.i;
    if (threshold < 0) return ActionStatus::FAILED_ABORT;
    return (ctx.phase_elapsed_ms >= static_cast<uint32_t>(threshold))
        ? ActionStatus::OK
        : ActionStatus::FAILED_RECOVERABLE;
}

// Helper: compare StateValue до typed param value. Returns int analogous до
// strcmp (negative, 0, positive). Returns INT32_MIN on type mismatch
// between state і param.
//
// String comparison branch: коли state holds StringValue AND param.type == STR,
// resolves param's string from the recipe's string pool (passed through ctx
// pointers) і compares lexicographically через strncmp.
static int compare_state_to_param(const modesp::StateValue& sv, const ActionParam& p,
                                  const char* string_pool, uint16_t pool_size) {
    if (auto pi = etl::get_if<int32_t>(&sv)) {
        if (p.type == static_cast<uint8_t>(ParamType::I32)) {
            int32_t pv = p.v.i;
            return (*pi < pv) ? -1 : (*pi > pv) ? 1 : 0;
        }
        if (p.type == static_cast<uint8_t>(ParamType::F32)) {
            float pf = p.v.f;
            float pi_f = static_cast<float>(*pi);
            return (pi_f < pf) ? -1 : (pi_f > pf) ? 1 : 0;
        }
    } else if (auto pf = etl::get_if<float>(&sv)) {
        if (p.type == static_cast<uint8_t>(ParamType::F32)) {
            return (*pf < p.v.f) ? -1 : (*pf > p.v.f) ? 1 : 0;
        }
        if (p.type == static_cast<uint8_t>(ParamType::I32)) {
            float pv = static_cast<float>(p.v.i);
            return (*pf < pv) ? -1 : (*pf > pv) ? 1 : 0;
        }
    } else if (auto pb = etl::get_if<bool>(&sv)) {
        if (p.type == static_cast<uint8_t>(ParamType::BOOL)) {
            return (*pb == p.v.b) ? 0 : (*pb ? 1 : -1);
        }
    } else if (auto ps = etl::get_if<modesp::StringValue>(&sv)) {
        if (p.type == static_cast<uint8_t>(ParamType::STR)) {
            uint16_t off = p.v.s_idx;
            if (string_pool == nullptr || off >= pool_size) return INT32_MIN;
            uint8_t len = static_cast<uint8_t>(string_pool[off]);
            if (off + 1u + len > pool_size) return INT32_MIN;
            const char* state_str = ps->c_str();
            int rc = std::strncmp(state_str, string_pool + off + 1, len);
            if (rc != 0) return rc;
            return (state_str[len] == '\0') ? 0
                 : (static_cast<unsigned char>(state_str[len]) > 0 ? 1 : -1);
        }
    }
    return INT32_MIN;  // type mismatch sentinel
}

// Helper для state_key_eq/ne/lt/gt/le/ge family. Reads `key` і `value` params,
// compares state[key] to value, returns OK/FAILED_RECOVERABLE based на cmp_op.
template<typename CmpOp>
static ActionStatus state_key_cmp_helper(ActionContext& ctx, CmpOp cmp_op) {
    if (ctx.param_count != 2) return ActionStatus::FAILED_ABORT;
    if (ctx.state == nullptr) return ActionStatus::FAILED_ABORT;

    const ActionParam* key_p = find_param(ctx, djb2_hash16("key"));
    const ActionParam* val_p = find_param(ctx, djb2_hash16("value"));
    if (!key_p || !val_p) return ActionStatus::FAILED_ABORT;
    if (key_p->type != static_cast<uint8_t>(ParamType::STR)) return ActionStatus::FAILED_ABORT;

    char keybuf[64];
    if (!copy_string(ctx, key_p->v.s_idx, keybuf, sizeof(keybuf))) {
        return ActionStatus::FAILED_RECOVERABLE;
    }

    modesp::StateValue sv;
    if (!ctx.state->get_raw(keybuf, sv)) {
        return ActionStatus::FAILED_RECOVERABLE;  // key missing — condition false
    }
    int cmp = compare_state_to_param(sv, *val_p,
                                      ctx.string_pool, ctx.string_pool_size);
    if (cmp == INT32_MIN) return ActionStatus::FAILED_ABORT;  // type mismatch
    return cmp_op(cmp) ? ActionStatus::OK : ActionStatus::FAILED_RECOVERABLE;
}

static ActionStatus do_state_key_eq(ActionContext& ctx) {
    return state_key_cmp_helper(ctx, [](int c) { return c == 0; });
}
static ActionStatus do_state_key_ne(ActionContext& ctx) {
    return state_key_cmp_helper(ctx, [](int c) { return c != 0; });
}
static ActionStatus do_state_key_lt(ActionContext& ctx) {
    return state_key_cmp_helper(ctx, [](int c) { return c < 0; });
}
static ActionStatus do_state_key_gt(ActionContext& ctx) {
    return state_key_cmp_helper(ctx, [](int c) { return c > 0; });
}
static ActionStatus do_state_key_le(ActionContext& ctx) {
    return state_key_cmp_helper(ctx, [](int c) { return c <= 0; });
}
static ActionStatus do_state_key_ge(ActionContext& ctx) {
    return state_key_cmp_helper(ctx, [](int c) { return c >= 0; });
}

/// state_key_in_range {key, min, max} — true якщо min <= state[key] <= max.
static ActionStatus do_state_key_in_range(ActionContext& ctx) {
    if (ctx.param_count != 3) return ActionStatus::FAILED_ABORT;
    if (ctx.state == nullptr) return ActionStatus::FAILED_ABORT;

    const ActionParam* key_p = find_param(ctx, djb2_hash16("key"));
    const ActionParam* min_p = find_param(ctx, djb2_hash16("min"));
    const ActionParam* max_p = find_param(ctx, djb2_hash16("max"));
    if (!key_p || !min_p || !max_p) return ActionStatus::FAILED_ABORT;
    if (key_p->type != static_cast<uint8_t>(ParamType::STR)) return ActionStatus::FAILED_ABORT;

    char keybuf[64];
    if (!copy_string(ctx, key_p->v.s_idx, keybuf, sizeof(keybuf))) {
        return ActionStatus::FAILED_RECOVERABLE;
    }
    modesp::StateValue sv;
    if (!ctx.state->get_raw(keybuf, sv)) return ActionStatus::FAILED_RECOVERABLE;

    int cmp_min = compare_state_to_param(sv, *min_p,
                                          ctx.string_pool, ctx.string_pool_size);
    int cmp_max = compare_state_to_param(sv, *max_p,
                                          ctx.string_pool, ctx.string_pool_size);
    if (cmp_min == INT32_MIN || cmp_max == INT32_MIN) return ActionStatus::FAILED_ABORT;
    return (cmp_min >= 0 && cmp_max <= 0) ? ActionStatus::OK : ActionStatus::FAILED_RECOVERABLE;
}

/// state_key_changed {key} — true on first eval after state[key] differs from
/// previous value. Edge-detection wiring у engine (Stage 1.5); MVP placeholder
/// returns "no edge" (FAILED_RECOVERABLE) — safe default.
static ActionStatus do_state_key_changed(ActionContext& ctx) {
    if (ctx.param_count != 1) return ActionStatus::FAILED_ABORT;
    if (ctx.state == nullptr) return ActionStatus::FAILED_ABORT;

    const ActionParam* key_p = find_param(ctx, djb2_hash16("key"));
    if (!key_p || key_p->type != static_cast<uint8_t>(ParamType::STR)) {
        return ActionStatus::FAILED_ABORT;
    }
    char keybuf[64];
    if (!copy_string(ctx, key_p->v.s_idx, keybuf, sizeof(keybuf))) {
        return ActionStatus::FAILED_RECOVERABLE;
    }
    modesp::StateValue sv;
    (void)ctx.state->get_raw(keybuf, sv);  // existence check (placeholder)
    return ActionStatus::FAILED_RECOVERABLE;  // no edge detected (MVP)
}

/// time_of_day_eq {hh, mm} — wall-clock match. Requires SNTP-synced system time.
/// Match granularity: minute. Returns OK first tick within target minute,
/// FAILED_RECOVERABLE otherwise.
static ActionStatus do_time_of_day_eq(ActionContext& ctx) {
    if (ctx.param_count != 2) return ActionStatus::FAILED_ABORT;
    const ActionParam* hh_p = find_param(ctx, djb2_hash16("hh"));
    const ActionParam* mm_p = find_param(ctx, djb2_hash16("mm"));
    if (!hh_p || !mm_p) return ActionStatus::FAILED_ABORT;
    if (hh_p->type != static_cast<uint8_t>(ParamType::I32) ||
        mm_p->type != static_cast<uint8_t>(ParamType::I32)) {
        return ActionStatus::FAILED_ABORT;
    }
    int32_t target_hh = hh_p->v.i;
    int32_t target_mm = mm_p->v.i;
    if (target_hh < 0 || target_hh > 23 || target_mm < 0 || target_mm > 59) {
        return ActionStatus::FAILED_ABORT;
    }

    std::time_t now = std::time(nullptr);
    if (now < 24 * 3600) {
        return ActionStatus::FAILED_RECOVERABLE;
    }
    std::tm tm_local{};
#ifdef _WIN32
    localtime_s(&tm_local, &now);
#else
    localtime_r(&now, &tm_local);
#endif
    return (tm_local.tm_hour == target_hh && tm_local.tm_min == target_mm)
        ? ActionStatus::OK
        : ActionStatus::FAILED_RECOVERABLE;
}

}  // namespace handlers

// ────────────────────────────────────────────────────────────────────────
// Registration
// ────────────────────────────────────────────────────────────────────────

bool register_builtins(ActionRegistry& reg) {
    bool ok = true;

    // Actions
    ok &= reg.register_action({djb2_hash16("log"),       "log",       &handlers::do_log,       1, 1});
    ok &= reg.register_action({djb2_hash16("set_state"), "set_state", &handlers::do_set_state, 3, 3});
    ok &= reg.register_action({djb2_hash16("wait_ms"),   "wait_ms",   &handlers::do_wait_ms,   1, 1});

    // Leaf conditions
    ok &= reg.register_condition({djb2_hash16("time_elapsed_ms"),    "time_elapsed_ms",    &handlers::do_time_elapsed_ms,    1, 1});
    ok &= reg.register_condition({djb2_hash16("state_key_eq"),       "state_key_eq",       &handlers::do_state_key_eq,       2, 2});
    ok &= reg.register_condition({djb2_hash16("state_key_ne"),       "state_key_ne",       &handlers::do_state_key_ne,       2, 2});
    ok &= reg.register_condition({djb2_hash16("state_key_lt"),       "state_key_lt",       &handlers::do_state_key_lt,       2, 2});
    ok &= reg.register_condition({djb2_hash16("state_key_gt"),       "state_key_gt",       &handlers::do_state_key_gt,       2, 2});
    ok &= reg.register_condition({djb2_hash16("state_key_le"),       "state_key_le",       &handlers::do_state_key_le,       2, 2});
    ok &= reg.register_condition({djb2_hash16("state_key_ge"),       "state_key_ge",       &handlers::do_state_key_ge,       2, 2});
    ok &= reg.register_condition({djb2_hash16("state_key_in_range"), "state_key_in_range", &handlers::do_state_key_in_range, 3, 3});
    ok &= reg.register_condition({djb2_hash16("state_key_changed"),  "state_key_changed",  &handlers::do_state_key_changed,  1, 1});
    ok &= reg.register_condition({djb2_hash16("time_of_day_eq"),     "time_of_day_eq",     &handlers::do_time_of_day_eq,     2, 2});

    // Composite conditions (all_of, any_of, not) — handled inline у engine.

    return ok;
}

}  // namespace modesp::scenario::builtins
