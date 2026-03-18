<script>
  import { apiGet, apiPost } from '../../lib/api.js';
  import { setStateKey, state } from '../../stores/state.js';
  import { mqttBroker, mqttUser, mqttPass, mqttPrefix } from '../../stores/mqttForm.js';
  import { t } from '../../stores/i18n.js';
  import { toastSuccess, toastError } from '../../stores/toast.js';
  import { onMount } from 'svelte';

  export let config;

  let loading = false;
  let mounted = false;
  let prevEnabled = null;

  onMount(async () => {
    try {
      const d = await apiGet('/api/mqtt');
      // Status keys для відображення
      setStateKey('mqtt.connected', d.connected || false);
      setStateKey('mqtt.status', d.status || 'unknown');
      setStateKey('mqtt.enabled', d.enabled || false);
      // Form fields через stores (TextInput/PasswordInput синхронізуються)
      mqttBroker.set(d.broker || '');
      mqttUser.set(d.user || '');
      mqttPrefix.set(d.prefix || '');
      // Port через $state (NumberInput читає звідти)
      setStateKey('mqtt.port', d.port || 1883);
      // Password не повертається з API — store лишається порожнім
      prevEnabled = !!d.enabled;
    } catch (e) {}
    mounted = true;
  });

  // Instant toggle: при зміні mqtt.enabled одразу відправляємо на сервер
  $: if (mounted && prevEnabled !== null && !!$state['mqtt.enabled'] !== prevEnabled) {
    prevEnabled = !!$state['mqtt.enabled'];
    apiPost('/api/mqtt', { enabled: prevEnabled })
      .then(r => { if (r.ok) toastSuccess($t['alert.saved_mqtt']); })
      .catch(() => {});
  }

  async function save() {
    loading = true;
    const data = {
      broker: ($mqttBroker || '').trim(),
      port: parseInt($state['mqtt.port']) || 1883,
      user: ($mqttUser || '').trim(),
      password: $mqttPass || '',
      prefix: ($mqttPrefix || '').trim(),
      enabled: !!$state['mqtt.enabled'],
    };

    try {
      const r = await apiPost('/api/mqtt', data);
      if (r.ok) toastSuccess($t['alert.saved_mqtt']);
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
