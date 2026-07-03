#pragma once
/**
 * @file panel_text.h
 * @brief On-screen text slots for the LED panel — a tiny text-output API.
 *
 * Lives WITH the panel module (which owns the panel.slotN state keys and the display),
 * so the framework core carries no panel knowledge. A module that wants to post a short
 * message to the panel opts in by depending on the `panel` component and including this
 * header; the panel module rotates the non-empty slots onto the LED display (alongside
 * the clock / sensor readouts).
 *
 * This is a thin, zero-cost convention over SharedState — the slots are ordinary string
 * state keys ("panel.slot0".."panel.slot4"), so a module posts with its OWN state_set():
 *
 *     #include "panel_text.h"          // REQUIRES the panel component
 *     ...
 *     state_set(modesp::panel_text::slot(0), "ALARM");   // post to slot 0
 *     state_set(modesp::panel_text::slot(1), "DEFROST");
 *     state_set(modesp::panel_text::slot(0), "");        // clear slot 0
 *
 * Notes:
 *   - Messages display up to 31 chars (the SharedState string cap; the panel scrolls them).
 *   - An empty ("") or never-written slot is simply not shown.
 *   - state_set is a BaseModule method, so call it from inside a module (on_update/on_init).
 *   - To read a slot back, use read_string(modesp::panel_text::slot(i), buf, sizeof(buf)).
 */

namespace modesp {
namespace panel_text {

// Number of shared text slots the panel exposes.
inline constexpr int SLOTS = 5;

// SharedState string key for text slot i (0..SLOTS-1). Out-of-range clamps to slot 0.
inline const char* slot(int i) {
    static const char* const KEYS[SLOTS] = {
        "panel.slot0", "panel.slot1", "panel.slot2", "panel.slot3", "panel.slot4"
    };
    return (i >= 0 && i < SLOTS) ? KEYS[i] : KEYS[0];
}

} // namespace panel_text
} // namespace modesp
