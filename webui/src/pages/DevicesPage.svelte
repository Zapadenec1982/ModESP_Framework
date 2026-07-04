<script>
  // Devices page — subscribe remote devices (BLE by MAC today) into the runtime registry
  // (/data/devices.json). Scanning + subscribing lives HERE, at device scope, not inside a
  // role card: a subscribed device becomes a named source with an id, and roles bind to
  // that id on the Bindings page transport-agnostically (no MAC ever on a role binding).
  // The subscribable driver types come from ui.json (generator: discovery.assigns == "mac").
  import { onMount } from 'svelte';
  import { apiGet, apiPost } from '../lib/api.js';
  import { pages } from '../stores/ui.js';
  import { t } from '../stores/i18n.js';
  import Card from '../components/Card.svelte';

  $: devicesPage = $pages.find(p => p.id === 'devices') || {};
  $: subscribable = devicesPage.subscribable || [];

  let devices = [];          // current /data/devices.json rows
  let loading = true;
  let loaded = false;        // initial GET succeeded — save is unsafe until then
  let error = null;
  let saving = false;
  let dirty = false;
  let needsRestart = false;

  // Scan state (single active scan at a time)
  let scanning = false;
  let scanDriver = null;     // the subscribable entry currently scanned
  let scanResults = [];
  let scanError = null;

  onMount(async () => {
    try {
      const d = await apiGet('/api/devices');
      devices = d.devices || [];
      loaded = true;
    } catch (e) {
      error = e.message;
    } finally {
      loading = false;
    }
  });

  $: subscribedMacs = new Set(devices.map(d => (d.mac || '').toLowerCase()));

  async function scan(sub) {
    scanDriver = sub;
    scanning = true;
    scanError = null;
    scanResults = [];
    try {
      // A BLE scan is radio-wide; the endpoint requires a bus param but the driver
      // ignores it for BLE — pass the hw_type as a harmless placeholder.
      const sep = sub.scan_endpoint.includes('?') ? '&' : '?';
      const url = sub.scan_endpoint + sep + 'bus=' + encodeURIComponent(sub.hw_type || 'ble');
      const data = await apiGet(url);
      scanResults = data.devices || [];
    } catch (e) {
      scanError = e.message;
    } finally {
      scanning = false;
    }
  }

  // Derive a stable, charset-safe device id from a MAC: <hw_type>_<last 3 octets>.
  // The id is what bindings reference, so it must not drift — auto-generated and fixed
  // (the human-facing name is the editable label). Collisions get a numeric suffix.
  function deriveId(sub, mac) {
    const hex = (mac || '').replace(/[^0-9a-fA-F]/g, '').toLowerCase();
    const suffix = hex.slice(-6) || 'dev';
    const prefix = (sub.hw_type || 'ble').replace(/[^a-z0-9_]/g, '') || 'dev';
    const base = prefix + '_' + suffix;
    const used = new Set(devices.map(d => d.id));
    let id = base, n = 1;
    while (used.has(id)) { id = base + '_' + (n++); }
    return id;
  }

  function subscribe(sub, res) {
    const mac = res.address;
    if (!mac || subscribedMacs.has(mac.toLowerCase())) return;   // already subscribed
    devices = [...devices, {
      id: deriveId(sub, mac),
      hw_type: sub.hw_type || 'ble',
      mac,
      label: '',
    }];
    dirty = true;
  }

  const LABEL_MAX = 64;   // keep a full 12-device POST body under the server's buffer

  function renameDevice(id, label) {
    const clipped = (label || '').slice(0, LABEL_MAX);
    devices = devices.map(d => d.id === id ? { ...d, label: clipped } : d);
    dirty = true;
  }

  function removeDevice(id) {
    devices = devices.filter(d => d.id !== id);
    dirty = true;
  }

  async function save() {
    saving = true;
    error = null;
    try {
      const res = await apiPost('/api/devices', { manifest_version: 1, devices });
      if (res.needs_restart) needsRestart = true;
      dirty = false;
    } catch (e) {
      error = e.message;
    } finally {
      saving = false;
    }
  }

  async function restart() {
    try { await apiPost('/api/restart', {}); } catch (_) {}
    setTimeout(() => location.reload(), 5000);
  }
</script>

