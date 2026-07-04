<script>
  // Generic device picker for any discovery-capable driver (OneWire ROM, BLE MAC, …).
  // It knows nothing product-specific: the scan endpoint + which binding field the pick
  // fills come from ui.json (hw.scan = { endpoint, assigns }). Dispatches pick({value}).
  import { createEventDispatcher } from 'svelte';
  import { apiGet } from '../../lib/api.js';
  import { t } from '../../stores/i18n.js';

  export let scan = null;                 // { endpoint, assigns } from the hardware entry
  export let busId = '';                  // hardware id (bus for OneWire; radio-wide/ignored for BLE)
  export let current = '';                // currently assigned value (address or mac)
  export let assignedValues = new Set();  // values already used by other bindings

  const dispatch = createEventDispatcher();

  let scanning = false;
  let devices = [];
  let error = null;

  // Reset the list when the selected hardware changes
  $: if (busId) { devices = []; error = null; }

  async function doScan() {
    if (!scan || !scan.endpoint) return;
    scanning = true;
    error = null;
    try {
      const sep = scan.endpoint.includes('?') ? '&' : '?';
      const data = await apiGet(`${scan.endpoint}${sep}bus=${encodeURIComponent(busId)}`);
      devices = data.devices || [];
    } catch (e) {
      error = e.message;
    } finally {
      scanning = false;
    }
  }

  function pick(addr) {
    dispatch('pick', { value: addr });
  }
</script>

<div class="disc-picker">
  {#if current && devices.length === 0}
    <div class="current-row">
      <span class="addr-mono">{current}</span>
      <button class="scan-btn" on:click={doScan} disabled={scanning}>
        {scanning ? $t['bind.scanning'] : $t['bind.scan']}
      </button>
    </div>
  {:else}
    <button class="scan-btn full" on:click={doScan} disabled={scanning}>
      {scanning ? $t['bind.scanning'] : $t['bind.scan']}
    </button>
  {/if}

  {#if error}
    <div class="disc-error">{error}</div>
  {/if}

  {#if devices.length > 0}
    <div class="device-list">
      {#each devices as dev}
        {@const inUse = assignedValues.has(dev.address) && dev.address !== current}
        {@const isCurrent = dev.address === current}
        <div class="device" class:current={isCurrent} class:in-use={inUse}>
          <div class="dev-info">
            <span class="addr-mono">{dev.address}</span>
            <div class="dev-meta">
              {#if dev.temperature !== undefined}
                <span class="dev-temp">{dev.temperature.toFixed(1)} °C</span>
              {/if}
              {#if dev.rssi !== undefined}
                <span class="dev-rssi">{dev.rssi} dBm</span>
              {/if}
            </div>
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
  {:else if !scanning && !current}
    <div class="disc-hint">{$t['bind.scan_hint']}</div>
  {/if}
</div>

<style>
  .disc-picker { margin-top: 8px; }
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
  .disc-error {
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
  .dev-meta {
    display: flex;
    gap: 10px;
    align-items: baseline;
  }
  .dev-temp {
    font-size: 14px;
    font-weight: 600;
    color: var(--accent);
    font-variant-numeric: tabular-nums;
  }
  .dev-rssi {
    font-size: 12px;
    color: var(--fg-muted);
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
  .disc-hint {
    font-size: 13px;
    color: var(--fg-muted);
    text-align: center;
    padding: 8px 0;
  }
</style>
