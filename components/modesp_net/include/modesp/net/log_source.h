/**
 * @file log_source.h
 * @brief History provider contract for the /api/log* endpoints.
 *
 * modesp_net (framework) must not depend on any concrete product module.
 * The product's logging module (e.g. datalogger) implements ILogSource and
 * self-registers in on_init(); HttpService consumes the slot and null-guards
 * when no log module is present in project.json.
 *
 * Header-only on purpose: the slot works even when modesp_net compiles empty
 * (CONFIG_MODESP_NET_ENABLE=n exports include dirs but no sources), so a log
 * module registering itself never breaks an offline build.
 */

#pragma once

#include "esp_http_server.h"
#include <cstddef>

namespace modesp {

class ILogSource {
public:
    virtual ~ILogSource() = default;

    /// Стрімінг логу за останні `hours` як chunked JSON у HTTP response.
    virtual esp_err_t serialize_log_chunked(httpd_req_t* req, int hours) const = 0;

    /// Стислі лічильники для /api/log/summary. false — серіалізація не вдалася.
    virtual bool serialize_summary(char* buf, size_t buf_size) const = 0;
};

namespace log_source {

inline const ILogSource*& slot() {
    static const ILogSource* s = nullptr;
    return s;
}
inline void set(const ILogSource* src) { slot() = src; }
inline const ILogSource* get()         { return slot(); }

} // namespace log_source
} // namespace modesp
