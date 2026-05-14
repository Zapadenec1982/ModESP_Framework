# 06 — Арбітраж ресурсів (ISA-88 §5.3)

> 📖 **In English:** [documentation/en/03-framework-reference/scenario-engine/06_resource_arbitration.md](../../../en/03-framework-reference/scenario-engine/06_resource_arbitration.md)

Engine гарантує, що паралельні сценарії не конфліктують за спільні ресурси
(актуатори, контролери, апаратні модулі). Адаптує ISA-88 §5.3 —
стандарт для хімічного пакетного оброблення — до вбудованих обмежень ModESP.

## Дві області видимості

### Область сценарію (scenario-scope)

Захоплюється атомарно під час `start()`, звільняється після завершення / aborts / unload.
Використовується для ресурсів довготривалого утримання, якими сценарій
«володіє» протягом усього свого виконання: виділеного контролера нагрівача,
ексклюзивного доступу до групи сенсорів тощо.

```jsonc
"scenario": {
  "resources": [
    {"resource": "equipment.heater", "exclusive": true}
  ],
  "tracks": [...]
}
```

`start()` викликає `arbiter.acquire_scenario(handle, resources, count)`.
Атомарно «все або нічого»: якщо БУДЬ-ЯКИЙ із перерахованих ресурсів
утримується іншим handle, повертає `RESOURCE_CONTENDED` БЕЗ часткового
захоплення.

### Область фази (phase-scope)

Захоплюється при вході у фазу, звільняється при виході з фази. Використовується
для коротко утримуваних спільних ресурсів (наприклад, насос поливу
теплиці, спільний для кількох зон; пробний зонд, що використовується
короткочасно в декількох фазах).

```jsonc
"tracks": [{
  "phases": [{
    "name": "watering",
    "phase_resources": [{"resource": "equipment.pump", "exclusive": true}],
    ...
  }]
}]
```

`track_tick` викликає `arbiter.try_acquire_phase(handle, track, phase, claims, count)`
при вході у фазу. У разі конфлікту доріжка переходить у стан
`WAITING_FOR_RESOURCE` і повторює спроби кожен такт, доки ресурс не буде
захоплений або не спрацює таймаут фази.

## Відстеження володіння

`etl::flat_map<uint16_t resource_hash, OwnerInfo, MAX_RESOURCES=32>`.
Один власник на хеш ресурсу в MVP (семантика спільного багатовласницького
доступу — Stage 1.5).

```cpp
struct OwnerInfo {
    SequenceHandle handle;      // 1..MAX_SEQUENCES
    TrackIdx       track_idx;   // 0xFF = scenario-scope
    uint8_t        phase_idx;   // діагностика
    uint8_t        exclusive;   // 1 = ексклюзивний
};
```

`TRACK_IDX_SCENARIO = 0xFF` відрізняє володіння у scenario-scope від
володіння окремою доріжкою. Один handle може одночасно утримувати
ресурси у scenario-scope і phase-scope на різних ресурсах.

## Алгоритм атомарного захоплення

Двофазний коміт (на виклик `acquire_scenario` / `try_acquire_phase`):

```
Фаза 1 (dry-run): для кожного ресурсу в пакеті:
    якщо can_grant(hash, exclusive, requestor) == false:
        повернути RESOURCE_CONTENDED

Фаза 2 (commit): для кожного ресурсу:
    якщо вже належить тому самому handle:
        пропустити (ідемпотентне повторне надання; mark inserted[i] = false)
    якщо owners_.full():
        rollback: видалити записи, де inserted[j] == true для j < i
        повернути RESOURCE_CONTENDED
    вставити володіння; mark inserted[i] = true

повернути OK
```

Bitmap `inserted[]` (доданий під час пост-ревʼю фікса) гарантує, що rollback
видалить лише ті записи, які були фактично вставлені у поточному виклику —
зберігає попередні володіння того самого власника, які були ідемпотентними
повторними наданнями.

## Правила `can_grant`

| Існуючий власник | Запит ексклюзивний? | Дозволено? |
|---|---|---|
| Немає (вільно) | Будь-який | Так |
| Той самий (handle, track) | Будь-який | Так (ідемпотентне повторне надання) |
| Інший + існуючий ексклюзивний | Будь-який | Ні |
| Інший + існуючий спільний | Ексклюзивний | Ні |
| Інший + існуючий спільний | Спільний | Ні (MVP — одновласницька карта; Stage 1.5 — багатовласницька) |

**Важливе застереження MVP:** семантика «спільний+спільний — OK», типова
для `std::shared_mutex`, **не реалізована** — одновласницька карта на
ресурс. Рецепти, що декларують `exclusive: 0`, отримують ту саму поведінку,
що й `exclusive: 1` (відхиляються при крос-handle конфлікті).
Задокументовано в заголовку `resource_arbiter.h`.

## Синтаксис декларації у рецепті

```jsonc
"scenario": {
  "resources": [
    {"resource": "equipment.heater_zone1", "exclusive": true},
    {"resource": "equipment.shared_sensor", "exclusive": false}
  ],
  "tracks": [{
    "name": "main",
    "phases": [{
      "name": "warmup",
      "phase_resources": [
        {"resource": "equipment.fan", "exclusive": true}
      ],
      ...
    }]
  }]
}
```

