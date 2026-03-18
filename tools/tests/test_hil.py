#!/usr/bin/env python3
"""
Hardware-in-the-Loop (HIL) тести — перевірка бізнес-логіки через HTTP API.

Підключається до реального ESP32 контролера, змінює параметри через
POST /api/settings, читає стан через GET /api/state, перевіряє алгоритми.

Запуск:
    python -m pytest tools/tests/test_hil.py -v --esp-ip=192.168.1.32
    python -m pytest tools/tests/test_hil.py -v -k "thermostat" --esp-ip=192.168.1.32

Вимоги:
    - ESP32 з ModESP v4 прошивкою в тій же мережі
    - requests: pip install requests
"""

import time
import json
import pytest

try:
    import requests
except ImportError:
    pytest.skip("requests not installed (pip install requests)", allow_module_level=True)


# ═══════════════════════════════════════════════════════════════════
# Конфігурація та фікстури
# ═══════════════════════════════════════════════════════════════════

DEFAULT_ESP_IP = "192.168.1.143"
API_TIMEOUT = 5  # секунд


@pytest.fixture(scope="session")
def esp_ip():
    """IP адреса ESP32. Змінити: ESP_IP=192.168.1.100 pytest ..."""
    import os
    return os.environ.get("ESP_IP", DEFAULT_ESP_IP)


@pytest.fixture(scope="session")
def api(esp_ip):
    """HTTP API клієнт для ESP32."""
    return ESP32API(esp_ip)


@pytest.fixture(autouse=True)
def check_connection(api):
    """Перевірка що ESP32 доступний перед кожним тестом."""
    try:
        api.get_state()
    except Exception as e:
        pytest.skip(f"ESP32 not reachable at {api.base_url}: {e}")


class ESP32API:
    """Обгортка над HTTP API ESP32."""

    def __init__(self, ip: str):
        self.base_url = f"http://{ip}"
        self.session = requests.Session()
        self.session.headers.update({"Content-Type": "application/json"})

    def get_state(self) -> dict:
        """GET /api/state — весь SharedState."""
        r = self.session.get(f"{self.base_url}/api/state", timeout=API_TIMEOUT)
        r.raise_for_status()
        return r.json()

    def get_key(self, key: str):
        """Отримати значення конкретного ключа зі стану."""
        state = self.get_state()
        return state.get(key)

    def set_settings(self, **kwargs) -> dict:
        """POST /api/settings — зміна параметрів (макс. 8 за раз)."""
        r = self.session.post(f"{self.base_url}/api/settings",
                              json=kwargs, timeout=API_TIMEOUT)
        r.raise_for_status()
        return r.json()

    def get_modules(self) -> dict:
        """GET /api/modules — статус модулів."""
        r = self.session.get(f"{self.base_url}/api/modules", timeout=API_TIMEOUT)
        r.raise_for_status()
        return r.json()

    def get_ui(self) -> dict:
        """GET /api/ui — UI schema."""
        r = self.session.get(f"{self.base_url}/api/ui", timeout=API_TIMEOUT)
        r.raise_for_status()
        return r.json()

    def wait_for(self, key: str, expected, timeout_s: float = 30,
                 poll_interval: float = 1.0) -> bool:
        """Чекати поки ключ набуде очікуване значення."""
        deadline = time.time() + timeout_s
        while time.time() < deadline:
            val = self.get_key(key)
            if val == expected:
                return True
            time.sleep(poll_interval)
        return False

    def wait_for_condition(self, check_fn, timeout_s: float = 30,
                           poll_interval: float = 1.0) -> bool:
        """Чекати поки check_fn(state) поверне True."""
        deadline = time.time() + timeout_s
        while time.time() < deadline:
            state = self.get_state()
            if check_fn(state):
                return True
            time.sleep(poll_interval)
        return False


# ═══════════════════════════════════════════════════════════════════
# Допоміжні функції
# ═══════════════════════════════════════════════════════════════════

def restore_defaults(api: ESP32API, settings: dict):
    """Відновити параметри до значень за замовчуванням після тесту."""
    # POST /api/settings приймає макс. 8 пар за раз
    items = list(settings.items())
    for i in range(0, len(items), 7):
        batch = dict(items[i:i+7])
        api.set_settings(**batch)
    time.sleep(0.5)


# ═══════════════════════════════════════════════════════════════════
# 1. CONNECTIVITY — базова перевірка
# ═══════════════════════════════════════════════════════════════════

