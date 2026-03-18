# Eval: Quality Criteria

## Good change checklist

- [ ] No heap allocation in hot path (on_update, on_message)
- [ ] No edits to generated files
- [ ] State keys follow `module.key` format
- [ ] Readwrite keys have min/max/step or options
- [ ] Persist keys have correct type in manifest
- [ ] New features guarded by `has_feature()` if conditional
- [ ] Equipment arbitration order preserved (lockout > blocked > defrost > thermostat)
- [ ] Interlocks respected (defrost_relay + compressor never both ON)
- [ ] Tests pass: pytest (310) + host doctest (108)
- [ ] Manifest changes → regenerate (`python tools/generate_ui.py`)

## Red flags (reject or fix)

- `std::string`, `std::vector`, `new` in module code
- Direct HAL access from business module (only Equipment touches HAL)
- SharedState key without manifest declaration
- Missing `static const char* TAG` in new .cpp
- Modifying generated files directly
- `printf` or `std::cout` instead of ESP_LOGx
- Blocking calls (vTaskDelay, sleep) in on_update()

## Test requirements

- Business logic changes → add/update host doctest in `tests/host/`
- Manifest changes → verify pytest passes
- WebUI changes → manual check in browser (no automated UI tests)
- New HTTP endpoint → test with curl

## Demo readiness criteria

- Clean boot without errors in serial log
- All sensors reading (no -327.68 values)
- Thermostat cycles compressor correctly
- WebUI loads < 3s on local network
- WiFi reconnects after router restart (AP>STA probe)
- NVS persist survives reboot
