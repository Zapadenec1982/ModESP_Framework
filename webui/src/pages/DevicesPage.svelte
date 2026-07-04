<script>
  // Devices page — subscribe remote BLE devices into the runtime registry
  // (/data/devices.json). ONE manual scan (GET /api/ble/scan) lists every nearby device our
  // drivers understand (the transport identifies each by which decoder claimed its packet),
  // with its type + live readings so the operator can pick THEIR specific sensor among
  // same-type ones. Subscribing records the device (id + mac + driver type); roles then bind
  // to that id on the Bindings page transport-agnostically (no MAC ever on a role binding).
  import { onMount } from 'svelte';
  import { apiGet, apiPost } from '../lib/api.js';
  import { pages } from '../stores/ui.js';
  import { t } from '../stores/i18n.js';
  import Card from '../components/Card.svelte';

  $: devicesPage = $pages.find(p => p.id === 'devices') || {};
  $: scanEndpoint = devicesPage.scan_endpoint || '/api/ble/scan';
  $: bleTypes = devicesPage.ble_types || [];    // [{driver, label}] — type→friendly-label map
  $: typeLabel = Object.fromEntries(bleTypes.map(x => [x.driver, x.label]));

  let devices = [];          // current /data/devices.json rows
  let loading = true;
  let loaded = false;        // initial GET succeeded — save is unsafe until then
  let error = null;
  let saving = false;
  let dirty = false;
  let restartPending = false;   // a change was saved; a restart applies it

  // Single unified scan
  let scanning = false;
  let scanned = false;
  let scanResults = [];      // [{address, type, summary, rssi}]
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

  async function scan() {
    scanning = true;
    scanError = null;
    try {
      const data = await apiGet(scanEndpoint);
      scanResults = data.devices || [];
      scanned = true;
    } catch (e) {
      scanError = e.message;
    } finally {
      scanning = false;
    }
  }

  // Derive a stable, charset-safe device id from a MAC: ble_<last 3 octets>. The id is what
  // bindings reference, so it must not drift — auto-generated and fixed (the human name is
  // the editable label). Collisions get a numeric suffix.
  function deriveId(mac) {
    const hex = (mac || '').replace(/[^0-9a-fA-F]/g, '').toLowerCase();
    const suffix = hex.slice(-6) || 'dev';
    const base = 'ble_' + suffix;
    const used = new Set(devices.map(d => d.id));
    let id = base, n = 1;
    while (used.has(id)) { id = base + '_' + (n++); }
    return id;
  }

  // Subscribe a scanned device: record its mac AND its identified driver type (hw_type "ble"
  // is shared by several drivers, so the device's own driver disambiguates role binding).
  function subscribe(res) {
    const mac = res.address;
    if (!mac || subscribedMacs.has(mac.toLowerCase())) return;   // already subscribed
    devices = [...devices, {
      id: deriveId(mac),
      hw_type: 'ble',
      driver: res.type || '',
      mac,
      label: '',
    }];
    dirty = true;
    save();   // 1-click subscribe → persist immediately (no separate Save step)
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
    save();   // structural change — persist immediately
  }

  let savePending = false;
  async function save() {
    if (saving) { savePending = true; return; }   // coalesce concurrent saves (rapid subscribe)
    saving = true;
    error = null;
    try {
      do {
        savePending = false;
        const res = await apiPost('/api/devices', { manifest_version: 1, devices });
        if (res.needs_restart) restartPending = true;   // banner, not a modal per action
        dirty = false;
      } while (savePending);   // a mutation landed mid-save → re-POST the latest array
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
  {#if restartPending}
    <div class="restart-banner">
      <span>{$t['dev.saved_msg'] || $t['bind.saved_msg']}</span>
      <button class="restart-btn" on:click={restart}>{$t['bind.restart']}</button>
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
              {#if d.driver}<span class="dev-type">{typeLabel[d.driver] || d.driver}</span>{/if}
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

  <!-- ONE unified scan: all nearby supported BLE devices with live readings -->
  <Card title={$t['dev.scan_title'] || 'Пошук BLE-пристроїв'}>
    <p class="scan-desc">{$t['dev.scan_desc'] || 'Показано лише пристрої, які розуміють наші драйвери. Розрізняйте однотипні за показами (нахиліть/наблизьте потрібний) чи за RSSI.'}</p>
    <button class="scan-btn" on:click={scan} disabled={scanning}>
      {scanning ? ($t['dev.scanning'] || 'Сканування…') : ($t['dev.scan'] || 'Сканувати')}
    </button>

    {#if scanError}<div class="scan-error">{scanError}</div>{/if}

    {#if scanResults.length > 0}
      <div class="scan-list">
        {#each scanResults as res}
          {@const have = subscribedMacs.has((res.address || '').toLowerCase())}
          <div class="scan-item" class:have>
            <div class="scan-info">
              <div class="scan-top">
                <span class="scan-type">{typeLabel[res.type] || res.type}</span>
                {#if res.summary}<span class="scan-reading">{res.summary}</span>{/if}
              </div>
              <div class="scan-meta">
                <span class="scan-mac">{res.address}</span>
                {#if res.rssi !== undefined}<span class="scan-rssi">{res.rssi} dBm</span>{/if}
              </div>
            </div>
            {#if have}
              <span class="badge">{$t['dev.have'] || 'підписано'}</span>
            {:else}
              <button class="sub-btn" on:click={() => subscribe(res)}>
                {$t['dev.subscribe'] || 'Підписати'}
              </button>
            {/if}
          </div>
        {/each}
      </div>
    {:else if scanned && !scanning}
      <div class="scan-hint">{$t['dev.none_found'] || 'Підтримуваних пристроїв поблизу не знайдено'}</div>
    {:else if !scanning}
      <div class="scan-hint">{$t['dev.scan_hint'] || 'Натисніть «Сканувати» для пошуку пристроїв поблизу'}</div>
    {/if}
  </Card>

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
  .restart-banner {
    display: flex; align-items: center; justify-content: space-between; gap: 12px;
    background: var(--accent-bg); border: 1px solid var(--accent);
    color: var(--fg); padding: 10px 14px; border-radius: 8px; margin-bottom: 12px; font-size: 14px;
  }
  .restart-btn {
    padding: 6px 16px; border-radius: 6px; border: none;
    background: var(--accent); color: white; cursor: pointer; font-size: 13px; white-space: nowrap;
  }
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
  .dev-sub { display: flex; gap: 10px; align-items: baseline; flex-wrap: wrap; }
  .dev-type { font-size: 12px; font-weight: 600; color: var(--accent); }
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
  .scan-info { display: flex; flex-direction: column; gap: 3px; min-width: 0; }
  .scan-top { display: flex; gap: 10px; align-items: baseline; flex-wrap: wrap; }
  .scan-type { font-size: 13px; font-weight: 600; color: var(--fg); }
  .scan-reading {
    font-size: 14px; font-weight: 600; color: var(--accent);
    font-variant-numeric: tabular-nums;
  }
  .scan-mac { font-family: monospace; font-size: 12px; color: var(--fg-muted); }
  .scan-meta { display: flex; gap: 10px; align-items: baseline; }
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
</style>