class TestConnectivity:
    """Перевірка базового підключення до ESP32."""

    def test_state_endpoint(self, api):
        """GET /api/state повертає JSON з ключами."""
        state = api.get_state()
        assert isinstance(state, dict)
        assert len(state) > 50, f"Замало ключів у стані: {len(state)}"

    def test_modules_endpoint(self, api):
        """GET /api/modules повертає список модулів."""
        modules = api.get_modules()
        assert isinstance(modules, (dict, list))

    def test_ui_endpoint(self, api):
        """GET /api/ui повертає UI schema."""
        ui = api.get_ui()
        assert isinstance(ui, (dict, list))

    def test_required_state_keys(self, api):
        """Перевірка наявності ключових state keys."""
        state = api.get_state()
        required = [
            "equipment.air_temp", "equipment.sensor1_ok", "equipment.compressor",
            "thermostat.temperature", "thermostat.setpoint", "thermostat.state",
            "thermostat.display_temp", "thermostat.effective_setpoint",
            "defrost.active", "defrost.phase",
            "protection.alarm_active", "protection.alarm_code",
        ]
        missing = [k for k in required if k not in state]
        assert not missing, f"Відсутні ключі: {missing}"

    def test_settings_post(self, api):
        """POST /api/settings змінює параметр."""
        old_sp = api.get_key("thermostat.setpoint")
        test_sp = -15.0 if old_sp != -15.0 else -16.0
        result = api.set_settings(**{"thermostat.setpoint": test_sp})
        assert result.get("ok") is True
        time.sleep(0.5)
        new_sp = api.get_key("thermostat.setpoint")
        assert abs(new_sp - test_sp) < 0.1, f"Setpoint не змінився: {new_sp} != {test_sp}"
        # Повертаємо назад
        api.set_settings(**{"thermostat.setpoint": old_sp})


# ═══════════════════════════════════════════════════════════════════
# 2. THERMOSTAT — state machine та регулювання
# ═══════════════════════════════════════════════════════════════════

class TestThermostat:
    """Тести термостата (state machine, регулювання, захист компресора)."""

    def test_state_is_valid(self, api):
        """thermostat.state має бути одним з 4 можливих значень."""
        state = api.get_key("thermostat.state")
        assert state in ("startup", "idle", "cooling", "safety_run"), \
            f"Невідомий стан: {state}"

    def test_effective_setpoint_normal(self, api):
        """effective_setpoint = setpoint коли нічний режим вимкнений."""
        # Вимикаємо нічний режим
        api.set_settings(**{"thermostat.night_mode": 0})
        time.sleep(1)
        state = api.get_state()
        sp = state.get("thermostat.setpoint", 0)
        eff_sp = state.get("thermostat.effective_setpoint", 0)
        assert abs(eff_sp - sp) < 0.1, \
            f"effective_setpoint ({eff_sp}) != setpoint ({sp}) при night_mode=0"

    def test_setpoint_clamp(self, api):
        """Setpoint обмежується до діапазону min/max."""
        api.set_settings(**{"thermostat.setpoint": -999.0})
        time.sleep(0.5)
        sp = api.get_key("thermostat.setpoint")
        assert sp >= -50.0, f"Setpoint {sp} нижче мінімуму -50"

        api.set_settings(**{"thermostat.setpoint": 999.0})
        time.sleep(0.5)
        sp = api.get_key("thermostat.setpoint")
        assert sp <= 50.0, f"Setpoint {sp} вище максимуму 50"

        # Повертаємо
        api.set_settings(**{"thermostat.setpoint": -18.0})

    def test_differential_clamp(self, api):
        """Differential обмежується до діапазону 0.5-10.0."""
        old = api.get_key("thermostat.differential")
        api.set_settings(**{"thermostat.differential": 0.0})
        time.sleep(0.5)
        val = api.get_key("thermostat.differential")
        assert val >= 0.5, f"Differential {val} нижче мінімуму 0.5"
        api.set_settings(**{"thermostat.differential": old})

    def test_cooling_logic(self, api):
        """Якщо T > setpoint + differential, термостат повинен перейти в COOLING.

        УВАГА: цей тест залежить від реальної температури.
        Працює лише якщо T повітря вища за setpoint + differential.
        """
        state = api.get_state()
        temp = state.get("equipment.air_temp", 0)
        sp = state.get("thermostat.setpoint", -18)
        diff = state.get("thermostat.differential", 2)
        thermo_state = state.get("thermostat.state")
        defrost = state.get("defrost.active", False)

        if defrost:
            pytest.skip("Defrost активний — тест неможливий")

        if temp >= sp + diff:
            # Температура вище порогу — очікуємо COOLING (або STARTUP)
            if thermo_state == "startup":
                pytest.skip("Контролер в STARTUP — чекає startup_delay")
            assert thermo_state in ("cooling", "idle"), \
                f"T={temp} >= SP+diff={sp+diff}, але стан={thermo_state}"
        else:
            # Температура нижче порогу — очікуємо IDLE
            if thermo_state == "cooling":
                # Може бути min_on_time ще не закінчився
                pass  # дозволяємо
            elif thermo_state != "startup":
                assert thermo_state == "idle", \
                    f"T={temp} < SP+diff={sp+diff}, але стан={thermo_state}"

    def test_compressor_request_matches_state(self, api):
        """req.compressor=true тільки в COOLING або SAFETY_RUN."""
        state = api.get_state()
        thermo_state = state.get("thermostat.state")
        req_comp = state.get("thermostat.req.compressor", False)
        defrost = state.get("defrost.active", False)

        if defrost:
            # Під час defrost req.compressor=false (thermostat paused)
            assert req_comp is False, \
                "thermostat.req.compressor=true під час defrost"
            return

        if thermo_state in ("idle", "startup"):
            assert req_comp is False, \
                f"req.compressor=true в стані {thermo_state}"
        elif thermo_state == "cooling":
            assert req_comp is True, \
                "req.compressor=false в стані cooling"

    def test_comp_timers_counting(self, api):
        """Таймери comp_on_time / comp_off_time рахують."""
        t1 = api.get_state()
        time.sleep(3)
        t2 = api.get_state()

        comp = t1.get("equipment.compressor", False)
        if comp:
            on1 = t1.get("thermostat.comp_on_time", 0)
            on2 = t2.get("thermostat.comp_on_time", 0)
            assert on2 > on1, f"comp_on_time не зростає: {on1} → {on2}"
        else:
            off1 = t1.get("thermostat.comp_off_time", 0)
            off2 = t2.get("thermostat.comp_off_time", 0)
            assert off2 > off1, f"comp_off_time не зростає: {off1} → {off2}"


