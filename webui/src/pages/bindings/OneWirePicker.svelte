<script>
  import { createEventDispatcher } from 'svelte';
  import { apiGet } from '../../lib/api.js';
  import { t } from '../../stores/i18n.js';

  export let busId = '';
  export let address = '';
  export let assignedAddresses = new Set();

  const dispatch = createEventDispatcher();

  let scanning = false;
  let devices = [];
  let error = null;

  // Скидання при зміні шини
  $: if (busId) { devices = []; error = null; }

  async function scan() {
    if (!busId) return;
    scanning = true;
    error = null;
    try {
      const data = await apiGet(`/api/onewire/scan?bus=${busId}`);
      devices = data.devices || [];
    } catch (e) {
      error = e.message;
    } finally {
      scanning = false;
    }
  }

  function pick(addr) {
    dispatch('pick', { address: addr });
  }
</script>

<div class="ow-picker">
  {#if address && devices.length === 0}
    <div class="current-row">
      <span class="addr-mono">{address}</span>
      <button class="scan-btn" on:click={scan} disabled={scanning}>
        {scanning ? $t['bind.scanning'] : $t['bind.scan']}
      </button>
    </div>
  {:else}
    <button class="scan-btn full" on:click={scan} disabled={scanning}>
      {scanning ? $t['bind.scanning'] : $t['bind.scan']}
    </button>
  {/if}

  {#if error}
    <div class="ow-error">{error}</div>
  {/if}

  {#if devices.length > 0}
    <div class="device-list">
      {#each devices as dev}
        {@const inUse = assignedAddresses.has(dev.address) && dev.address !== address}
        {@const isCurrent = dev.address === address}
        <div class="device" class:current={isCurrent} class:in-use={inUse}>
          <div class="dev-info">
            <span class="addr-mono">{dev.address}</span>
            {#if dev.temperature !== undefined}
              <span class="dev-temp">{dev.temperature.toFixed(1)} °C</span>
            {/if}
          </div>
          {#if isCurrent}
            <span class="badge current">{$t['bind.selected'] || 'обрано'}</span>
          {:else if inUse}
            <span class="badge muted">{$t['bind.in_use'] || 'зайнято'}</span>
          {:else}
            <button class="pick-btn" on:click={() => pick(dev.address)}>
              {$t['bind.pick'] || 'Обрати'}
            </button>
          {/if}
        </div>
      {/each}
    </div>
  {:else if !scanning && !address}
    <div class="ow-hint">{$t['bind.scan_hint']}</div>
  {/if}
</div>

<style>
  .ow-picker { margin-top: 8px; }
  .current-row {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 8px;
    margin-bottom: 8px;
  }
  .addr-mono {
    font-family: monospace;
    font-size: 12px;
    color: var(--fg);
  }
  .scan-btn {
    padding: 6px 14px;
    border-radius: 6px;
    border: 1px solid var(--accent);
    background: transparent;
    color: var(--accent);
    cursor: pointer;
    font-size: 13px;
    white-space: nowrap;
  }
  .scan-btn.full { width: 100%; }
  .scan-btn:hover { background: var(--accent-bg); }
  .scan-btn:disabled { opacity: 0.4; cursor: not-allowed; }
  .ow-error {
    font-size: 13px;
    color: var(--error);
    margin: 6px 0;
  }
  .device-list {
    display: flex;
    flex-direction: column;
    gap: 6px;
    margin-top: 8px;
  }
  .device {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 6px 10px;
    background: var(--bg);
    border: 1px solid var(--border);
    border-radius: 6px;
  }
  .device.current {
    border-color: var(--accent);
    background: var(--accent-bg);
  }
  .device.in-use { opacity: 0.5; }
  .dev-info {
    display: flex;
    flex-direction: column;
    gap: 2px;
  }
  .dev-temp {
    font-size: 14px;
    font-weight: 600;
    color: var(--accent);
    font-variant-numeric: tabular-nums;
  }
  .badge {
    font-size: 12px;
    font-weight: 500;
    color: var(--accent);
  }
  .badge.muted { color: var(--fg-muted); }
  .pick-btn {
    padding: 4px 12px;
    border-radius: 4px;
    border: 1px solid var(--accent);
    background: var(--accent);
    color: white;
    cursor: pointer;
    font-size: 12px;
  }
  .pick-btn:hover { opacity: 0.9; }
  .ow-hint {
    font-size: 13px;
    color: var(--fg-muted);
    text-align: center;
    padding: 8px 0;
  }
</style>
