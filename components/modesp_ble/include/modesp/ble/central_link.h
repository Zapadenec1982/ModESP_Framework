/**
 * @file central_link.h
 * @brief Generic BLE central-CONNECT seam — transport ↔ connect-driver.
 *
 * The observer side has adv_decoder.h (sensors register decoders); this is the
 * CONNECT analogue. modesp_ble owns the single NimBLE host, the scan, and the
 * connect/discover/write state machine, but it knows NO device: which name to
 * scan, which characteristics to bind, and how to frame bytes are all DATA a
 * connect driver supplies at factory time via register_connect_profile().
 *
 * The driver receives an ICentralLink it writes THROUGH — one profile == one
 * link (single radio; do not assume multiple). All device wire-format knowledge
 * (GATT UUIDs, command/text byte encoding, fonts) lives in the driver.
 *
 * Layering: driver → modesp_ble; modesp_ble depends on no driver (same direction
 * as adv_decoder.h / DriverRegistry).
 */
#pragma once

#include "sdkconfig.h"   // CONFIG_MODESP_BLE_* must be defined before the guard

#if defined(CONFIG_MODESP_BLE_ENABLE) && defined(CONFIG_MODESP_BLE_CENTRAL)

#include <stdint.h>
#include <stddef.h>
#include "host/ble_uuid.h"   // ble_uuid_t — the profile passes &(ble_uuid128_t...).u (both
                             // includers, the transport + the connect driver, link NimBLE `bt`)

namespace modesp::ble {

/// Notify sink — invoked on the NimBLE HOST TASK when the subscribed characteristic
/// (notify_uuid) fires. Keep it short and non-blocking (never do a with-response
/// write from here — that would deadlock the host task).
using CentralNotifyCb = void (*)(const uint8_t* data, uint16_t len, void* ctx);

/// A generic central-connect profile a driver supplies at factory time. Every
/// device-specific value is DATA here, so the transport stays format-agnostic.
struct ConnectProfile {
    const char*       name_prefix;   ///< adv-name prefix to scan+connect (board.json ble_devices.name)
    const ble_uuid_t* write_uuid;    ///< characteristic captured as the write handle (16- or 128-bit)
    const ble_uuid_t* notify_uuid;   ///< characteristic to subscribe (CCCD enable); nullptr = write-only
    CentralNotifyCb   on_notify;     ///< nullptr ok — invoked on the host task for notify RX
    void*             ctx;           ///< opaque, passed back to on_notify
};

/// The link a driver writes through. Returned by register_connect_profile();
/// stable for the process lifetime.
class ICentralLink {
public:
    /// True once connected AND the write characteristic is bound (was is_connected()).
    virtual bool connected() const = 0;

    /// One command to the write characteristic. with_response=false → fire-and-forget
    /// no-response fast path; true → BLOCKS on the transport's completion semaphore for
    /// flow control. NEVER call the blocking form from the NimBLE host task (it would
    /// deadlock against the completion callback that releases it). Returns false if not
    /// connected or the write failed.
    virtual bool write(const uint8_t* data, uint16_t len, bool with_response) = 0;

    /// Run `body` while holding the transport's recursive write-mutex across the WHOLE
    /// call, so a control write from another caller cannot interleave mid-frame. The
    /// driver builds/chunks its bytes inside `body`, calling link->write(...) per chunk.
    /// This is how a multi-write frame stays atomic while chunking lives in the driver.
    /// C fn-ptr + arg (not std::function) to honour the zero-heap rule. Returns body's
    /// result, or false if the lock could not be taken.
    virtual bool write_frame(bool (*body)(ICentralLink* link, void* arg), void* arg) = 0;

protected:
    ~ICentralLink() = default;
};

/// Register a connect profile (call once per driver from its factory, before the
/// NimBLE host starts). Idempotent by name_prefix. Returns the link, or nullptr if
/// the (fixed) profile pool is full.
ICentralLink* register_connect_profile(const ConnectProfile& p);

/// Number of registered connect profiles (the scan matches adv names against these).
size_t connect_profile_count();

} // namespace modesp::ble

#endif // CONFIG_MODESP_BLE_ENABLE && CONFIG_MODESP_BLE_CENTRAL
