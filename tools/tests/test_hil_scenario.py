#!/usr/bin/env python3
"""
HIL parity test для rebuilt scenario engine (modesp_scenario).

Verifies end-to-end behavior parity з old modesp_sequence engine:
  1. Load abs_test.modr via POST /api/scenario/load
  2. Start via POST /api/scenario/start
  3. Poll mirror keys to track progression through phases
  4. Verify completion (both tracks → completed, scenario_state = "completed")
  5. Unload cleanly

Plan: Phase 4 — HIL parity test + decommission.

Запуск:
    python -m pytest tools/tests/test_hil_scenario.py -v --esp-ip=192.168.1.143
    ESP_IP=192.168.1.143 python -m pytest tools/tests/test_hil_scenario.py -v

Вимоги:
    - ESP32 з прошитим Phase 3 firmware (modesp_scenario wired)
    - LittleFS image включає data/scenarios/abs_test.modr (auto-built у idf.py build)
"""

import os
import time
import pytest

try:
    import requests
except ImportError:
    pytest.skip("requests not installed (pip install requests)", allow_module_level=True)


DEFAULT_ESP_IP = "192.168.1.143"
API_TIMEOUT = 5  # seconds


@pytest.fixture(scope="session")
def esp_ip():
    return os.environ.get("ESP_IP", DEFAULT_ESP_IP)


@pytest.fixture(scope="session")
def base_url(esp_ip):
    return f"http://{esp_ip}"


@pytest.fixture(scope="session")
def session(base_url):
    s = requests.Session()
    s.headers.update({"Content-Type": "application/json"})
    try:
        r = s.get(f"{base_url}/api/state", timeout=API_TIMEOUT)
        r.raise_for_status()
    except Exception as e:
        pytest.skip(f"ESP32 not reachable at {base_url}: {e}")
    return s


# ═══════════════════════════════════════════════════════════════════
# Helpers
# ═══════════════════════════════════════════════════════════════════

def get_state(session, base_url):
    r = session.get(f"{base_url}/api/state", timeout=API_TIMEOUT)
    r.raise_for_status()
    return r.json()


def get_scenario_list(session, base_url):
    r = session.get(f"{base_url}/api/scenario/list", timeout=API_TIMEOUT)
    r.raise_for_status()
    return r.json()


def get_scenario_info(session, base_url, handle):
    r = session.get(f"{base_url}/api/scenario/info?handle={handle}",
                    timeout=API_TIMEOUT)
    r.raise_for_status()
    return r.json()


def scenario_load(session, base_url, path):
    r = session.post(f"{base_url}/api/scenario/load",
                     json={"path": path}, timeout=API_TIMEOUT)
    r.raise_for_status()
    return r.json()


def scenario_op(session, base_url, op, handle):
    """Generic POST /api/scenario/<op> {"handle": N}."""
    r = session.post(f"{base_url}/api/scenario/{op}",
                     json={"handle": handle}, timeout=API_TIMEOUT)
    r.raise_for_status()
    return r.json()


def wait_for(predicate, *, timeout=10.0, poll=0.5, desc=""):
    """Poll predicate() until truthy or timeout. Returns last value."""
    deadline = time.time() + timeout
    last = None
    while time.time() < deadline:
        last = predicate()
        if last:
            return last
        time.sleep(poll)
    raise AssertionError(f"Timeout {timeout}s waiting for: {desc} (last={last})")


# ═══════════════════════════════════════════════════════════════════
# Tests
# ═══════════════════════════════════════════════════════════════════

def test_scenario_module_registered(session, base_url):
    """Engine module reachable і list endpoint returns valid JSON."""
    info = get_scenario_list(session, base_url)
    assert "active" in info
    assert "slots" in info
    assert info["active"] == 0  # no scenarios loaded initially


def test_abs_test_load_start_complete(session, base_url):
    """Full end-to-end: load abs_test.modr, run both tracks, verify completion."""

    # ── 1. Load ──
    resp = scenario_load(session, base_url, "/data/scenarios/abs_test.modr")
    assert "handle" in resp, f"Load failed: {resp}"
    handle = resp["handle"]
    assert 1 <= handle <= 8

    # ── 2. Inspect — state == LOADED ──
    info = get_scenario_info(session, base_url, handle)
    assert info["state"] == "loaded"
    assert info["tracks"] is not None
    assert len(info["tracks"]) == 2  # main + watcher

    # ── 3. Start ──
    resp = scenario_op(session, base_url, "start", handle)
    assert resp.get("ok") is True, f"Start failed: {resp}"

    # ── 4. Wait until RUNNING і phase progress ──
    def is_running():
        st = get_state(session, base_url)
        return st.get("abs_test.scenario_state") == "running"
    wait_for(is_running, timeout=3.0, desc="scenario starts running")

    # main track phases: phase_a (1s) → phase_b (gated by test.input_a>10 OR 5s) → phase_c (0.5s) → $complete
    # watcher: watching → $complete (when main.phase_name == "phase_c")
    # Без injection test.input_a, phase_b waits time fallback: 5s. Total runtime ~6-7s.

    def both_completed():
        st = get_state(session, base_url)
        return (st.get("abs_test.scenario_state") == "completed"
                and st.get("abs_test.main_state") == "completed"
                and st.get("abs_test.watcher_state") == "completed")
    wait_for(both_completed, timeout=12.0, poll=0.5,
             desc="both tracks complete")

    # ── 5. Verify final state via dedicated endpoint ──
    info = get_scenario_info(session, base_url, handle)
    assert info["state"] == "completed"
    for t in info["tracks"]:
        assert t["state"] == "completed"

    # ── 6. Unload ──
    resp = scenario_op(session, base_url, "unload", handle)
    assert resp.get("ok") is True

    # Confirm gone з list
    lst = get_scenario_list(session, base_url)
    assert lst["active"] == 0


