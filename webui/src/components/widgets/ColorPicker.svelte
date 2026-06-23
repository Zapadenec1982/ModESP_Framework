<script>
  import { onDestroy } from 'svelte';
  import { createSettingSender } from '../../lib/settings.js';

  export let config;
  export let value;

  const sender = createSettingSender(config.key, {
    debounceMs: 0,
    endpoint: config.api_endpoint || '/api/settings',
  });
  const { pending, cleanup } = sender;
  onDestroy(cleanup);

  // <input type="color"> needs a valid #rrggbb; fall back to white for missing/invalid state.
  $: hex = (typeof value === 'string' && /^#[0-9a-fA-F]{6}$/.test(value)) ? value : '#ffffff';

  function onChange(e) {
    sender.send(e.target.value);   // "#rrggbb"
  }
</script>

<div class="color-widget" class:is-pending={$pending}>
  <div class="color-label">{config.description || config.key}</div>
  <div class="color-row">
    <input
      type="color"
      class="color-input"
      value={hex}
      on:change={onChange}
    />
    <span class="color-hex">{hex.toUpperCase()}</span>
  </div>
</div>

<style>
  .color-widget {
    padding: 10px 0;
    border-bottom: 0.5px solid var(--border);
    transition: opacity 0.2s;
  }
  .color-widget:last-child { border-bottom: none; }
  .color-widget.is-pending { opacity: 0.6; }
  .color-label {
    font-size: 13px;
    font-weight: 500;
    color: var(--text-2);
    margin-bottom: 6px;
  }
  .color-row {
    display: flex;
    align-items: center;
    gap: 12px;
  }
  .color-input {
    width: 52px;
    height: 36px;
    padding: 0;
    border: 1px solid var(--border);
    border-radius: var(--radius-sm);
    background: var(--surface-2);
    cursor: pointer;
    touch-action: manipulation;
    -webkit-tap-highlight-color: transparent;
  }
  .color-input::-webkit-color-swatch-wrapper { padding: 3px; }
  .color-input::-webkit-color-swatch { border: none; border-radius: 4px; }
  .color-hex {
    font-family: var(--font-mono);
    font-size: 13px;
    color: var(--text-3);
    font-variant-numeric: tabular-nums;
  }
</style>
