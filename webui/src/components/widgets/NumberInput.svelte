<script>
  import { onDestroy } from "svelte";
  import { setStateKey } from "../../stores/state.js";
  import { createSettingSender } from "../../lib/settings.js";

  export let config;
  export let value;

  $: min = config.min ?? 0;
  $: max = config.max ?? 100;
  $: step = config.step ?? 1;
  $: display = value !== undefined && value !== null ? value : "—";

  const sender = createSettingSender(config.key);
  const { pending, flashOk, cleanup } = sender;
  onDestroy(cleanup);

  function adjust(delta) {
    const cur = typeof value === "number" ? value : 0;
    const nv =
      Math.round(Math.max(min, Math.min(max, cur + delta)) * 100) / 100;
    if (config.form_only) {
      setStateKey(config.key, nv);
    } else {
      sender.send(nv);
    }
    try { navigator.vibrate(10); } catch (e) {}
  }

  function onInput(e) {
    const nv = Math.max(min, Math.min(max, parseFloat(e.target.value) || 0));
    if (config.form_only) {
      setStateKey(config.key, nv);
    } else {
      sender.send(nv);
    }
  }

  // Long-press auto-repeat with scroll guard
  let repeatTimer = null;
  let repeatInterval = null;
  let startY = null;
  let scrollCancelled = false;
  let initialDelay = null;
  let pendingDelta = null;
  let fired = false;
  const SCROLL_THRESHOLD = 8;

  function onPointerDown(e, delta) {
    startY = e.clientY;
    scrollCancelled = false;
    pendingDelta = delta;
    fired = false;
    // Delay first action to distinguish tap from scroll
    initialDelay = setTimeout(() => {
      if (!scrollCancelled) {
        fired = true;
        adjust(delta);
        repeatTimer = setTimeout(() => {
          let speed = 150;
          repeatInterval = setInterval(() => {
            adjust(delta);
            if (speed > 50) {
              clearInterval(repeatInterval);
              speed -= 20;
              repeatInterval = setInterval(() => adjust(delta), speed);
            }
          }, speed);
        }, 400);
      }
    }, 100);
  }

  function onPointerMove(e) {
    if (startY !== null && !scrollCancelled) {
      if (Math.abs(e.clientY - startY) > SCROLL_THRESHOLD) {
        scrollCancelled = true;
        stopRepeat();
      }
    }
  }

  function stopRepeat() {
    // Quick tap: if delay hasn't fired yet, do single adjust now
    if (!fired && !scrollCancelled && pendingDelta !== null) {
      adjust(pendingDelta);
    }
    if (initialDelay) { clearTimeout(initialDelay); initialDelay = null; }
    if (repeatTimer) { clearTimeout(repeatTimer); repeatTimer = null; }
    if (repeatInterval) { clearInterval(repeatInterval); repeatInterval = null; }
    startY = null;
    pendingDelta = null;
    fired = false;
  }

  onDestroy(stopRepeat);
</script>

<div class="stepper-row">
  <div class="step-lbl">{config.description || config.key}</div>
  <div class="stepper" class:flash-ok={$flashOk}>
    <button
      class="st-btn"
      on:pointerdown={(e) => onPointerDown(e, -step)}
      on:pointermove={onPointerMove}
      on:pointerup={stopRepeat}
      on:pointerleave={stopRepeat}
    >−</button>
    <input
      type="number"
      class="st-val"
      {min}
      {max}
      {step}
      value={display}
      on:change={onInput}
    />
    <button
      class="st-btn"
      on:pointerdown={(e) => onPointerDown(e, step)}
      on:pointermove={onPointerMove}
      on:pointerup={stopRepeat}
      on:pointerleave={stopRepeat}
    >+</button>
    {#if config.unit}
      <span class="unit">{config.unit}</span>
    {/if}
    {#if $pending}
      <span class="pending-dot"></span>
    {/if}
  </div>
</div>

<style>
  .stepper-row {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 10px 0;
    border-bottom: 0.5px solid var(--border);
  }

  .stepper-row:last-child {
    border-bottom: none;
  }

  .step-lbl {
    font-size: 13px;
    font-weight: 500;
    color: var(--text-2);
    flex: 1;
    min-width: 0;
  }

  .stepper {
    display: flex;
    align-items: center;
    gap: 0;
    background: none;
    border: none;
    box-shadow: none;
    padding: 0;
    position: relative;
  }

  .stepper.flash-ok .st-val {
    color: var(--ok);
    transition: color 0.15s;
  }

  .st-btn {
    width: 44px;
    height: 44px;
    border: none;
    background: transparent;
    color: var(--text-4);
    font-size: 20px;
    font-weight: 300;
    border-radius: 8px;
    cursor: pointer;
    display: flex;
    align-items: center;
    justify-content: center;
    transition: color 0.15s, background 0.15s;
    touch-action: manipulation;
    -webkit-tap-highlight-color: transparent;
  }

  .st-btn:hover {
    color: var(--accent);
    background: var(--surface-2);
  }

  .st-btn:active {
    color: var(--accent);
    transform: scale(0.88);
  }

  .st-val {
    width: 52px;
    text-align: center;
    font-size: 16px;
    font-weight: 600;
    letter-spacing: -0.5px;
    font-variant-numeric: tabular-nums;
    font-family: var(--font-mono);
    color: var(--text-1);
    background: transparent;
    border: none;
    -moz-appearance: textfield;
  }

  .st-val:focus {
    outline: none;
    color: var(--accent);
  }

  .st-val::-webkit-inner-spin-button,
  .st-val::-webkit-outer-spin-button {
    -webkit-appearance: none;
    margin: 0;
  }

  .unit {
    font-size: 11px;
    color: var(--text-4);
    margin-left: 2px;
    min-width: 24px;
    white-space: nowrap;
  }

  .pending-dot {
    position: absolute;
    top: -4px;
    right: -4px;
    width: 8px;
    height: 8px;
    border-radius: 50%;
    background: var(--warn);
    animation: pulse-dot 0.8s infinite;
  }

  @keyframes pulse-dot {
    0%,
    100% {
      opacity: 1;
      box-shadow: 0 0 6px var(--warn-dim);
    }
    50% {
      opacity: 0.4;
      box-shadow: none;
    }
  }
</style>