Компілятор обчислює `djb2_hash16(resource_name)` і записує хеш до записів
`modr_resource_decl` / `modr_phase_resource_claim` у `.modr`.

## Інтеграція з життєвим циклом

| Подія | Виклик арбітра |
|-------|--------------|
| `engine.start(h)` | `acquire_scenario(h, resources, count)` |
| Доріжка входить у нову фазу з `phase_resource_n > 0` | `try_acquire_phase(h, track, phase_idx, claims, count)` |
| Доріжка виходить із фази (перехід спрацював або abort) | `release_phase(h, track)` |
| Доріжка FAILED (action FAILED_ABORT, abort рівня сценарію, таймаут фази) | `release_phase(h, track)` |
| Сценарій досягає COMPLETED або FAILED | `release_scenario(h)` |
| `engine.unload(h)` | Обидва `release_scenario(h)` ТА `release_phase(h, t)` для всіх t |

Шлях abort (доданий після ревʼю): `instance_abort` звільняє ресурси
phase-scope для кожної нетермінальної доріжки ПЕРЕД примусовим переведенням
її у FAILED. Без цього track_tick робить ранній return на FAILED, і ресурси
фази витікають.

## Крос-модульний арбітраж (область MVP)

Engine арбітрує ТІЛЬКИ між сценаріями (handle). Конфлікти МІЖ сценарієм
ТА бізнес-модулем (наприклад, simple_thermo, що пише в той самий request-ключ
SharedState) **НЕ** арбітруються — діє правило last-write-wins на відповідному
записі SharedState.

**Відповідальність автора рецепту:**
- Якщо рецепт керує апаратним актуатором, вимкніть конкуруючі бізнес-модулі
  через дію `set_state` при вході у фазу (наприклад, `simple_thermo.enabled = false`)
- Увімкніть їх знову при завершенні / abort через exit-дії
- Обробники abort МАЮТЬ бути ідемпотентним повторним увімкненням —
  engine НЕ відновлює автоматично вимкнені модулі; автор рецепту явно
  тестує шлях abort

Це задокументоване обмеження MVP за планом Q8. Покращення Stage 1.5:
явний крос-модульний арбітраж через API Equipment Manager.

## Спостережуваність відновлення

Engine записує `scenario.engine_recovery_pending = true` після відновлення
сценарію, який утримував ресурси. UI відображає це через обмеження
`visible_when`; користувач явно обирає resume або abort. Без автовідновлення —
human-in-the-loop гарантує, що стан апаратури відповідає очікуванням рецепту.

(Stage 1.5 — наразі ключ recovery зарезервований у маніфесті engine, але
ще не записується кодом engine; додається одночасно з функцією
«recovery banner» у WebUI.)

## Опрацьований приклад: поливання теплиці

Два екземпляри рецепту керують 4 зонами. Насос спільний для всіх зон.

```jsonc
// Recipe "irrig_a" — зони 1+2
"scenario": {
  "resources": [
    {"resource": "zone.1", "exclusive": true},
    {"resource": "zone.2", "exclusive": true}
  ],
  "tracks": [{
    "name": "main",
    "phases": [{
      "name": "water_zone_1",
      "phase_resources": [{"resource": "pump", "exclusive": true}],
      "entry": [{"action": "set_state",
                 "params": {"key": "zone.1.valve", "type": "bool", "value": true}}],
      "transitions": [{"to": "water_zone_2", "when": {"time_elapsed_ms": 30000}}]
    }, {
      "name": "water_zone_2",
      "phase_resources": [{"resource": "pump", "exclusive": true}],
      "entry": [{"action": "set_state",
                 "params": {"key": "zone.2.valve", "type": "bool", "value": true}}],
      "transitions": [{"to": "$complete", "when": {"time_elapsed_ms": 30000}}]
    }]
  }]
}

// Recipe "irrig_b" — зони 3+4 (аналогічно, захоплює насос на кожну фазу)
```

Два екземпляри стартують одночасно. Обидва захоплюють зонально-специфічні
ресурси у scenario-scope (без конфлікту — різні зони). Обидва хочуть насос
у phase-scope:

- Екземпляр A починає поливати зону 1 — захоплює насос.
- Екземпляр B намагається почати поливати зону 3 — насос недоступний →
  переходить у `WAITING_FOR_RESOURCE`, накопичується phase_elapsed_ms.
- Екземпляр A завершує зону 1 (30 с) — вихід із фази звільняє насос.
- Наступний такт екземпляра B: `try_acquire_phase` успішний → виходить із WAITING.
- ...

Таймаут фази служить запобіжником: якщо насос ніколи не звільняється
(наприклад, екземпляр A завис), фаза екземпляра B зрештою провалюється,
сценарій робить abort.

## Дивіться також

- [adr/0005-isa88-resource-arbitration.md](adr/0005-isa88-resource-arbitration.md)
  — обґрунтування прийняття
- [03_api_reference.md](03_api_reference.md#resourcearbiter--isa-88-53)
  — публічний API
- [04_state_machines.md](04_state_machines.md) — поведінка стану WAITING_FOR_RESOURCE
- Джерело: `components/modesp_scenario/src/resource_arbiter.cpp`
