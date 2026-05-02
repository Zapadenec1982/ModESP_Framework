# 05 — Cross-Track Synchronization (Tick-Order Semantics)

**Status:** placeholder. Заповнюється у Step 13 (test_track_synchronization).

## Заповнюється

- **Tick-order semantics** primary contract:
  - Engine ticks instances у declaration order
  - Within instance, tracks tick у declaration order
  - Кожен `state_get()` reads SharedState fresh
  - Track A's writes visible до Track B якщо B ticks LATER у same tick
  - All writes visible до всіх tracks на наступному tick
- **Author guidelines:** declare tracks producer-before-consumer order
- **Worked examples** (compile-and-trace):
  - Two tracks де A signals B з state_key
  - Two tracks де race condition можлива (same-tick mutual writes)
  - Three tracks з deterministic chain
- **Edge cases:**
  - Track read-then-write same key — own write visible after this tick
  - Multiple tracks writing same key — last writer у tick wins
- **Anti-patterns:** circular wait, instant-visibility expectation

## Reference

- ADR-0003 для tick-order rationale (vs snapshot alternative).
- Plan Q6 для current spec.