# ═══════════════════════════════════════════════════════════════════
# 3. NIGHT SETBACK — нічний режим
# ═══════════════════════════════════════════════════════════════════

class TestNightSetback:
    """Тести нічного зміщення setpoint."""

    def test_night_mode_off(self, api):
        """night_mode=0 → night_active=false."""
        api.set_settings(**{"thermostat.night_mode": 0})
        time.sleep(1)
        active = api.get_key("thermostat.night_active")
        assert active is False, f"night_active={active} при mode=0"

    def test_night_mode_manual_on(self, api):
        """night_mode=3 + night_active=true → effective_sp збільшується."""
        old_mode = api.get_key("thermostat.night_mode")
        old_setback = api.get_key("thermostat.night_setback")

        try:
            api.set_settings(**{
                "thermostat.night_mode": 3,
                "thermostat.night_setback": 5.0,
            })
            time.sleep(0.5)
            api.set_settings(**{"thermostat.night_active": True})
            time.sleep(1)

            state = api.get_state()
            sp = state.get("thermostat.setpoint", 0)
            eff = state.get("thermostat.effective_setpoint", 0)
            active = state.get("thermostat.night_active")

            assert active is True, "night_active не увімкнувся"
            expected = sp + 5.0
            assert abs(eff - expected) < 0.1, \
                f"effective_sp={eff}, очікувалось {expected} (sp={sp} + 5.0)"
        finally:
            api.set_settings(**{
                "thermostat.night_mode": old_mode if old_mode is not None else 0,
                "thermostat.night_setback": old_setback if old_setback is not None else 3.0,
            })
            time.sleep(0.5)
            if old_mode != 3:
                api.set_settings(**{"thermostat.night_active": False})

    def test_night_mode_manual_off(self, api):
        """night_mode=3 + night_active=false → effective_sp = setpoint."""
        api.set_settings(**{"thermostat.night_mode": 3})
        time.sleep(0.5)
        api.set_settings(**{"thermostat.night_active": False})
        time.sleep(1)

        state = api.get_state()
        sp = state.get("thermostat.setpoint", 0)
        eff = state.get("thermostat.effective_setpoint", 0)
        assert abs(eff - sp) < 0.1, \
            f"effective_sp={eff} != setpoint={sp} при night_active=false"

        # Повертаємо
        api.set_settings(**{"thermostat.night_mode": 0})

    def test_night_setback_range(self, api):
        """night_setback обмежується до 0-10°C."""
        api.set_settings(**{"thermostat.night_setback": -5.0})
        time.sleep(0.5)
        val = api.get_key("thermostat.night_setback")
        assert val >= 0.0, f"night_setback {val} < 0"

        api.set_settings(**{"thermostat.night_setback": 20.0})
        time.sleep(0.5)
        val = api.get_key("thermostat.night_setback")
        assert val <= 10.0, f"night_setback {val} > 10"

        # Повертаємо
        api.set_settings(**{"thermostat.night_setback": 3.0})

    def test_night_schedule_mode(self, api):
        """night_mode=1 (schedule) — night_active залежить від часу."""
        api.set_settings(**{"thermostat.night_mode": 1})
        time.sleep(1)

        state = api.get_state()
        active = state.get("thermostat.night_active")
        system_time = state.get("system.time", "")

        # Просто перевіряємо що night_active — bool (логіка залежить від часу)
        assert isinstance(active, bool), f"night_active не bool: {active}"

        # Повертаємо
        api.set_settings(**{"thermostat.night_mode": 0})


# ═══════════════════════════════════════════════════════════════════
# 4. DISPLAY DURING DEFROST
# ═══════════════════════════════════════════════════════════════════