def test_mirror_keys_populated(session, base_url):
    """Mirror keys appear у SharedState while scenario active."""
    resp = scenario_load(session, base_url, "/data/scenarios/abs_test.modr")
    handle = resp["handle"]
    try:
        scenario_op(session, base_url, "start", handle)

        # Mirror keys should appear immediately on first engine tick.
        def mirror_ready():
            st = get_state(session, base_url)
            required = [
                "abs_test.scenario_state",
                "abs_test.scenario_elapsed_s",
                "abs_test.main_state",
                "abs_test.main_phase_name",
                "abs_test.main_phase_idx",
                "abs_test.watcher_state",
                "abs_test.watcher_phase_name",
            ]
            missing = [k for k in required if k not in st]
            return not missing

        wait_for(mirror_ready, timeout=3.0, desc="mirror keys populated")

        # Spot-check values during RUNNING
        st = get_state(session, base_url)
        assert st["abs_test.scenario_state"] == "running"
        assert isinstance(st["abs_test.scenario_elapsed_s"], int)
        assert st["abs_test.main_phase_name"] in ("phase_a", "phase_b", "phase_c")

        # Abort cleanly
        scenario_op(session, base_url, "abort", handle)
    finally:
        try:
            scenario_op(session, base_url, "unload", handle)
        except Exception:
            pass


def test_pause_resume(session, base_url):
    """Pause holds runtime; resume continues."""
    resp = scenario_load(session, base_url, "/data/scenarios/abs_test.modr")
    handle = resp["handle"]
    try:
        scenario_op(session, base_url, "start", handle)
        wait_for(lambda: get_scenario_info(session, base_url, handle)["state"] == "running",
                 timeout=3.0, desc="running")

        # Pause
        scenario_op(session, base_url, "pause", handle)
        info = get_scenario_info(session, base_url, handle)
        assert info["state"] == "paused"

        # Wait — elapsed should NOT advance significantly у paused state.
        e1 = info["elapsed_s"]
        time.sleep(1.5)
        info2 = get_scenario_info(session, base_url, handle)
        # У paused, instance_tick should no-op. Sometimes 1s tick happens
        # exactly on boundary, але delta > 1s would mean tick continued.
        assert info2["elapsed_s"] - e1 <= 1, \
            f"elapsed advanced under pause: {e1} -> {info2['elapsed_s']}"

        # Resume і expect progression
        scenario_op(session, base_url, "resume", handle)
        wait_for(lambda: get_scenario_info(session, base_url, handle)["state"] == "running",
                 timeout=3.0, desc="resumed running")
    finally:
        try:
            scenario_op(session, base_url, "abort", handle)
            scenario_op(session, base_url, "unload", handle)
        except Exception:
            pass


def test_invalid_handle_rejected(session, base_url):
    """Engine rejects ops on non-existent handles."""
    r = session.post(f"{base_url}/api/scenario/start",
                     json={"handle": 99}, timeout=API_TIMEOUT)
    # Engine responds 400 з error JSON for INVALID_HANDLE.
    assert r.status_code == 400
    body = r.json()
    assert body.get("ok") is False
    assert body.get("error") == "invalid_handle"


def test_unload_idempotent(session, base_url):
    """Unloading а freshly loaded scenario is OK; double unload returns INVALID_HANDLE."""
    resp = scenario_load(session, base_url, "/data/scenarios/abs_test.modr")
    handle = resp["handle"]
    r1 = scenario_op(session, base_url, "unload", handle)
    assert r1.get("ok") is True
    # Second unload should error.
    r = session.post(f"{base_url}/api/scenario/unload",
                     json={"handle": handle}, timeout=API_TIMEOUT)
    assert r.status_code == 400


# Note: power-cycle recovery test (verifying SCTK token persist/restore і
# SQTK rejection) requires power-cycle automation (relay control або manual
# reset prompt). Not included у smoke pytest — exercised manually as Phase 4
# acceptance criterion. Documented у migration notes.
#
# Resource contention test requires а recipe з resource declarations — abs_test
# has none. Stage 1.5 deliverable (когда secondary fixture lands).
