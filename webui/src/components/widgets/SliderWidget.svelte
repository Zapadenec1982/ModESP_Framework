<script>
  import { apiPost } from "../../lib/api.js";
  import { setStateKey } from "../../stores/state.js";
  import { toastError } from "../../stores/toast.js";

  export let config;
  export let value;

  $: min = config.min ?? -35;
  $: max = config.max ?? 0;
  $: step = config.step ?? 0.5;
  $: display = value !== undefined && value !== null ? Number(value) : null;

  let pending = false;
  let flashOk = false;
  let debounceTimer = null;

  function onInput(e) {
    const v = parseFloat(e.target.value);
    display = v;
    pending = true;
    if (debounceTimer) clearTimeout(debounceTimer);
    debounceTimer = setTimeout(() => sendValue(v), 300);
  }

  function onChange(e) {
    const v = parseFloat(e.target.value);
    display = v;
    if (debounceTimer) clearTimeout(debounceTimer);
    sendValue(v);
  }

  async function sendValue(v) {
    pending = true;
    setStateKey(config.key, v);
    try {
      await apiPost("/api/settings", { [config.key]: v });
      flashOk = true;
      setTimeout(() => {
        flashOk = false;
      }, 400);
    } catch (e) {
      toastError(e.message);
      display = value;
    } finally {
      pending = false;
    }
  }
</script>

<div class="slider-wrap" class:flash-ok={flashOk}>
  <div class="slider-header" style={config.hideHeader ? "display: none;" : ""}>
    <span class="label"
      >{config.description || config.key}{#if config.unit}
        ({config.unit}){/if}</span
    >
    <span class="slider-val">
      {display !== null ? Number(display).toFixed(1) : "—"}
      {#if pending}<span class="pending-dot"></span>{/if}
    </span>
  </div>
  <div class="rng-w">
    <input
      class="rng"
      type="range"
      {min}
      {max}
      {step}
      value={display ?? min}
      on:input={onInput}
      on:change={onChange}
      style="--pct: {(((display ?? min) - min) / (max - min)) * 100}%"
    />
  </div>
  <div class="slider-labels" style={config.hideLabels ? "display: none;" : ""}>
    <span>{min}</span>
    <span>{max}</span>
  </div>
</div>

<style>
  .slider-wrap {
    padding: 10px 0;
    border-bottom: 0.5px solid var(--border);
    border-left: 3px solid transparent;
    transition: border-color 0.2s;
  }
  .slider-wrap:last-child {
    border-bottom: none;
  }
  .slider-wrap.flash-ok {
    border-left-color: var(--ok);
  }
  .slider-header {
    display: flex;
    justify-content: space-between;
    align-items: baseline;
    margin-bottom: 6px;
  }
  .label {
    font-size: 13px;
    font-weight: 500;
    color: var(--text-2);
  }
  .slider-val {
    font-size: 18px;
    font-weight: 700;
    color: var(--accent);
    font-variant-numeric: tabular-nums;
    font-family: var(--font-mono);
    display: flex;
    align-items: center;
    gap: 6px;
  }
  .pending-dot {
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
    }
    50% {
      opacity: 0.3;
    }
  }

  .rng-w {
    position: relative;
    padding: 10px 0;
    width: 100%;
  }

  /* Custom Slider — quiet, doesn't compete with hero temperature */
  .rng {
    -webkit-appearance: none;
    appearance: none;
    width: 100%;
    height: 4px;
    background: var(--surface-3);
    border-radius: 2px;
    outline: none;
    cursor: pointer;
    position: relative;
    touch-action: manipulation;
  }

  .rng::before {
    content: "";
    position: absolute;
    top: 0;
    left: 0;
    height: 100%;
    width: var(--pct);
    background: var(--text-3);
    border-radius: 2px;
    pointer-events: none;
    transition: background 0.2s;
  }

  .rng:hover::before,
  .rng:active::before {
    background: var(--accent);
  }

  .rng::-webkit-slider-thumb {
    -webkit-appearance: none;
    appearance: none;
    width: 20px;
    height: 20px;
    border-radius: 50%;
    background: var(--text-2);
    border: 2px solid var(--surface);
    box-shadow: 0 1px 4px rgba(0,0,0,0.3);
    cursor: grab;
    position: relative;
    z-index: 2;
    transition: transform 0.15s, background 0.15s, box-shadow 0.15s;
  }

  .rng::-moz-range-thumb {
    width: 20px;
    height: 20px;
    border-radius: 50%;
    background: var(--text-2);
    border: 2px solid var(--surface);
    box-shadow: 0 1px 4px rgba(0,0,0,0.3);
    cursor: grab;
    position: relative;
    z-index: 2;
    transition: transform 0.15s, background 0.15s, box-shadow 0.15s;
  }

  .rng:hover::-webkit-slider-thumb {
    background: var(--accent);
    box-shadow: 0 0 8px var(--accent-glow);
  }
  .rng:hover::-moz-range-thumb {
    background: var(--accent);
    box-shadow: 0 0 8px var(--accent-glow);
  }

  .rng:active::-webkit-slider-thumb {
    transform: scale(1.1);
    background: var(--accent);
    cursor: grabbing;
  }
  .rng:active::-moz-range-thumb {
    transform: scale(1.1);
    background: var(--accent);
    cursor: grabbing;
  }

  .slider-labels {
    display: flex;
    justify-content: space-between;
    font-size: 10px;
    color: var(--text-4);
    margin-top: 2px;
    padding: 0 2px;
  }
</style>
