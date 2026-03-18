<script>
  import { onDestroy } from "svelte";
  import { setStateKey } from "../../stores/state.js";
  import { createSettingSender } from "../../lib/settings.js";

  export let config;
  export let value;

  $: isOn = !!value;

  const sender = createSettingSender(config.key, { debounceMs: 0 });
  const { pending, cleanup } = sender;
  onDestroy(cleanup);

  function toggle() {
    const nv = !isOn;
    if (config.form_only) {
      setStateKey(config.key, nv);
    } else {
      sender.send(nv);
    }
  }
</script>

<div class="widget-row" class:is-pending={$pending}>
  <span class="label">{config.description || config.key}</span>
  <button class="toggle" class:on={isOn} on:click={toggle} disabled={$pending}>
    <span class="toggle-thumb"></span>
  </button>
</div>

<style>
  .widget-row {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 10px 0;
    border-bottom: 0.5px solid var(--border);
    transition: opacity 0.2s;
  }
  .widget-row:last-child {
    border-bottom: none;
  }
  .widget-row.is-pending {
    opacity: 0.6;
  }
  .label {
    font-size: 13px;
    font-weight: 500;
    color: var(--text-2);
    flex: 1;
    min-width: 0;
  }
  .toggle {
    width: 56px;
    height: 32px;
    border-radius: 16px;
    border: none;
    background: var(--surface-3);
    cursor: pointer;
    position: relative;
    transition:
      background 0.3s cubic-bezier(0.16, 1, 0.3, 1),
      box-shadow 0.3s;
    padding: 0;
    flex-shrink: 0;
    box-shadow: inset 0 2px 6px rgba(0, 0, 0, 0.2);
    touch-action: manipulation;
    -webkit-tap-highlight-color: transparent;
  }
  .toggle:disabled {
    cursor: wait;
  }
  .toggle.on {
    background: var(--ok);
    box-shadow:
      inset 0 2px 6px rgba(0, 0, 0, 0.1),
      0 0 12px var(--ok-glow);
  }
  .toggle-thumb {
    position: absolute;
    top: 3px;
    left: 3px;
    width: 26px;
    height: 26px;
    border-radius: 50%;
    background: var(--text-3);
    box-shadow: 0 2px 4px rgba(0, 0, 0, 0.2);
    transition: all 0.3s cubic-bezier(0.16, 1, 0.3, 1);
  }
  .toggle.on .toggle-thumb {
    transform: translateX(24px);
    background: #fff;
  }
</style>
