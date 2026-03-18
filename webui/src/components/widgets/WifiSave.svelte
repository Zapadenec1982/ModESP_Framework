<script>
  import { apiPost } from '../../lib/api.js';
  import { t } from '../../stores/i18n.js';
  import { toastSuccess, toastError, toastWarn } from '../../stores/toast.js';
  import { wifiSsid, wifiPassword } from '../../stores/wifiForm.js';

  export let config;

  let loading = false;

  async function save() {
    const ssid = ($wifiSsid || '').trim();
    const password = $wifiPassword || '';

    if (!ssid) { toastWarn($t['alert.ssid_empty']); return; }

    loading = true;
    try {
      const r = await apiPost('/api/wifi', { ssid, password });
      if (r.ok) toastSuccess($t['alert.saved_restart']);
      else toastError($t['alert.error']);
    } catch (e) {
      toastError($t['alert.error'] + ': ' + e.message);
    } finally {
      loading = false;
    }
  }
</script>

<div class="save-widget">
  <button class="action-btn" disabled={loading} on:click={save}>
    {loading ? '...' : (config.label || $t['btn.save'])}
  </button>
</div>

<style>
  .save-widget { padding: 4px 0; margin-top: 6px; }
  .action-btn {
    background: linear-gradient(135deg, var(--accent), #0369a1);
    color: #fff; border: none;
    padding: 12px 24px; border-radius: 10px;
    cursor: pointer; font-size: 14px; font-weight: 600;
    width: 100%; transition: all 0.2s;
    box-shadow: 0 2px 8px rgba(59,130,246,0.3);
  }
  .action-btn:hover:not(:disabled) { transform: translateY(-1px); }
  .action-btn:disabled { opacity: 0.6; cursor: not-allowed; }
</style>