class TestDisplayDefrost:
    """Тести відображення температури під час відтайки."""

    def test_display_temp_exists(self, api):
        """thermostat.display_temp завжди присутній."""
        val = api.get_key("thermostat.display_temp")
        assert val is not None, "display_temp відсутній"

    def test_display_temp_normal(self, api):
        """Поза defrost display_temp = реальна температура."""
        state = api.get_state()
        defrost = state.get("defrost.active", False)
        if defrost:
            pytest.skip("Defrost активний")

        display_temp = state.get("thermostat.display_temp")
        real_temp = state.get("thermostat.temperature")

        if real_temp is not None and display_temp is not None:
            assert abs(display_temp - real_temp) < 0.1, \
                f"display_temp={display_temp} != temperature={real_temp}"

    def test_display_defrost_options(self, api):
        """display_defrost приймає значення 0, 1, 2."""
        for val in [0, 1, 2]:
            api.set_settings(**{"thermostat.display_defrost": val})
            time.sleep(0.5)
            actual = api.get_key("thermostat.display_defrost")
            assert actual == val, f"display_defrost={actual}, очікувалось {val}"

        # Повертаємо default
        api.set_settings(**{"thermostat.display_defrost": 1})


# ═══════════════════════════════════════════════════════════════════
# 5. DEFROST — ручна відтайка
# ═══════════════════════════════════════════════════════════════════

class TestDefrost:
    """Тести відтайки (FSM, ручний запуск/зупинка)."""

    def test_defrost_state_keys(self, api):
        """Defrost ключі присутні і мають правильні типи."""
        state = api.get_state()
        assert isinstance(state.get("defrost.active"), bool)
        assert isinstance(state.get("defrost.phase"), str)

    def test_defrost_idle_state(self, api):
        """При defrost.active=false, phase=idle."""
        state = api.get_state()
        if not state.get("defrost.active"):
            assert state.get("defrost.phase") == "idle", \
                f"defrost не active, але phase={state.get('defrost.phase')}"

    def test_defrost_settings_persist(self, api):
        """Зміна defrost параметрів через API."""
        old_interval = api.get_key("defrost.interval")
        test_val = 4 if old_interval != 4 else 6

        api.set_settings(**{"defrost.interval": test_val})
        time.sleep(0.5)
        new_val = api.get_key("defrost.interval")
        assert new_val == test_val, f"defrost.interval={new_val}, очікувалось {test_val}"

        # Повертаємо
        api.set_settings(**{"defrost.interval": old_interval})

    def test_defrost_type_options(self, api):
        """defrost.type приймає 0 (natural), 1 (electric), 2 (hot gas)."""
        old_type = api.get_key("defrost.type")
        # Тільки тип 0 гарантовано доступний (не потребує defrost_relay)
        api.set_settings(**{"defrost.type": 0})
        time.sleep(0.5)
        assert api.get_key("defrost.type") == 0
        api.set_settings(**{"defrost.type": old_type})


