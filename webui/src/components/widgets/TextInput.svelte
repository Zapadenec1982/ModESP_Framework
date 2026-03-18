<script>
  import { wifiSsid } from '../../stores/wifiForm.js';
  import { mqttBroker, mqttUser, mqttPrefix } from '../../stores/mqttForm.js';

  export let config;
  export let value;

  // Text inputs store their value locally for form submission
  export let inputValue = '';

  $: if (value !== undefined && value !== null && !inputValue) {
    inputValue = String(value);
  }

  // Read-back від зовнішніх store змін (WifiScan.selectNetwork, MqttSave onMount)
  $: if (config.key === 'wifi.ssid' && $wifiSsid !== inputValue) inputValue = $wifiSsid;
  $: if (config.key === 'mqtt.broker' && $mqttBroker !== inputValue) inputValue = $mqttBroker;
  $: if (config.key === 'mqtt.user' && $mqttUser !== inputValue) inputValue = $mqttUser;
  $: if (config.key === 'mqtt.prefix' && $mqttPrefix !== inputValue) inputValue = $mqttPrefix;

  // Sync до store СИНХРОННО при вводі — до reactive cycle
  // (Svelte reorders $: reactives при компіляції, тому bind:value + $: write
  //  ламався: READ з store скидав inputValue до старого значення ДО WRITE)
  function onInput(e) {
    inputValue = e.target.value;
    if (config.key === 'wifi.ssid') wifiSsid.set(inputValue);
    else if (config.key === 'mqtt.broker') mqttBroker.set(inputValue);
    else if (config.key === 'mqtt.user') mqttUser.set(inputValue);
    else if (config.key === 'mqtt.prefix') mqttPrefix.set(inputValue);
  }
</script>

<div class="input-widget">
  <div class="input-label">{config.description || config.key}</div>
  <input
    type="text"
    class="text-input"
    placeholder={config.description || ''}
    value={inputValue}
    on:input={onInput}
  />
</div>

<style>
  .input-widget { padding: 4px 0; }
  .input-label { font-size: 14px; color: var(--fg-muted); margin-bottom: 6px; }
  .text-input {
    background: var(--bg);
    border: 1px solid var(--border);
    color: var(--fg);
    border-radius: 8px;
    padding: 10px 12px;
    width: 100%;
    font-size: 14px;
    transition: border-color 0.2s;
  }
  .text-input:focus { border-color: var(--accent); outline: none; }
</style>
