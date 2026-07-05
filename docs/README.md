# `docs/` — інженерні нотатки (raw)

Це **не** користувацька документація. Тут — сирі інженерні матеріали: ADR, reverse-engineering протоколів, дослідження чипів, плани фаз. Вони можуть бути частково застарілими й пишуться «як думаємо», не «як пояснюємо».

**Авторитетна, підтримувана документація — у [`documentation/`](../documentation/)** (uk + en дзеркало). Починай звідти:
- ⭐ [rules.md](../documentation/uk/03-framework-reference/rules.md) — звід правил фреймворку.
- [project-hierarchy.md](../documentation/uk/03-framework-reference/project-hierarchy.md) — ієрархія + маршрут периферії.
- [drivers/](../documentation/uk/03-framework-reference/drivers/) · [modules/](../documentation/uk/03-framework-reference/modules/) — довідка по кожному.

## Що тут лежить
| Тека | Зміст | Пов'язаний user-doc |
|------|-------|---------------------|
| `amt630a/` | AMT630A OSD-чип: control reference, driver design, power modes, ADR-001 (OSD-notifications), research/ | [drivers/amt630a.md](../documentation/uk/03-framework-reference/drivers/amt630a.md) |
| `display/` | ADR-002 (display-архітектура, семантичний шов IDisplayPort), ADR-003, review | [modules/display.md](../documentation/uk/03-framework-reference/modules/display.md) |
| `ble/` | iPixel NimBLE spec, panel protocol, BTHome observer spec, phase-плани | [drivers/ble_led_panel.md](../documentation/uk/03-framework-reference/drivers/ble_led_panel.md), [ble_xiaomi_th.md](../documentation/uk/03-framework-reference/drivers/ble_xiaomi_th.md) |
| `UNIVERSALITY_AUDIT_*.md` | разові аудити | — |

**Правило:** нова стабільна інформація → у `documentation/`. `docs/` лишається чернетковим шаром; ключові ADR лінкуй із відповідного user-doc.
