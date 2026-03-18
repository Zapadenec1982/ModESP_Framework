<script>
  import { onDestroy } from "svelte";
  import { setStateKey } from "../../stores/state.js";
  import { state } from "../../stores/state.js";
  import { createSettingSender } from "../../lib/settings.js";

  export let config;
  export let value;

  $: options = config.options || [];
  $: disabled = config.disabled || false;
  $: current = value !== undefined && value !== null ? value : "";

  // Runtime: перевірка requires_state через $state
  $: enriched = options.map((opt) => ({
    ...opt,
    isDisabled: opt.requires_state ? !$state[opt.requires_state] : false,
  }));

  // Hint для поточного disabled значення (якщо NVS має старе значення)
  $: currentOpt = enriched.find((o) => o.value === current);
  $: hint = currentOpt?.isDisabled ? currentOpt.disabled_hint : null;

  const sender = createSettingSender(config.key, {
    debounceMs: 0,
    endpoint: config.api_endpoint || "/api/settings",
  });
  const { pending, cleanup } = sender;
  onDestroy(cleanup);

  function onChange(e) {
    const nv = parseInt(e.target.value);
    if (isNaN(nv)) return;
    sender.send(nv);
  }
</script>

<div class="select-widget" class:disabled class:is-pending={$pending}>
  <div class="select-label">{config.description || config.key}</div>
  <div class="sel-w">
    <select
      class="select-input"
      value={current}
      on:change={onChange}
      disabled={disabled || $pending}
    >
      {#each enriched as opt}
        <option
          value={opt.value}
          disabled={opt.isDisabled}
          title={opt.isDisabled && opt.disabled_hint ? opt.disabled_hint : ""}
        >
          {opt.label}
        </option>
      {/each}
    </select>
    <svg class="sel-arr" width="16" height="16" viewBox="0 0 24 24" fill="none"
      stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
      <polyline points="6 9 12 15 18 9"/>
    </svg>
  </div>
  {#if hint}
    <div class="disabled-hint">{hint}</div>
  {/if}
  {#if disabled && config.disabled_reason}
    <div class="disabled-reason">{config.disabled_reason}</div>
  {/if}
</div>

<style>
  .select-widget {
    padding: 10px 0;
    border-bottom: 0.5px solid var(--border);
    transition: opacity 0.2s;
  }
  .select-widget:last-child {
    border-bottom: none;
  }
  .select-widget.disabled {
    opacity: 0.5;
  }
  .select-widget.is-pending {
    opacity: 0.6;
  }
  .select-label {
    font-size: 13px;
    font-weight: 500;
    color: var(--text-2);
    margin-bottom: 6px;
  }
  .sel-w {
    position: relative;
  }
  .select-input {
    width: 100%;
    padding: 10px 36px 10px 12px;
    font-size: 14px;
    min-height: 44px;
    background: var(--surface-2);
    border: 1px solid var(--border);
    border-radius: var(--radius-sm);
    color: var(--text-1);
    cursor: pointer;
    appearance: none;
    -webkit-appearance: none;
    font-family: inherit;
    transition: all var(--transition-fast);
    touch-action: manipulation;
    -webkit-tap-highlight-color: transparent;
  }
  .select-input:hover:not(:disabled) {
    border-color: var(--border-accent);
    background: var(--surface-3);
  }
  .select-input:disabled {
    cursor: not-allowed;
  }
  .select-input:focus {
    outline: none;
    border-color: var(--accent);
    box-shadow: 0 0 0 2px var(--accent-glow-s);
  }
  .select-input option {
    background: var(--surface);
    color: var(--text-1);
  }
  .select-input option:disabled {
    color: var(--text-4);
    text-decoration: line-through;
  }
  .sel-arr {
    position: absolute;
    right: 10px;
    top: 50%;
    transform: translateY(-50%);
    color: var(--text-3);
    pointer-events: none;
  }
  .disabled-hint {
    font-size: 11px;
    color: var(--warn);
    margin-top: 4px;
  }
  .disabled-reason {
    font-size: 11px;
    color: var(--danger);
    margin-top: 4px;
  }
</style>
