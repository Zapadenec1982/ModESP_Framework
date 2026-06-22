<script>
  import { onDestroy } from 'svelte';
  import { writable } from 'svelte/store';
  import { setStateKey } from '../../stores/state.js';
  import { createSettingSender } from '../../lib/settings.js';
  import { wifiSsid } from '../../stores/wifiForm.js';
  import { mqttBroker, mqttUser, mqttPrefix } from '../../stores/mqttForm.js';

  export let config;
  export let value;

  // Text inputs store their value locally for form submission
  export let inputValue = '';

  // wifi/mqtt fields feed a local form store (a sibling Save button persists them); every
  // OTHER string key is a live readwrite field that POSTs the typed text to /api/settings.
  const FORM_STORES = {
    'wifi.ssid': wifiSsid,
    'mqtt.broker': mqttBroker,
    'mqtt.user': mqttUser,
    'mqtt.prefix': mqttPrefix,
  };
  const formStore = FORM_STORES[config.key];   // defined → wifi/mqtt form field; undefined → live field

  // SharedState strings are etl::string<32> → 31 usable chars; honour an explicit cap if given.
  $: maxlength = config.maxlength ?? 31;

  // Live field → debounced POST (createSettingSender), committed on blur/Enter (see onCommit).
  // form_only fields write the store/state locally (no POST); only true live fields get a sender.
  const sender = (formStore || config.form_only) ? null
    : createSettingSender(config.key, { endpoint: config.api_endpoint || '/api/settings' });
  const pending = sender ? sender.pending : writable(false);
  if (sender) onDestroy(sender.cleanup);

  // Seed the box from the server value ONCE (initial mount / late WS snapshot). After that the
  // user owns the field — re-seeding would snap a just-cleared field back to the old value, which
  // would make clearing (e.g. panel.message → resume rotation) impossible.
  let seeded = false;
  $: if (!seeded && value !== undefined && value !== null) {
    if (!inputValue) inputValue = String(value);
    seeded = true;
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
    if (formStore) formStore.set(inputValue);
  }

  // Live fields commit on change (blur/Enter), NOT per keystroke — avoids a POST per key + NVS wear.
  function onCommit() {
    if (formStore) return;                              // wifi/mqtt persist via their Save button
    if (config.form_only) setStateKey(config.key, inputValue);
    else if (sender) sender.send(inputValue);
  }

  function onKeydown(e) {
    if (e.key === 'Enter') e.target.blur();             // Enter → blur → change → onCommit
  }
</script>

<div class="input-widget">
  <div class="input-label">{config.description || config.key}</div>
  <div class="input-row">
    <input
      type="text"
      class="text-input"
      placeholder={config.description || ''}
      {maxlength}
      value={inputValue}
      on:input={onInput}
      on:change={onCommit}
      on:keydown={onKeydown}
    />
    {#if $pending}
      <span class="pending-dot"></span>
    {/if}
  </div>
</div>

<style>
  .input-widget { padding: 4px 0; }
  .input-label { font-size: 14px; color: var(--fg-muted); margin-bottom: 6px; }
  .input-row { display: flex; align-items: center; gap: 8px; position: relative; }
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
  .pending-dot {
    flex: 0 0 auto;
    width: 8px;
    height: 8px;
    border-radius: 50%;
    background: var(--warn);
    animation: pulse-dot 0.8s infinite;
  }
  @keyframes pulse-dot {
    0%, 100% { opacity: 1; }
    50% { opacity: 0.4; }
  }
</style>