{#if loading}
  <div class="center-msg">{$t['dev.loading'] || 'Завантаження…'}</div>
{:else if !loaded}
  <!-- Initial load failed: do NOT render the editor. Saving from an unloaded baseline
       would POST a list built without the existing devices and truncate-replace the
       registry, wiping devices the user never saw. -->
  <div class="center-msg error">{error || ($t['dev.load_failed'] || 'Не вдалося завантажити пристрої')}</div>
{:else}
  {#if needsRestart}
    <div class="modal-overlay" on:click|self={() => needsRestart = false}>
      <div class="modal-dialog">
        <div class="modal-icon">✓</div>
        <div class="modal-title">{$t['dev.saved_title'] || $t['bind.saved_title']}</div>
        <div class="modal-text">{$t['dev.saved_msg'] || $t['bind.saved_msg']}</div>
        <div class="modal-actions">
          <button class="modal-btn-restart" on:click={restart}>{$t['bind.restart']}</button>
          <button class="modal-btn-later" on:click={() => needsRestart = false}>{$t['bind.later']}</button>
        </div>
      </div>
    </div>
  {/if}

  {#if error}
    <div class="error-banner">{error}</div>
  {/if}

  <!-- Subscribed devices -->
  <Card title={$t['dev.subscribed'] || 'Підписані пристрої'}>
    {#if devices.length === 0}
      <div class="empty">{$t['dev.none'] || 'Немає підписаних пристроїв. Скануйте нижче.'}</div>
    {:else}
      {#each devices as d (d.id)}
        <div class="dev-row">
          <div class="dev-main">
            <input class="dev-label" placeholder={d.id} maxlength={LABEL_MAX}
                   value={d.label || ''}
                   on:input={e => renameDevice(d.id, e.target.value)} />
            <div class="dev-sub">
              <span class="dev-mac">{d.mac}</span>
              <span class="dev-id">{d.id}</span>
            </div>
          </div>
          <button class="dev-remove" title={$t['btn.remove'] || 'Видалити'}
                  on:click={() => removeDevice(d.id)}>&#x2715;</button>
        </div>
      {/each}
    {/if}
  </Card>

  <!-- Scan + subscribe, one card per subscribable driver type -->
  {#each subscribable as sub}
    <Card title={sub.label || sub.driver}>
      {#if sub.description}<p class="scan-desc">{sub.description}</p>{/if}
      <button class="scan-btn" on:click={() => scan(sub)}
              disabled={scanning && scanDriver === sub}>
        {scanning && scanDriver === sub
          ? ($t['dev.scanning'] || 'Сканування…')
          : (sub.scan_button || $t['dev.scan'] || 'Сканувати')}
      </button>

      {#if scanError && scanDriver === sub}
        <div class="scan-error">{scanError}</div>
      {/if}

      {#if scanDriver === sub && scanResults.length > 0}
        <div class="scan-list">
          {#each scanResults as res}
            {@const have = subscribedMacs.has((res.address || '').toLowerCase())}
            <div class="scan-item" class:have>
              <div class="scan-info">
                <span class="scan-mac">{res.address}</span>
                <div class="scan-meta">
                  {#if res.temperature !== undefined}
                    <span class="scan-temp">{res.temperature.toFixed(1)} °C</span>
                  {/if}
                  {#if res.rssi !== undefined}
                    <span class="scan-rssi">{res.rssi} dBm</span>
                  {/if}
                </div>
              </div>
              {#if have}
                <span class="badge">{$t['dev.have'] || 'підписано'}</span>
              {:else}
                <button class="sub-btn" on:click={() => subscribe(sub, res)}>
                  {$t['dev.subscribe'] || 'Підписати'}
                </button>
              {/if}
            </div>
          {/each}
        </div>
      {:else if scanDriver === sub && !scanning}
        <div class="scan-hint">{$t['dev.scan_hint'] || 'Натисніть «Сканувати» для пошуку пристроїв поблизу'}</div>
      {/if}
    </Card>
  {/each}

  <!-- Save bar -->
  <div class="save-bar">
    {#if dirty}
      <span class="dirty-hint">{$t['dev.unsaved'] || 'Є незбережені зміни'}</span>
    {/if}
    <button class="save-btn" on:click={save} disabled={!dirty || saving || !loaded}>
      {saving ? ($t['dev.saving'] || 'Збереження…') : ($t['dev.save'] || 'Зберегти')}
    </button>
  </div>
{/if}

<style>
  .center-msg { text-align: center; padding: 40px 0; color: var(--fg-muted); }
  .error-banner {
    background: rgba(239, 68, 68, 0.1);
    border: 1px solid var(--error);
    color: var(--error);
    padding: 10px 14px;
    border-radius: 8px;
    margin-bottom: 12px;
    font-size: 14px;
  }
  .empty { color: var(--fg-muted); font-size: 14px; padding: 8px 0; }

  .dev-row {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 10px;
    padding: 10px 0;
    border-bottom: 1px solid var(--border);
  }
  .dev-row:last-child { border-bottom: none; }
  .dev-main { display: flex; flex-direction: column; gap: 4px; min-width: 0; flex: 1; }
  .dev-label {
    background: var(--bg);
    border: 1px solid var(--border);
    border-radius: 6px;
    padding: 6px 10px;
    font-size: 14px;
    color: var(--fg);
    max-width: 260px;
  }
  .dev-label:focus { outline: none; border-color: var(--accent); }
  .dev-sub { display: flex; gap: 10px; align-items: baseline; }
  .dev-mac { font-family: monospace; font-size: 12px; color: var(--fg); }
  .dev-id  { font-family: monospace; font-size: 11px; color: var(--fg-muted); }
  .dev-remove {
    background: none; border: none; color: var(--fg-muted);
    cursor: pointer; font-size: 16px; padding: 2px 6px; border-radius: 4px;
  }
  .dev-remove:hover { color: var(--error); background: rgba(239, 68, 68, 0.1); }

  .scan-desc { font-size: 13px; color: var(--fg-muted); margin-bottom: 10px; }
  .scan-btn {
    padding: 8px 16px;
    border-radius: 8px;
    border: 1px solid var(--accent);
    background: transparent;
    color: var(--accent);
    cursor: pointer;
    font-size: 14px;
  }
  .scan-btn:hover { background: var(--accent-bg); }
  .scan-btn:disabled { opacity: 0.5; cursor: not-allowed; }
  .scan-error { color: var(--error); font-size: 13px; margin-top: 8px; }
  .scan-hint { color: var(--fg-muted); font-size: 13px; margin-top: 10px; }

  .scan-list { display: flex; flex-direction: column; gap: 6px; margin-top: 12px; }
  .scan-item {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 8px 12px;
    background: var(--bg);
    border: 1px solid var(--border);
    border-radius: 8px;
  }
  .scan-item.have { opacity: 0.55; }
  .scan-info { display: flex; flex-direction: column; gap: 2px; }
  .scan-mac { font-family: monospace; font-size: 13px; color: var(--fg); }
  .scan-meta { display: flex; gap: 10px; align-items: baseline; }
  .scan-temp {
    font-size: 14px; font-weight: 600; color: var(--accent);
    font-variant-numeric: tabular-nums;
  }
  .scan-rssi { font-size: 12px; color: var(--fg-muted); font-variant-numeric: tabular-nums; }
  .badge { font-size: 12px; color: var(--fg-muted); }
  .sub-btn {
    padding: 5px 14px;
    border-radius: 6px;
    border: 1px solid var(--accent);
    background: var(--accent);
    color: white;
    cursor: pointer;
    font-size: 13px;
  }
  .sub-btn:hover { opacity: 0.9; }

  .save-bar {
    display: flex;
    align-items: center;
    justify-content: flex-end;
    gap: 12px;
    margin-top: 16px;
  }
  .dirty-hint { font-size: 13px; color: var(--warning); }
  .save-btn {
    padding: 10px 24px;
    border-radius: 8px;
    border: none;
    background: var(--accent);
    color: white;
    font-size: 14px;
    cursor: pointer;
  }
  .save-btn:disabled { opacity: 0.4; cursor: not-allowed; }

  .modal-overlay {
    position: fixed; inset: 0; background: rgba(0,0,0,0.5);
    display: flex; align-items: center; justify-content: center; z-index: 1000;
  }
  .modal-dialog {
    background: var(--surface-1, var(--bg));
    border: 1px solid var(--border);
    border-radius: 16px;
    padding: 28px;
    max-width: 340px;
    text-align: center;
  }
  .modal-icon {
    width: 48px; height: 48px; border-radius: 50%;
    background: var(--accent); color: white; font-size: 24px;
    display: flex; align-items: center; justify-content: center; margin: 0 auto 12px;
  }
  .modal-title { font-size: 18px; font-weight: 600; margin-bottom: 8px; }
  .modal-text { font-size: 14px; color: var(--fg-muted); margin-bottom: 20px; }
  .modal-actions { display: flex; gap: 10px; justify-content: center; }
  .modal-btn-restart {
    padding: 10px 20px; border-radius: 8px; border: none;
    background: var(--accent); color: white; cursor: pointer; font-size: 14px;
  }
  .modal-btn-later {
    padding: 10px 20px; border-radius: 8px; border: 1px solid var(--border);
    background: transparent; color: var(--fg); cursor: pointer; font-size: 14px;
  }
</style>
