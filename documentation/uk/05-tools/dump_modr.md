# `dump_modr.py` — інспектор двійкових файлів `.modr`

> 📖 **In English:** [documentation/en/05-tools/dump_modr.md](../../en/05-tools/dump_modr.md)

Інспектор скомпільованих двійкових рецептів `.modr` у форматі, читаному
людиною. Аналог `objdump` для ELF або `protoc --decode` для protobuf.
Не є повним декомпілятором назад у JSON (планується на Stage 2 у
TypeScript разом із редактором WebUI) — це утиліта для відлагодження.

ВИМАГАЄ: Python 3.8+. Без зовнішніх залежностей.

```
python tools/dump_modr.py path/to/recipe.modr
python tools/dump_modr.py --hex path/to/recipe.modr
```

Прапор `--hex` додає сирий байтовий дамп поруч зі структурованим
виглядом.

## Коли використовувати

- **Збої HIL-тестів** — оглянути, який двійковий файл видав компілятор,
  щоб діагностувати неправильну поведінку рушія.
- **Розбіжність схеми та двійкового формату** — регресійне
  відлагодження еталонних файлів при зміні двійкового формату.
- **Міграція версій формату** — порівняння розкладок поруч.
- **Ручні перевірки осудливості** під час активної розробки рушія.

## Формат виводу

Зразок (скорочений):

```
== Header ==
magic       = 0x52444F4D ('MODR')
version     = 1
size_bytes  = 412
crc32       = 0xAABBCCDD
tracks_count = 2
phases_count = 4
...

== Tracks ==
[0] name='main'    flags=0x01 (MAIN)  phases=0x0040..0x0080
[1] name='watcher' flags=0x00         phases=0x0080..0x0094

== Phases ==
[0] track=0 name='phase_a' timeout=10000ms transitions=0x0100..0x010c
    actions: [log msg=...] [set_state key='test.output_a' bool=true]
[1] ...

== Transitions ==
[0] kind=TIME   target=phase[1] time_ms=1000
[1] kind=COND   target=phase[2] cond_hash=0xABCD
    params: [key='test.input_a' i32=10]
...

== String pool ==
0x0000: 'main'
0x0006: 'watcher'
0x000f: 'phase_a'
...
```

Вивід читається згори донизу, віддзеркалюючи розкладку двійкового
файлу. Кожна секція друкує свій абсолютний байтовий зсув, кількість
підзаписів та декодовані поля.

## Спеціальні маркери

- `$complete` — ціль переходу 0xFFFF (успіх сценарію).
- `$abort` — ціль переходу 0xFFFE (невдача сценарію).
- `NO_OFFSET` — заповнювач 0xFFFF, використовується для вказівників
  "немає дочірніх".

## Довідник формату

Розкладка двійкового файлу збігається із заголовками C++:

| Секція | Константа розміру у `modr_format.h` | Байти |
|---|---|---|
| Header | `SIZE_HEADER` | 56 |
| Track | `SIZE_TRACK` | 16 |
| Phase | `SIZE_PHASE` | 20 |
| Transition | `SIZE_TRANSITION` | 12 |
| Action | `SIZE_ACTION` | 8 |
| Param entry | `SIZE_PARAM_ENTRY` | 8 |
| Resource decl | `SIZE_RESOURCE_DECL` | 4 |

Порядок у файлі: заголовок → доріжки → фази → переходи → дії →
параметри → ресурси → пул рядків → хвостовий CRC32.

## Перевірка CRC

Інструмент перевіряє CRC32 при завантаженні. Якщо CRC у хвості не
збігається з обчисленим CRC байтів [0..size_bytes), він друкує
червону помилку і все ж дампить структуру за найкращих можливостей.
Використовуйте це, щоб ловити пошкоджені файли (зазвичай через невдалу
прошивку розділу).

## Декодування хешу

Посилання на дії та умови використовують хеші djb2_hash16. Інструмент
намагається повернути їх назад за допомогою `tools/known_actions.json`
— якщо хеш збігається з відомою дією, друкує ім'я; інакше залишає
сирий шістнадцятковий вигляд.

```
[0] action_hash=0x4d2a    # 'set_state'
[1] action_hash=0xff01    # <unknown — was the action removed from known_actions.json?>
```

Якщо бачите `<unknown>`, то або рецепт скомпільовано проти новішого
`known_actions.json`, ніж той, що в репозиторії, або дію було
перейменовано.

## Типові помилки

**`magic mismatch`:** файл не є `.modr` (обрізаний, неправильний формат
або неправильна версія). Перезапустіть `compile_scenario.py` і
перевірте вхідні дані.

**Обрізаний дамп:** якщо `size_bytes` у заголовку > фактичного розміру
файлу, файл неповний. Імовірно, невдала прошивка або частковий запис.

**Завеликий вивід:** великі рецепти можуть прокручуватися на кілька
екранів. Пропустіть через пейджер (`| less`) або перенаправте у файл
(`> dump.txt`).

## Що далі

- **[compile_scenario.md](compile_scenario.md)** — виробляє файли
  `.modr`, які оглядає цей інструмент.
- **[03-framework-reference/components/modesp_scenario.md](../03-framework-reference/components/modesp_scenario.md)** —
  рушій, що завантажує `.modr` під час виконання.

## Джерела

- [`tools/dump_modr.py`](../../../tools/dump_modr.py)
- [`components/modesp_scenario/include/modesp/scenario/modr_format.h`](../../../components/modesp_scenario/include/modesp/scenario/modr_format.h)