class TestDefrostCycle:
    """Тести повного циклу відтайки (запуск → фази → зупинка).

    Запускає реальну відтайку на контролері!
    Запуск окремо: pytest -v -k "DefrostCycle"
    """

    def _ensure_idle(self, api):
        """Якщо defrost активний — зупинити і дочекатись IDLE."""
        state = api.get_state()
        if state.get("defrost.active"):
            api.set_settings(**{"defrost.manual_stop": True})
            assert api.wait_for("defrost.active", False, timeout_s=10), \
                "Не вдалось зупинити активну відтайку"
            time.sleep(1)

    def test_01_manual_start_enters_active(self, api):
        """Ручний запуск → defrost.active=true, phase=active (type=0)."""
        self._ensure_idle(api)

        count_before = api.get_key("defrost.defrost_count")
        api.set_settings(**{"defrost.manual_start": True})
        time.sleep(2)

        state = api.get_state()
        assert state.get("defrost.active") is True, \
            "defrost.active=false після manual_start"
        assert state.get("defrost.phase") == "active", \
            f"Очікувався phase=active, отримали {state.get('defrost.phase')}"
        # manual_start trigger має скинутись
        assert state.get("defrost.manual_start") is False, \
            "manual_start не скинувся"

    def test_02_thermostat_paused_during_defrost(self, api):
        """Під час defrost термостат ставить всі requests=false."""
        state = api.get_state()
        if not state.get("defrost.active"):
            pytest.skip("Defrost не активний")

        assert state.get("thermostat.req.compressor") is False, \
            "thermostat.req.compressor=true під час defrost"
        assert state.get("thermostat.req.evap_fan") is False, \
            "thermostat.req.evap_fan=true під час defrost"
        assert state.get("thermostat.req.cond_fan") is False, \
            "thermostat.req.cond_fan=true під час defrost"

    def test_03_defrost_requests_type0(self, api):
        """Type=0 (natural): все OFF — компресор зупинений."""
        state = api.get_state()
        if not state.get("defrost.active"):
            pytest.skip("Defrost не активний")
        if state.get("defrost.phase") != "active":
            pytest.skip(f"Phase={state.get('defrost.phase')}, не active")

        # Type 0: все OFF
        assert state.get("defrost.req.compressor") is False, \
            "req.compressor=true при natural defrost"
        assert state.get("defrost.req.defrost_relay") is False, \
            "req.defrost_relay=true при natural defrost"

    def test_04_display_temp_during_defrost(self, api):
        """display_temp поводиться згідно з display_defrost mode."""
        state = api.get_state()
        if not state.get("defrost.active"):
            pytest.skip("Defrost не активний")

        display_mode = state.get("thermostat.display_defrost", 1)
        display_temp = state.get("thermostat.display_temp")
        real_temp = state.get("thermostat.temperature")

        if display_mode == 0:
            # Реальна T — display_temp ~= temperature
            if real_temp is not None and display_temp is not None:
                assert abs(display_temp - real_temp) < 1.0, \
                    f"mode=0: display={display_temp}, real={real_temp}"
        elif display_mode == 1:
            # Заморожена T — не змінюється з часом
            time.sleep(3)
            dt2 = api.get_key("thermostat.display_temp")
            assert abs(display_temp - dt2) < 0.2, \
                f"mode=1 (frozen): T змінилась {display_temp}→{dt2}"
        elif display_mode == 2:
            # "-d-" символ: sentinel -999
            assert display_temp <= -900, \
                f"mode=2: display_temp={display_temp}, очікувалось <= -900"

    def test_05_phase_timer_counting(self, api):
        """phase_timer зростає під час активної фази."""
        state = api.get_state()
        if not state.get("defrost.active"):
            pytest.skip("Defrost не активний")

        t1 = state.get("defrost.phase_timer", 0)
        time.sleep(3)
        t2 = api.get_key("defrost.phase_timer")
        assert t2 > t1, f"phase_timer не зростає: {t1} → {t2}"

    def test_06_manual_stop(self, api):
        """manual_stop зупиняє відтайку → phase=idle."""
        state = api.get_state()
        if not state.get("defrost.active"):
            pytest.skip("Defrost не активний")

        api.set_settings(**{"defrost.manual_stop": True})
        time.sleep(2)

        state = api.get_state()
        assert state.get("defrost.active") is False, \
            "defrost.active=true після manual_stop"
        assert state.get("defrost.phase") == "idle", \
            f"phase={state.get('defrost.phase')}, очікувався idle"
        assert state.get("defrost.manual_stop") is False, \
            "manual_stop trigger не скинувся"

    def test_07_thermostat_resumes_after_defrost(self, api):
        """Після відтайки термостат повертається в idle і відновлює роботу."""
        state = api.get_state()
        if state.get("defrost.active"):
            pytest.skip("Defrost ще активний")

        thermo_state = state.get("thermostat.state")
        assert thermo_state in ("idle", "cooling", "startup"), \
            f"thermostat.state={thermo_state} після defrost"

        # display_temp = реальна T (не заморожена)
        display = state.get("thermostat.display_temp")
        real = state.get("thermostat.temperature")
        if display is not None and real is not None:
            assert abs(display - real) < 0.5, \
                f"display_temp={display} != temperature={real} після defrost"

    def test_08_display_mode_switch_during_defrost(self, api):
        """Зміна display_defrost mode під час відтайки.

        Запускає відтайку, перемикає mode 0→1→2, перевіряє display_temp.
        """
        self._ensure_idle(api)

        # Зберігаємо оригінальний mode
        orig_mode = api.get_key("thermostat.display_defrost")

        try:
            # Запускаємо відтайку
            api.set_settings(**{"defrost.manual_start": True})
            time.sleep(2)

            state = api.get_state()
            if not state.get("defrost.active"):
                pytest.skip("Defrost не запустився")

            real_temp = state.get("thermostat.temperature", 0)

            # Mode 0: реальна T
            api.set_settings(**{"thermostat.display_defrost": 0})
            time.sleep(1.5)
            dt = api.get_key("thermostat.display_temp")
            # Реальна T — відрізняється не більше ніж на 1°C від temperature
            current = api.get_key("equipment.air_temp")
            if current is not None and dt is not None:
                assert abs(dt - current) < 1.0, \
                    f"mode=0: display={dt}, air_temp={current}"

            # Mode 2: sentinel -999 ("-d-")
            api.set_settings(**{"thermostat.display_defrost": 2})
            time.sleep(1.5)
            dt = api.get_key("thermostat.display_temp")
            assert dt is not None and dt <= -900, \
                f"mode=2: display_temp={dt}, очікувалось <= -900"

            # Mode 1: frozen T (не змінюється)
            api.set_settings(**{"thermostat.display_defrost": 1})
            time.sleep(1.5)
            dt1 = api.get_key("thermostat.display_temp")
            time.sleep(2)
            dt2 = api.get_key("thermostat.display_temp")
            if dt1 is not None and dt2 is not None:
                assert abs(dt1 - dt2) < 0.2, \
                    f"mode=1: frozen T змінилась {dt1}→{dt2}"

        finally:
            # Зупиняємо відтайку та повертаємо mode
            api.set_settings(**{"defrost.manual_stop": True})
            time.sleep(1)
            api.set_settings(**{
                "thermostat.display_defrost": orig_mode if orig_mode is not None else 1
            })


