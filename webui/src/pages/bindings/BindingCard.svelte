<script>
  import { createEventDispatcher } from 'svelte';
  import { t } from '../../stores/i18n.js';
  import DiscoveryPicker from './DiscoveryPicker.svelte';

  export let roleDef;
  export let binding;
  export let hwList = [];
  export let usedIds = new Set();
  export let assignedAddresses = new Set();

  const dispatch = createEventDispatcher();

  // A role's address is set one of two ways, both filling binding.address:
  //  1) scan picker — the driver discovers per-role channels on a shared bus (roleDef.scan,
  //     e.g. a OneWire ROM). Gated on the SELECTED hardware being that driver's own bus type
  //     so a multi-driver role (ds18b20+ntc) doesn't show the OneWire scanner on an ADC pick.
  //  2) channel <select> — the driver exposes a fixed channel enum (roleDef.channels, e.g. a
  //     BLE sensor's temperature/humidity/battery). No scan; the device identity is chosen on
  //     the Devices page and this role just picks which value of it to read.
  // Manifest-driven — no hardcoded hw_type/driver/channel here.
  $: selectedHw = hwList.find(h => h.id === binding.hardware);
  $: scanCfg = (roleDef && roleDef.scan) || null;
  $: needsPicker = !!(scanCfg && selectedHw && selectedHw.hw_type === scanCfg.hw_type);
  $: channels = (roleDef && roleDef.channels) || null;
  $: needsChannel = !!(channels && selectedHw && !needsPicker);

  function onPick(value) {
    dispatch('changeAddr', { role: roleDef.role, addr: value });
  }
</script>

<div class="binding-row">
  <div class="role-info">
    <span class="role-label">{roleDef.label}</span>
    <button class="remove-btn" on:click={() => dispatch('remove', roleDef.role)}
            title={$t['btn.remove']}>&#x2715;</button>
  </div>
  <select class="hw-select" class:hw-empty={!binding.hardware}
          value={binding.hardware}
          on:change={e => dispatch('changeHw', { role: roleDef.role, hw: e.target.value })}>
    {#if !binding.hardware}
      <option value="" disabled>— {$t['bind.choose_hw'] || 'Оберіть обладнання'} —</option>
    {/if}
    {#each hwList as hw}
      {@const used = usedIds.has(hw.id)}
      <option value={hw.id} disabled={used}>
        {hw.label}{hw.gpio !== undefined ? ` (GPIO ${hw.gpio})` : ''}{used ? ` — ${$t['bind.used']}` : ''}
      </option>
    {/each}
  </select>
  {#if needsChannel}
    <select class="ch-select" class:ch-empty={!binding.address}
            value={binding.address || ''}
            on:change={e => onPick(e.target.value)}>
      <option value="" disabled>— {$t['bind.choose_channel'] || 'Оберіть канал'} —</option>
      {#each channels as ch}
        <option value={ch.value}>{ch.label}</option>
      {/each}
    </select>
  {:else if needsPicker}
    <DiscoveryPicker
      scan={scanCfg}
      busId={binding.hardware}
      current={binding.address || ''}
      assignedValues={assignedAddresses}
      on:pick={e => onPick(e.detail.value)}
    />
  {/if}
</div>

<style>
  .binding-row {
    padding: 10px 0;
    border-bottom: 1px solid var(--border);
  }
  .binding-row:last-child { border-bottom: none; }
  .role-info {
    display: flex;
    align-items: center;
    justify-content: space-between;
    margin-bottom: 8px;
  }
  .role-label {
    font-size: 14px;
    font-weight: 500;
    color: var(--fg);
  }
  .remove-btn {
    background: none;
    border: none;
    color: var(--fg-muted);
    cursor: pointer;
    font-size: 16px;
    padding: 2px 6px;
    border-radius: 4px;
  }
  .remove-btn:hover { color: var(--error); background: rgba(239, 68, 68, 0.1); }
  .hw-select, .ch-select {
    width: 100%;
    padding: 8px 12px;
    font-size: 14px;
    background: var(--bg);
    border: 1px solid var(--border);
    border-radius: 6px;
    color: var(--fg);
    cursor: pointer;
    appearance: auto;
  }
  .ch-select { margin-top: 8px; }
  .hw-select:focus, .ch-select:focus { outline: none; border-color: var(--accent); }
  .hw-empty, .ch-empty { border-color: var(--warning); color: var(--warning); }
</style>
