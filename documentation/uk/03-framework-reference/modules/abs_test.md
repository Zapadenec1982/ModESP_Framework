# `abs_test` — еталонний рецепт сценарію

> 📖 **In English:** [documentation/en/03-framework-reference/modules/abs_test.md](../../../en/03-framework-reference/modules/abs_test.md)

`abs_test` — це **еталонний рецепт сценарію** фреймворку. Він задіює майже всі можливості рушія сценаріїв в одній фікстурі: дві паралельні доріжки, переходи за часом і за умовою, композит `all_of`, синхронізація між доріжками через дзеркальні ключі, термінатор `$complete` і правило завершення `all_tracks_complete`. Використовується як HIL-тест паритету, золотий файл для `compile_scenario.py` і перший рецепт, який варто прочитати після `02-module-author-guide/recipe-authoring.md`.

Рецепт навмисно керує **абстрактними тестовими сигналами** (`test.output_a`, `test.output_b`, `test.counter`, `test.input_a`) замість справжнього обладнання, тож запускається без змін на будь-якій платі.

ВИМАГАЄ: `modesp_scenario`. Назва рецепту **8 символів**, щоб вкластися у бюджет 32-символьного дзеркального ключа (рецепт ≤ 12, доріжка ≤ 8 — див. `recipe-authoring.md`).

## Топологія

```
abs_test (сценарій)
├── track "main"  (прапорець main_track — керує дзеркальними ключами, зберігається у NVS)
│   ├── phase_a → phase_b (через 1 с)
│   ├── phase_b → phase_c (коли test.input_a > 10 І минуло 500 мс, АБО через 5 с)
│   └── phase_c → $complete (через 500 мс)
└── track "watcher"
    └── watching → $complete (коли abs_test.main_phase_name == "phase_c")
```

`completion_rule: all_tracks_complete` — сценарій завершується лише після того, як **обидві** доріжки досягли `$complete`. Загальний час виконання обмежений `scenario_timeout_max_ms = 120 000 мс` (2 хвилини); типовий прогін завершується менш ніж за 7 секунд.

## Що він задіює

| Можливість | Де |
|---|---|
| Кілька паралельних доріжок | `tracks: [main, watcher]` |
| Прапорець `main_track` | track.flags керує дзеркальними ключами `abs_test.main_*` |
| Дії при вході | `log`, `set_state` у кожній фазі |
| Перехід за часом | предикат `time_elapsed_ms` |
| Композитна умова | `all_of: [state_key_gt, time_elapsed_ms]` |
| Кілька переходів на фазу | phase_b має 2 (перемагає той, що спрацював першим) |
| Синхронізація між доріжками | watcher читає дзеркало `abs_test.main_phase_name` |
| Термінатор `$complete` | обидві доріжки закінчуються на `$complete` |
| Правило `all_tracks_complete` | сценарій завершується, коли обидві доріжки завершені |
| Таймаути на фазу і на сценарій | `timeout_ms`, `scenario_timeout_max_ms` |

## Дзеркальні ключі (контракт у межах бюджету 32 символи)

Рецепт `main_track` автоматично публікує дзеркальні ключі з іменами `<recipe>.<key>`:

| Ключ | Джерело |
|---|---|
| `abs_test.scenario_state` | рушій — `running`/`paused`/`completed`/`failed` |
| `abs_test.scenario_elapsed_s` | рушій — секунди з моменту `start()` |
| `abs_test.last_error` | рушій — останній числовий код `EngineError` |
| `abs_test.main_state` | доріжка main — `running`/`completed`/... |
| `abs_test.main_phase_name` | доріжка main — поточна назва фази |
| `abs_test.main_phase_idx` | доріжка main — поточний індекс фази |
| `abs_test.main_elapsed_s` | доріжка main — секунди у поточній фазі |
| `abs_test.watcher_state` | доріжка watcher — така сама форма |
| `abs_test.watcher_phase_name` | доріжка watcher |
| `abs_test.watcher_phase_idx` | доріжка watcher |
| `abs_test.watcher_elapsed_s` | доріжка watcher |