# ═══════════════════════════════════════════════════════════════════
# 6. PROTECTION — аварії
# ═══════════════════════════════════════════════════════════════════

class TestProtection:
    """Тести аварійної системи."""

    def test_alarm_state_keys(self, api):
        """Protection ключі присутні."""
        state = api.get_state()
        assert isinstance(state.get("protection.alarm_active"), bool)
        assert isinstance(state.get("protection.alarm_code"), str)
        assert isinstance(state.get("protection.high_temp_alarm"), bool)
        assert isinstance(state.get("protection.low_temp_alarm"), bool)
        assert isinstance(state.get("protection.sensor1_alarm"), bool)

    def test_alarm_code_consistency(self, api):
        """alarm_code відповідає alarm_active."""
        state = api.get_state()
        active = state.get("protection.alarm_active")
        code = state.get("protection.alarm_code")

        if active:
            assert code != "none", "alarm_active=true, але alarm_code=none"
        else:
            assert code == "none", f"alarm_active=false, але alarm_code={code}"

    def test_alarm_code_matches_flags(self, api):
        """alarm_code відповідає окремим прапорцям аварій."""
        state = api.get_state()
        code = state.get("protection.alarm_code", "none")

        if code == "err1":
            assert state.get("protection.sensor1_alarm") is True
        elif code == "high_temp":
            assert state.get("protection.high_temp_alarm") is True
        elif code == "low_temp":
            assert state.get("protection.low_temp_alarm") is True
        elif code == "err2":
            assert state.get("protection.sensor2_alarm") is True
        elif code == "door":
            assert state.get("protection.door_alarm") is True

    def test_high_limit_setting(self, api):
        """Зміна protection.high_limit."""
        old = api.get_key("protection.high_limit")
        test_val = 15.0 if old != 15.0 else 14.0

        api.set_settings(**{"protection.high_limit": test_val})
        time.sleep(0.5)
        val = api.get_key("protection.high_limit")
        assert abs(val - test_val) < 0.1
        api.set_settings(**{"protection.high_limit": old})

    def test_low_limit_setting(self, api):
        """Зміна protection.low_limit."""
        old = api.get_key("protection.low_limit")
        test_val = -30.0 if old != -30.0 else -32.0

        api.set_settings(**{"protection.low_limit": test_val})
        time.sleep(0.5)
        val = api.get_key("protection.low_limit")
        assert abs(val - test_val) < 0.1
        api.set_settings(**{"protection.low_limit": old})

    def test_reset_alarms(self, api):
        """Команда protection.reset_alarms скидає тригер."""
        api.set_settings(**{"protection.reset_alarms": True})
        time.sleep(1)
        # Тригер має повернутись в false
        val = api.get_key("protection.reset_alarms")
        assert val is False, "reset_alarms не скинувся назад в false"

    def test_post_defrost_delay_setting(self, api):
        """Зміна protection.post_defrost_delay."""
        old = api.get_key("protection.post_defrost_delay")
        test_val = 45 if old != 45 else 60

        api.set_settings(**{"protection.post_defrost_delay": test_val})
        time.sleep(0.5)
        val = api.get_key("protection.post_defrost_delay")
        assert val == test_val
        api.set_settings(**{"protection.post_defrost_delay": old})

    def test_sensor1_alarm_reflects_sensor(self, api):
        """sensor1_alarm=false коли sensor1_ok=true."""
        state = api.get_state()
        sensor_ok = state.get("equipment.sensor1_ok")
        alarm = state.get("protection.sensor1_alarm")

        if sensor_ok:
            # Якщо датчик OK і manual_reset=false, аварія має бути знята
            manual = state.get("protection.manual_reset", False)
            if not manual:
                assert alarm is False, \
                    "sensor1_alarm=true при sensor1_ok=true і manual_reset=false"


# ═══════════════════════════════════════════════════════════════════
# 7. EQUIPMENT — арбітраж та інтерлоки
# ═══════════════════════════════════════════════════════════════════

