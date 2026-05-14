# 04 — Скінченні автомати

> 📖 **In English:** [documentation/en/03-framework-reference/scenario-engine/04_state_machines.md](../../../en/03-framework-reference/scenario-engine/04_state_machines.md)

Рушій має два шари скінченних автоматів: **посценарний** (на рівні екземпляра)
та **подоріжковий** (у межах сценарію). Обидва реалізовані у `track.cpp`
та `instance.cpp`.

## Скінченний автомат сценарію

`SequenceRuntime::State` (визначений у `runtime_types.h`):

```
                                  ┌──────────────────────────┐
                                  │                          │
                                  │     pause()              │
                                  │     ◀──────              │
                                  │                          │
              load_buffer/        │                          │
              load_path           │                          │
   ┌───────┐  ──────────▶ ┌───────┴────┐  start()  ┌─────────▼──┐
   │ IDLE  │              │   LOADED   │ ────────▶ │  RUNNING   │
   └───────┘              └────────────┘            └────┬───────┘
       ▲                        │                       │
       │                        │ unload()              │ resume()
       │                        ▼                       │
       │                  ┌─────────────┐               │
       │                  │   IDLE      │               │
       │                  └─────────────┘               │
       │                                                │
       │ unload()                                       │
       ├─────────────────────────────────────┐          │
       │                                     │          ▼
   ┌───┴────────┐  abort() / global trans     │   ┌──────────┐
   │ COMPLETED  │     ◀────────              ┌─┤  ABORTING  │
   └────────────┘                            │ └──────────┘
                                             │
                                             │ усі доріжки термінальні
                                             ▼
                                       ┌─────────┐
                                       │ FAILED  │
                                       └─────────┘
                                             │
                                             │ unload()
                                             ▼
                                          IDLE
```

### Таблиця переходів

| Звідки | Подія | Куди | Побічні ефекти |
|--------|-------|------|----------------|
| IDLE | `load_buffer` / `load_path` успішно | LOADED | Буфер скопійовано у слот, `modr_validate` пройшов |
| LOADED | `start()` успішно | RUNNING | Ресурси на рівні сценарію захоплено атомарно; доріжки переведено у RUNNING із `initial_phase` |
| LOADED | `start()` конфлікт ресурсу | LOADED | Зміни стану нема; повертається `RESOURCE_CONTENDED` |
| RUNNING | `pause()` | PAUSED | `instance_tick` для цього слота нічого не робить |
| PAUSED | `resume()` | RUNNING | Доріжки відновлюються з тих самих `phase_idx` + `phase_elapsed_ms` |
| RUNNING | `abort()` / глобальний перехід | ABORTING | Доріжки примусово у FAILED; фазові ресурси вивільнено для кожної доріжки |
| RUNNING | `completion_rule` виконано | COMPLETED | Ресурси на рівні сценарію вивільнено |
| RUNNING | Головна доріжка FAILED (з прапорцем) | FAILED | Ресурси на рівні сценарію вивільнено |
| ABORTING | Усі доріжки термінальні | FAILED | Ресурси на рівні сценарію вивільнено |
| Будь-який не-IDLE | `unload()` | IDLE | Усі ресурси вивільнено, слот очищено |

### Обчислення `completion_rule`

Встановлюється у заголовку рецепта через `scenario.completion_rule`:

| Значення | Умова спрацювання |
|----------|------------------|
| `all_tracks_complete` (0) | Кожна доріжка у стані COMPLETED |
| `any_track_complete` (1) | Принаймні одна доріжка у стані COMPLETED |
| `main_track_complete` (2) | Доріжка з `MODR_TRACK_FLAG_MAIN` у COMPLETED |

Рушій обчислює правило після кожного `instance_tick`. Якщо головна доріжка
у FAILED, сценарій → FAILED незалежно від completion_rule (властивість безпеки —
відмова головної доріжки завжди термінальна для сценарію).

## Скінченний автомат доріжки

`TrackRuntime::State`:

```
   ┌───────┐  instance_start()
   │ IDLE  │ ──────────────▶ ┌─────────────┐
   └───────┘                 │   RUNNING   │ ◀────────┐
                             └──────┬──────┘          │
                                    │                 │
                          phase_resource_n > 0         │ успіх захоплення
                          AND try_acquire fails        │
                                    │                 │
                                    ▼                 │
                         ┌──────────────────────┐     │
                         │ WAITING_FOR_RESOURCE │ ────┘
                         └─────────┬────────────┘
                                    │
                                    │ тайм-аут фази
                                    ▼
                                ┌─────────┐
                                │ FAILED  │
                                └─────────┘

   RUNNING ──────────────▶ ┌──────────────┐
   (спрацював фазовий           │   ABORTING   │
    $abort-перехід)              └──────┬───────┘
                                     │
                                     │ усі exit-дії завершені
                                     ▼
                                 ┌─────────┐
                                 │ FAILED  │
                                 └─────────┘

   RUNNING ───────────────────▶ ┌────────────┐
   (ціль переходу == COMPLETE)    │ COMPLETED  │
                                  └────────────┘

   RUNNING ───────────────────▶ FAILED
   (дія повертає FAILED_ABORT)

   Будь-який стан ─────────────▶ FAILED
   (переривання на рівні сценарію: instance_abort встановлює прямо у FAILED,
    попередньо вивільнивши фазові ресурси)
```