Найдовший ключ: `abs_test.scenario_elapsed_s` = 27 символів — у межах 32-байтного ліміту SharedState. Саме тому назву рецепту навмисно обрано 8-символьною.

## Тестові сигнали (керовані діями при вході)

| Ключ | Фаза | Напрямок |
|---|---|---|
| `test.output_a` | phase_a → true, phase_b → false | запис |
| `test.output_b` | phase_b → true, phase_c → false | запис |
| `test.counter` | phase_b → 1 | запис |
| `test.input_a` | сторож переходу phase_b | читання |

Щоб примусово вибрати гілку `state_key_gt`, запишіть `test.input_a > 10` через `/api/settings` під час phase_b. Інакше перемагає резервний перехід за 5 с.

## Покриття pytest HIL

`tools/tests/test_hil_scenario.py` запускається на справжньому ESP32 і проводить цей рецепт через:

1. **Завантаження + запуск одного екземпляра** — перевіряє всі 6 фаз і кінцевий стан.
2. **Кілька екземплярів** — завантажте `abs_test` двічі; екземпляри залишаються незалежними.
3. **Конкуренція за ресурси** — керування конфліктуючими виходами очікує, що другому завантаженому екземпляру буде відмовлено.
4. **Глобальний перехід** — інжекція ключа збою; доріжка main стрибає у `$fail`.
5. **Відновлення після перезавантаження живлення** — `start()`, жорсткий перезапуск, `try_recover()` відновлюється з `PAUSED`.
6. **Оновлення дзеркал у WebUI** — phase_name видимий у режимі реального часу.

Усі 6 тестів мають проходити, щоб перебудову рушія вважати успішною.

## Компіляція

```
python tools/compile_scenario.py \
    --manifest modules/abs_test/manifest.json \
    --out data/scenarios/abs_test.modr
```

Скомпільований `.modr` постачається у `data/scenarios/` і прошивається на розділ даних LittleFS. Використовуйте `python tools/dump_modr.py data/scenarios/abs_test.modr` для перегляду бінарного потоку токенів.

## Інтерфейс користувача

Маніфест оголошує одну картку "Abstract test scenario" з обмеженням `visible_when` — з'являється лише коли сценарій завантажено і він в одному зі станів `running`/`paused`/`completed`/`failed`. Картка показує стан сценарію, секунди, що минули, та живі назви фаз з обох доріжок.

Сторінка **"Тест"** — українська за замовчуванням, оскільки рецепт є тестовою фікстурою; перейменуйте через `ui.page`, якщо форкаєте.

## Чому це гарний приклад

- **Компактний** — 109 рядків маніфесту охоплюють ~10 окремих можливостей рушія.
- **Без обладнання** — запускається однаково на кожній платі, без налаштування драйверів.
- **Багатодоріжковий** — єдина фікстура, що задіює семантику порядку тактів доріжок і синхронізацію між ними.
- **Композитні переходи** — єдина фікстура з `all_of` + кількома переходами на фазу.
- **Прив'язаний до HIL** — будь-яка ваша зміна у рушії має залишати цей рецепт зеленим.

Форкніть його, щоб прототипувати власні рецепти:

```
cp -r modules/abs_test modules/my_recipe
sed -i 's/abs_test/my_recipe/g' modules/my_recipe/manifest.json
```

(Слідкуйте за бюджетом назви рецепту у 12 символів — див. `recipe-authoring.md`.)

## Що далі

- **[02-module-author-guide/recipe-authoring.md](../../02-module-author-guide/recipe-authoring.md)** — повний довідник граматики рецептів.
- **[02-module-author-guide/recipe-actions.md](../../02-module-author-guide/recipe-actions.md)** — каталог вбудованих дій та умов, використаних тут.
- **[components/modesp_scenario.md](../components/modesp_scenario.md)** — оглядовий опис рушія.

## Джерела

- [`modules/abs_test/manifest.json`](../../../../modules/abs_test/manifest.json)
- [`data/scenarios/abs_test.modr`](../../../../data/scenarios/abs_test.modr) (скомпільований артефакт).
- [`tools/tests/test_hil_scenario.py`](../../../../tools/tests/test_hil_scenario.py) — HIL-набір паритету.
