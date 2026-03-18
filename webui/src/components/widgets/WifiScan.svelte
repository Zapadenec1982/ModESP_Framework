<script>
  import { apiGet } from '../../lib/api.js';
  import { t } from '../../stores/i18n.js';
  import { wifiSsid, wifiPassword } from '../../stores/wifiForm.js';

  export let config;

  let loading = false;
  let networks = [];
  let error = '';

  async function scan() {
    loading = true;
    error = '';
    networks = [];
    try {
      const r = await apiGet('/api/wifi/scan');
      networks = (r.networks || []).sort((a, b) => b.rssi - a.rssi);
    } catch (e) {
      error = e.message;
    } finally {
      loading = false;
    }
  }

  function selectNetwork(net) {
    wifiSsid.set(net.ssid);
    if (net.auth === 'open') wifiPassword.set('');
    networks = [];
  }

  function signalBars(rssi) {
    if (rssi >= -50) return 4;
    if (rssi >= -60) return 3;
    if (rssi >= -70) return 2;
    return 1;
  }
</script>

<div class="scan-widget">
  <button class="scan-btn" disabled={loading} on:click={scan}>
    {loading ? $t['wifi.scanning'] : (config.label || $t['wifi.scan'])}
  </button>

  {#if error}
    <div class="scan-error">{error}</div>
  {/if}

  {#if networks.length > 0}
    <ul class="network-list">
      {#each networks as net}
        <li class="network-item" on:click={() => selectNetwork(net)}>
          <div class="net-info">
            {#if net.auth !== 'open'}<span class="lock">&#128274;</span>{/if}
            <span class="net-ssid">{net.ssid}</span>
          </div>
          <div class="net-signal">
            <span class="signal-bars" data-bars={signalBars(net.rssi)}>
              <span class="bar"></span><span class="bar"></span>
              <span class="bar"></span><span class="bar"></span>
            </span>
            <span class="rssi">{net.rssi}</span>
          </div>
        </li>
      {/each}
    </ul>
  {/if}
</div>

<style>
  .scan-widget { padding: 4px 0; }
  .scan-btn {
    background: transparent;
    color: var(--accent, #3b82f6);
    border: 1px solid var(--accent, #3b82f6);
    padding: 12px 24px; border-radius: 10px;
    cursor: pointer; font-size: 14px; font-weight: 600;
    width: 100%; transition: all 0.2s;
  }
  .scan-btn:hover:not(:disabled) { background: var(--accent); color: #fff; }
  .scan-btn:disabled { opacity: 0.6; cursor: not-allowed; }
  .scan-error { color: var(--danger, #e74c3c); font-size: 12px; margin-top: 6px; }
  .network-list {
    list-style: none; margin: 8px 0 0; padding: 0;
    border: 1px solid var(--border); border-radius: 8px;
    max-height: 220px; overflow-y: auto;
  }
  .network-item {
    display: flex; justify-content: space-between; align-items: center;
    padding: 8px 12px; cursor: pointer;
    border-bottom: 1px solid var(--border);
    transition: background 0.15s;
  }
  .network-item:last-child { border-bottom: none; }
  .network-item:hover { background: var(--bg-hover, rgba(59,130,246,0.1)); }
  .net-info { display: flex; align-items: center; gap: 6px; min-width: 0; }
  .lock { font-size: 12px; }
  .net-ssid { font-size: 13px; color: var(--fg); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
  .net-signal { display: flex; align-items: center; gap: 6px; flex-shrink: 0; }
  .rssi { font-size: 11px; color: var(--fg-muted); min-width: 32px; text-align: right; }
  .signal-bars { display: flex; align-items: flex-end; gap: 2px; height: 14px; }
  .bar { width: 3px; background: var(--border); border-radius: 1px; }
  .bar:nth-child(1) { height: 4px; }
  .bar:nth-child(2) { height: 7px; }
  .bar:nth-child(3) { height: 10px; }
  .bar:nth-child(4) { height: 14px; }
  .signal-bars[data-bars="4"] .bar { background: var(--success, #22c55e); }
  .signal-bars[data-bars="3"] .bar:nth-child(1),
  .signal-bars[data-bars="3"] .bar:nth-child(2),
  .signal-bars[data-bars="3"] .bar:nth-child(3) { background: var(--success, #22c55e); }
  .signal-bars[data-bars="2"] .bar:nth-child(1),
  .signal-bars[data-bars="2"] .bar:nth-child(2) { background: var(--warning, #f59e0b); }
  .signal-bars[data-bars="1"] .bar:nth-child(1) { background: var(--danger, #e74c3c); }
</style>