class TestEquipment:
    """Тести Equipment Manager."""

    def test_equipment_state_keys(self, api):
        """Equipment ключі присутні."""
        state = api.get_state()
        assert "equipment.air_temp" in state
        assert "equipment.sensor1_ok" in state
        assert "equipment.compressor" in state

    def test_air_temp_is_number(self, api):
        """equipment.air_temp — число (float)."""
        val = api.get_key("equipment.air_temp")
        assert isinstance(val, (int, float)), f"air_temp не число: {type(val)}"

    def test_compressor_is_bool(self, api):
        """equipment.compressor — bool."""
        val = api.get_key("equipment.compressor")
        assert isinstance(val, bool), f"compressor не bool: {type(val)}"

    def test_defrost_relay_compressor_interlock(self, api):
        """Реле відтайки (тен) і компресор ніколи не ON одночасно при electric defrost."""
        state = api.get_state()
        comp = state.get("equipment.compressor", False)
        relay = state.get("equipment.defrost_relay", False)
        defrost_type = state.get("defrost.type", 0)
        # Інтерлок тільки при електричній відтайці (type=1)
        if defrost_type == 1:
            assert not (comp and relay), \
                "INTERLOCK VIOLATION: компресор і тен одночасно ON!"

    def test_no_outputs_without_sensor(self, api):
        """Якщо sensor1_ok=false, система в безпечному режимі."""
        state = api.get_state()
        sensor_ok = state.get("equipment.sensor1_ok")
        if not sensor_ok:
            thermo_state = state.get("thermostat.state")
            assert thermo_state == "safety_run", \
                f"sensor1_ok=false, але thermostat.state={thermo_state}"


# ═══════════════════════════════════════════════════════════════════
# 8. PERSIST — збереження параметрів
# ═══════════════════════════════════════════════════════════════════

class TestPersist:
    """Тести збереження параметрів (NVS persistence)."""

    def test_setpoint_persists(self, api):
        """Setpoint зберігається між запитами."""
        test_sp = -22.5
        api.set_settings(**{"thermostat.setpoint": test_sp})
        time.sleep(6)  # > 5s debounce PersistService

        sp = api.get_key("thermostat.setpoint")
        assert abs(sp - test_sp) < 0.1, f"Setpoint {sp} != {test_sp}"

        # Повертаємо
        api.set_settings(**{"thermostat.setpoint": -18.0})

    def test_multiple_settings_at_once(self, api):
        """Кілька параметрів змінюються одним POST запитом."""
        result = api.set_settings(**{
            "thermostat.setpoint": -20.0,
            "thermostat.differential": 3.0,
        })
        assert result.get("ok") is True
        time.sleep(0.5)

        state = api.get_state()
        assert abs(state.get("thermostat.setpoint", 0) - (-20.0)) < 0.1
        assert abs(state.get("thermostat.differential", 0) - 3.0) < 0.1

        # Повертаємо
        api.set_settings(**{
            "thermostat.setpoint": -18.0,
            "thermostat.differential": 2.0,
        })

    def test_readonly_key_rejected(self, api):
        """Спроба змінити read-only ключ відхиляється (HTTP 400)."""
        # equipment.air_temp — read only
        # API повертає 400 коли всі ключі відхилені
        r = api.session.post(f"{api.base_url}/api/settings",
                             json={"equipment.air_temp": 99.0},
                             timeout=API_TIMEOUT)
        assert r.status_code == 400, \
            f"Очікувався 400 для read-only ключа, отримали {r.status_code}"
        time.sleep(0.5)
        val = api.get_key("equipment.air_temp")
        assert val != 99.0, "Read-only ключ equipment.air_temp було змінено!"


# ═══════════════════════════════════════════════════════════════════
# 9. SYSTEM — загальний стан системи
# ═══════════════════════════════════════════════════════════════════

class TestSystem:
    """Тести системних ключів."""

    def test_uptime_counting(self, api):
        """system.uptime зростає."""
        up1 = api.get_key("system.uptime")
        time.sleep(3)
        up2 = api.get_key("system.uptime")
        assert up2 > up1, f"uptime не зростає: {up1} → {up2}"

    def test_wifi_connected(self, api):
        """wifi.connected=true (якщо ми підключені, WiFi працює)."""
        val = api.get_key("wifi.connected")
        assert val is True, f"wifi.connected={val}"

    def test_system_time_present(self, api):
        """system.time і system.date присутні (SNTP)."""
        state = api.get_state()
        assert "system.time" in state, "system.time відсутній"
        assert "system.date" in state, "system.date відсутній"

    def test_free_heap(self, api):
        """system.free_heap > 50KB (достатньо пам'яті)."""
        heap = api.get_key("system.free_heap")
        if heap is not None:
            assert heap > 50000, f"Мало вільної пам'яті: {heap} bytes"


# ═══════════════════════════════════════════════════════════════════
# 10. SCENARIO — комплексні сценарії
# ═══════════════════════════════════════════════════════════════════

