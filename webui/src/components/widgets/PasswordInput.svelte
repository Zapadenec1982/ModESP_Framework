<script>
  import { t } from '../../stores/i18n.js';
  import { wifiPassword } from '../../stores/wifiForm.js';
  import { mqttPass } from '../../stores/mqttForm.js';

  export let config;
  export let value;

  export let inputValue = '';
  let revealed = false;

  $: if (value !== undefined && value !== null && !inputValue) {
    inputValue = String(value);
  }

  // Read-back від зовнішніх store змін (MqttSave onMount)
  $: if (config.key === 'mqtt.password' && $mqttPass !== inputValue) inputValue = $mqttPass;

  // Sync до store СИНХРОННО при вводі — до reactive cycle
  function onInput(e) {
    inputValue = e.target.value;
    if (config.key === 'wifi.password') wifiPassword.set(inputValue);
    else if (config.key === 'mqtt.password') mqttPass.set(inputValue);
  }
</script>

<div class="input-widget">
  <div class="input-label">{config.description || config.key}</div>
  <div class="password-row">
    {#if revealed}
      <input
        type="text"
        class="text-input"
        placeholder={config.description || ''}
        value={inputValue}
        on:input={onInput}
      />
    {:else}
      <input
        type="password"
        class="text-input"
        placeholder={config.description || ''}
        value={inputValue}
        on:input={onInput}
      />
    {/if}
    <button class="reveal-btn" on:click={() => revealed = !revealed}>
      {revealed ? $t['pass.hide'] : $t['pass.show']}
    </button>
  </div>
</div>

<style>
  .input-widget { padding: 4px 0; }
  .input-label { font-size: 14px; color: var(--fg-muted); margin-bottom: 6px; }
  .password-row { display: flex; gap: 6px; }
  .text-input {
    flex: 1;
    background: var(--bg);
    border: 1px solid var(--border);
    color: var(--fg);
    border-radius: 8px;
    padding: 10px 12px;
    font-size: 14px;
    transition: border-color 0.2s;
  }
  .text-input:focus { border-color: var(--accent); outline: none; }
  .reveal-btn {
    padding: 0 12px;
    white-space: nowrap;
    border: 1px solid var(--border);
    background: var(--border);
    border-radius: 8px;
    cursor: pointer;
    font-size: 13px;
    color: var(--fg-muted);
    display: flex; align-items: center; justify-content: center;
  }
  .reveal-btn:hover { background: var(--bg-hover); }
</style>
