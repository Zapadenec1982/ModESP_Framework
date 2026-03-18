<script>
  import { createEventDispatcher } from 'svelte';
  import { t } from '../../stores/i18n.js';
  import OneWirePicker from './OneWirePicker.svelte';

  export let roleDef;
  export let binding;
  export let hwList = [];
  export let usedIds = new Set();
  export let assignedAddresses = new Set();

  const dispatch = createEventDispatcher();

  // Визначаємо чи обране hardware потребує ROM адресу (OneWire bus)
  $: selectedHw = hwList.find(h => h.id === binding.hardware);
  $: needsAddress = selectedHw && selectedHw.hw_type === 'onewire_bus';
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
  {#if needsAddress}
    <OneWirePicker
      busId={binding.hardware}
      address={binding.address || ''}
      {assignedAddresses}
      on:pick={e => dispatch('changeAddr', { role: roleDef.role, addr: e.detail.address })}
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
  .hw-select {
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
  .hw-select:focus { outline: none; border-color: var(--accent); }
  .hw-empty { border-color: var(--warning); color: var(--warning); }
</style>