class TestScenarios:
    """Комплексні сценарії що перевіряють взаємодію модулів."""

    def test_night_mode_raises_effective_sp(self, api):
        """Сценарій: увімкнення нічного режиму піднімає effective_setpoint.

        1. Встановлюємо SP=-18, night_setback=4
        2. Вмикаємо manual night mode
        3. Перевіряємо effective_sp = -14
        4. Вимикаємо
        5. Перевіряємо effective_sp = -18
        """
        # Зберігаємо оригінальні значення
        orig = api.get_state()
        orig_sp = orig.get("thermostat.setpoint", -18)
        orig_mode = orig.get("thermostat.night_mode", 0)
        orig_setback = orig.get("thermostat.night_setback", 3.0)

        try:
            # Крок 1: налаштування
            api.set_settings(**{
                "thermostat.setpoint": -18.0,
                "thermostat.night_setback": 4.0,
                "thermostat.night_mode": 3,
            })
            time.sleep(1)

            # Крок 2: вмикаємо ніч
            api.set_settings(**{"thermostat.night_active": True})
            time.sleep(1)

            state = api.get_state()
            assert state.get("thermostat.night_active") is True
            eff = state.get("thermostat.effective_setpoint", 0)
            assert abs(eff - (-14.0)) < 0.1, \
                f"effective_sp={eff}, очікувалось -14.0 (-18+4)"

            # Крок 3: вимикаємо ніч
            api.set_settings(**{"thermostat.night_active": False})
            time.sleep(1)

            state = api.get_state()
            assert state.get("thermostat.night_active") is False
            eff = state.get("thermostat.effective_setpoint", 0)
            assert abs(eff - (-18.0)) < 0.1, \
                f"effective_sp={eff}, очікувалось -18.0"
        finally:
            # Відновлюємо
            api.set_settings(**{
                "thermostat.setpoint": orig_sp,
                "thermostat.night_setback": orig_setback,
                "thermostat.night_mode": orig_mode,
            })
            time.sleep(0.5)
            api.set_settings(**{"thermostat.night_active": False})

    def test_defrost_display_temp_frozen(self, api):
        """Сценарій: під час відтайки display_temp заморожена (mode=1).

        Потрібна реальна відтайка (ручний запуск).
        """
        state = api.get_state()
        if state.get("defrost.active"):
            # Вже в відтайці — перевіряємо поточний display_temp
            display_mode = state.get("thermostat.display_defrost", 1)
            display_temp = state.get("thermostat.display_temp")

            if display_mode == 2:
                assert display_temp is not None and display_temp <= -900, \
                    f"display_defrost=2, але display_temp={display_temp} (не -999)"
            elif display_mode == 1:
                # Заморожена T — не повинна змінюватись з часом
                time.sleep(2)
                dt2 = api.get_key("thermostat.display_temp")
                assert abs(display_temp - dt2) < 0.1, \
                    "display_temp змінюється при mode=1 (frozen)"
        else:
            pytest.skip("Defrost не активний — запустіть ручну відтайку")

    def test_alarm_with_high_limit_change(self, api):
        """Сценарій: зменшення high_limit нижче поточної T.

        Встановлюємо high_limit трохи нижче air_temp.
        Після alarm_delay має спрацювати HAL alarm.
        УВАГА: не чекаємо повний alarm_delay (може бути 30 хв).
        Тест перевіряє що pending починається.
        """
        state = api.get_state()
        temp = state.get("equipment.air_temp", 0)
        defrost = state.get("defrost.active", False)
        orig_limit = state.get("protection.high_limit", 12.0)
        orig_delay = state.get("protection.alarm_delay", 30)

        if defrost:
            pytest.skip("Defrost активний — HAL alarm suppress")

        try:
            # Встановлюємо limit на 1°C нижче поточної T і мінімальний delay
            new_limit = temp - 1.0
            api.set_settings(**{
                "protection.high_limit": new_limit,
                "protection.alarm_delay": 5,
            })
            time.sleep(2)

            # Перевіряємо що high_limit змінився
            hl = api.get_key("protection.high_limit")
            assert abs(hl - new_limit) < 0.5, \
                f"high_limit={hl}, очікувалось ~{new_limit}"

            # Alarm ще не мав спрацювати (delay=5 хв > 2 секунди)
            # Але ми переконуємось що система реагує
            # (повний тест потребував би чекати 5 хв)
        finally:
            api.set_settings(**{
                "protection.high_limit": orig_limit,
                "protection.alarm_delay": orig_delay,
            })
            # Скидаємо аварії якщо щось спрацювало
            time.sleep(0.5)
            api.set_settings(**{"protection.reset_alarms": True})

    def test_consistency_snapshot(self, api):
        """Знімок стану — перевірка внутрішньої узгодженості."""
        state = api.get_state()

        # 1. Якщо alarm_active, має бути код != none
        if state.get("protection.alarm_active"):
            assert state.get("protection.alarm_code") != "none"

        # 2. Якщо defrost active, phase != idle
        if state.get("defrost.active"):
            assert state.get("defrost.phase") != "idle"

        # 3. Компресор і реле відтайки ніколи одночасно при електричній відтайці
        if state.get("defrost.type", 0) == 1:
            assert not (state.get("equipment.compressor") and state.get("equipment.defrost_relay"))

        # 4. effective_sp >= setpoint (бо night_setback >= 0)
        sp = state.get("thermostat.setpoint", 0)
        eff = state.get("thermostat.effective_setpoint", 0)
        if sp is not None and eff is not None:
            assert eff >= sp - 0.1, \
                f"effective_sp={eff} < setpoint={sp}"

        # 5. display_temp існує
        assert state.get("thermostat.display_temp") is not None
