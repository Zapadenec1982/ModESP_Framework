# `player` — аудіо-плеєр (аларм-тони + WAV/MP3-кліпи)

> 📖 **In English:** [documentation/en/03-framework-reference/modules/player.md](../../../en/03-framework-reference/modules/player.md)

Бізнес-модуль звуку: генерує аларм-тони (синус із LUT + фазовий акумулятор) і відтворює WAV/MP3-кліпи з LittleFS (`/data/audio/<name>`). Джерела зводяться до 16-бітного моно-PCM, до нього застосовується програмна гучність, і буфери віддаються у здатність `audio` — сьогодні це підсилювач [`max98357a`](../drivers/max98357a.md) по I²S, завтра будь-який інший sink. Модуль володіє окремим playback-таском (FreeRTOS), який єдиний торкається sink; `on_update()` лише кладе запити в чергу й дзеркалить atomics у SharedState.

**Роль = здатність (R0.1).** `player` оголошує роль `audio_main` зі здатністю `capability: "audio"` і ніколи не знає, який драйвер її дає — резолв іде за роллю через прив'язку. Аудіо-вихід опційний (`"optional": true`): без прив'язки модуль підставляє `NullSink` і тихо приймає команди (нічого не грає, система не падає).

## Потік даних

```
player.play / player.btn_alarm / player.btn_beep
                    │
              (черга запитів)
                    │
            playback-таск ── Tone (sine LUT) ─┐
                          └─ Clip (WAV/MP3) ──┼─▶ 16-bit mono PCM ─▶ гучність ─▶ IAudioSink.write()
                                              │                                        │
                    /data/audio/<name>.wav|.mp3                              max98357a (I²S) ─▶ динамік
                    │
      player.playing / player.clip  ◀── дзеркало стану ── on_update()
```

Очікувана роль у bindings: `audio_main` (капабіліті `audio`, драйвер `max98357a`, `hardware_type: i2s_bus`). Одна I²S-шина = один аудіо-вихід — sink синглтонний (див. [`drivers/max98357a.md`](../drivers/max98357a.md)).

## Команди й керування (web/MQTT, momentary)

Оголошені як module state-ключі (`access: readwrite`) — генератор зашиває їх у веб/MQTT автоматично. Командні ключі **самоскидні**: модуль споживає їх і повертає у порожнє/`false`.

| Ключ | Тип | Опис |
|---|---|---|
| `player.play` | string | Що грати: `alarm` / `beep` / назва WAV- або MP3-кліпа (без розширення → `.wav`). |
| `player.stop` | bool | Зупинити відтворення. |
| `player.btn_alarm` | bool | Тест: аларм-тон (2 кГц, переривчастий, safety-cap 120 с). |
| `player.btn_beep` | bool | Тест: короткий біп (2 кГц, 200 мс). |

## Налаштування (web, зберігаються)

`access: readwrite`, `persist: true` — генератор зашиває у веб/NVS/MQTT.

| Ключ | Тип | Деф. | Опис |
|---|---|---|---|
| `player.enabled` | bool | true | Звук увімк/вимк. Вимкнення на льоту зупиняє активний кліп. |
| `player.volume` | int 0-100 %, крок 5 | 70 | Гучність (**програмна** — MAX98357A не має регістра гучності; масштабується при заповненні буфера, 0-100 % → 0-256). |

## Індикація (read-only стан)

| Ключ | Опис |
|---|---|
| `player.playing` | Стан відтворення (`Грає`/`Тиша`). |
| `player.clip` | Поточний/останній кліп або тон. |

## Кліпи, тони, пріоритети

- **Тони** генеруються з sine-LUT — асет не потрібен. `alarm` — переривчастий гейт (200 мс on / 400 мс період), `beep` — одиночні 200 мс.
- **Кліпи** стрімляться з `/data/audio/<name>`: розширення обирає декодер — `.mp3` → Helix-декодер, інакше `.wav` (16-біт PCM моно). Без розширення додається `.wav`.
- **Пріоритет:** активний `alarm` (`PRIO_ALARM`) не перебивається нижчими запитами; `stop` і рівні/вищі запити перебивають.

> ℹ️ **Примітка:** MP3 компілюється лише з `CONFIG_MODESP_AUDIO_MP3` (деф. `y`). При вимкненні `.mp3`-кліпи відхиляються, WAV і тони працюють.

## Прив'язки

Аудіо-вихід — дротова I²S-шина з board.json, прив'язана до `max98357a`, роль `audio_main`, модуль `player`.

### board.json

```json
"i2s_buses": [
  { "id": "i2s_0", "bclk": 15, "ws": 16, "dout": 7, "sd": 17, "sample_rate_hz": 16000 }
]
```

### bindings.json

```json
[
  { "hardware": "i2s_0", "driver": "max98357a", "role": "audio_main", "module": "player" }
]
```

`module` мусить називати `player` (власника ролі, R1.2/R3.4), інакше білд падає. Здатність — `audio`; матч ролі й каналу лише за capability (R3.1). WAV/MP3-асети покладіть у `data/audio/`.

## Опційність

Модуль у `project.json` → компілюється завжди (легкий, інертний без sink). Сам **драйвер** опційний через `CONFIG_MODESP_DRIVER_MAX98357A`; MP3-декодер — через `CONFIG_MODESP_AUDIO_MP3`. Без аудіо-прив'язки — `NullSink` (команди приймаються, звуку немає).

## Типові помилки

- **Тиша, у логах `NullSink`** — немає bindings-запису `audio_main`→`max98357a`, або драйвер вимкнений у menuconfig. Узгодьте `tools/drivers_sync.py --fix`.
- **Кліп не грає** — файлу немає в `/data/audio/` (перевірте свіжість `data.bin`: білдьте через `run_build.ps1`, R8.1), або WAV не 16-біт PCM моно.
- **`.mp3` відхиляється** — вимкнений `CONFIG_MODESP_AUDIO_MP3`.

## Джерела

- [`modules/player/manifest.json`](../../../../modules/player/manifest.json)
- [`modules/player/src/player_module.cpp`](../../../../modules/player/src/player_module.cpp) — playback-таск, тони, гучність, вибір декодера.
- [`modules/player/src/wav_decoder.cpp`](../../../../modules/player/src/wav_decoder.cpp) / [`mp3_decoder.cpp`](../../../../modules/player/src/mp3_decoder.cpp)
- [`drivers/max98357a.md`](../drivers/max98357a.md) — драйвер аудіо-виходу (`IAudioSink`, I²S).
- [`rules.md`](../rules.md) — R0.1 (роль=capability), R3.1/R3.3/R3.4, R5.2.
- [`project-hierarchy.md`](../project-hierarchy.md) — маршрут периферії Module↔Role↔Device↔Binding.
