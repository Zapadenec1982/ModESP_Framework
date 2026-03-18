<script>
  import { apiGet, apiPost } from '../../lib/api.js';
  import { setStateKey, state } from '../../stores/state.js';
  import { t } from '../../stores/i18n.js';
  import { toastSuccess, toastError } from '../../stores/toast.js';
  import { onMount } from 'svelte';

  export let config;

  let loading = false;

  onMount(async () => {
    try {
      const d = await apiGet('/api/cloud');
      setStateKey('cloud.connected', d.connected || false);
      setStateKey('cloud.endpoint', d.endpoint || '');
      setStateKey('cloud.thing_name', d.thing_name || '');
      setStateKey('cloud.cert_loaded', d.cert_loaded || false);
      setStateKey('cloud.enabled', d.enabled || false);
    } catch (e) {}
  });

  async function save() {
    loading = true;
    const data = {
      endpoint: ($state['cloud.endpoint'] || '').trim(),
      thing_name: ($state['cloud.thing_name'] || '').trim(),
      enabled: !!$state['cloud.enabled'],
    };

    try {
      const r = await apiPost('/api/cloud', data);
      if (r.ok) toastSuccess($t['alert.saved'] || 'Saved!');
      else toastError($t['alert.error']);
    } catch (e) {
      toastError(($t['alert.error'] || 'Error') + ': ' + e.message);
    } finally {
      loading = false;
    }
  }
</script>

<div class="save-widget">
  <button class="action-btn" disabled={loading} on:click={save}>
    {loading ? '...' : (config.label || $t['btn.save'] || 'Save')}
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
