# Quickstart — Hello, Scenario

**Status:** placeholder. Заповнюється у Step 16 (engine integration + reference recipe).

## Заповнюється

5-minute hands-on:

1. **Where recipe lives:** `modules/recipe_abstract_test/manifest.json` — full example у плані.
2. **Build:** `idf.py build` — generate_ui.py + compile_scenario.py run автоматично.
3. **Flash:** `idf.py flash monitor`.
4. **Trigger from C++:** business module `engine.load("recipe_abstract_test")` + `engine.start(handle)`.
5. **Observe:** WebUI page "Тест" shows live mirror keys; logs показують transitions.
6. **Power-cycle test:** reset device mid-scenario; observe PAUSED state on boot; resume через HTTP.

End-to-end developer copy-paste-able example.