### Покроковий алгоритм такту (track_tick)

Псевдокод відповідає `track.cpp::track_tick`:

```
if state ∈ {IDLE, COMPLETED, FAILED}: return  # термінальний або сплячий

phase_elapsed_ms += dt_ms (з насиченням)

if state == WAITING_FOR_RESOURCE:
    if у фази є phase_resources:
        if try_acquire_phase(...):
            state = RUNNING                # провалитися далі
        else:
            if phase_elapsed_ms >= timeout: state = FAILED
            return
    else:
        state = RUNNING

# Тепер state ∈ {RUNNING, ABORTING}
if running_exit_actions:
    if exit_action_progress < phase.exit_action_n:
        викликати exit-дію
        return  # одна дія за такт
    застосувати залатчений перехід (просунути фазу / завершити / перервати)
    return

# Виконуємо entry-дії по одній за такт
if entry_action_progress < phase.entry_action_n:
    s = викликати entry-дію
    обробити (s) за політикою Q12 (OK — далі, PENDING — повтор, FAILED — abort/recover)
    return  # одна дія за такт

# Усі entry-дії виконано — обчислюємо переходи
for trans у phase.transitions:
    if transition_fires(trans):
        залатчити trans.target_phase, встановити running_exit_actions = true
        return

# Жоден перехід не спрацював — перевіряємо тайм-аут фази
if timeout != 0 AND phase_elapsed_ms >= timeout:
    state = FAILED
    вивільнити фазові ресурси
```

### Просування дій — одна за такт

Як entry-, так і exit-дії обробляють щонайбільше ОДНУ за один такт рушія.
При типовому періоді такту 10 мс фаза з 5 entry-діями виконуватиметься
5 тактів (50 мс) до початку обчислення переходів. Це передбачувано обмежує
витрати CPU на один такт.

Дія `PENDING` утримує доріжку на тій самій дії впродовж кількох тактів
(напр., перетворення DS18B20 ~750 мс = 75 PENDING-тактів). Довгі PENDING-дії
варто замінити явним переходом `wait_ms` за часом, після якого йде швидка дія.

## Семантика синхронізації між доріжками

Рушій тактує доріжки у межах сценарію В ПОРЯДКУ ОГОЛОШЕННЯ. У межах одного такту:

```
такт N:
  ┌─ Такт екземпляра сценарію N (handle 1):
  │  ├─ Обчислення глобальних переходів (за пріоритетом)
  │  ├─ Такт доріжки 0 (записи у SharedState видимі доріжці 1)
  │  ├─ Такт доріжки 1 (читає свіжий SharedState — бачить записи доріжки 0)
  │  └─ Перевірка правила завершення
  │
  ├─ Такт екземпляра сценарію N+1 (handle 2): ...
  └─ ...
```

Читання — **живі** (без снапшотів). Автори рецептів повинні оголошувати
доріжки-виробники перед доріжками-споживачами для детермінованої видимості
у межах одного такту. Див. [05_synchronization.md](05_synchronization.md)
для розгорнутих прикладів.

## Глобальні переходи

Обчислюються ПЕРШИМИ кожного такту, до тактів окремих доріжок. Сортуються
за пріоритетом (за спаданням — вищий пріоритет спрацьовує першим). При збігу:

| Область | Ефект |
|---------|-------|
| `abort_scenario` (0) | `instance_abort` — усі нетермінальні доріжки → FAILED, фазові ресурси вивільнено |
| `abort_main_track` (1) | Лише доріжка з прапорцем main_track → FAILED + вивільнення фазових ресурсів |

Глобальні переходи не мають порогів за часом — лише виразів умов. Безумовні
глобальні переходи (`kind == UNCONDITIONAL`) спрацьовують на першому такті,
що зручно для одноразових тригерів переривання, керованих прапорцями SharedState.

## Перехресні посилання

- [05_synchronization.md](05_synchronization.md) — повна семантика порядку тактів
- [06_resource_arbitration.md](06_resource_arbitration.md) — коли входять/виходять із WAITING_FOR_RESOURCE
- [10_error_model.md](10_error_model.md#action-failure-policy-machine) — деталі політики відмов дій
- [adr/0003-tick-order-sync-semantics.md](adr/0003-tick-order-sync-semantics.md) — обґрунтування дизайну
- [adr/0007-mandatory-phase-timeouts.md](adr/0007-mandatory-phase-timeouts.md) — чому тайм-аути обов'язкові
- Джерела: `components/modesp_scenario/src/track.cpp`,
  `instance.cpp`
