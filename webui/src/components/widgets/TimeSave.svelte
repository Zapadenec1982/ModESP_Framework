<script>
  import { apiGet, apiPost } from '../../lib/api.js';
  import { setStateKey } from '../../stores/state.js';
  import { t } from '../../stores/i18n.js';
  import { toastSuccess, toastError } from '../../stores/toast.js';
  import { onMount } from 'svelte';

  export let config;

  let loading = false;

  onMount(async () => {
    try {
      const d = await apiGet('/api/time');
      setStateKey('system.time', d.time || '--:--:--');
      setStateKey('system.date', d.date || '--.--.----');
      setStateKey('time.ntp_enabled', !!d.ntp_enabled);
      setTimeout(() => {
        setInput('time.timezone', d.timezone || '');
        // Set datetime-local value
        const dtEl = document.querySelector(`[data-widget-key="time.manual_datetime"] input`);
        if (dtEl && d.synced && d.unix > 1700000000) {
          const dt = new Date(d.unix * 1000);
          const pad = n => String(n).padStart(2, '0');
          dtEl.value = `${dt.getFullYear()}-${pad(dt.getMonth()+1)}-${pad(dt.getDate())}T${pad(dt.getHours())}:${pad(dt.getMinutes())}`;
        }
      }, 50);
    } catch (e) {}
  });

  function setInput(key, val) {
    const el = document.querySelector(`[data-widget-key="${key}"] input`)
            || document.querySelector(`[data-widget-key="${key}"] select`);
    if (el) el.value = val;
  }

  function getInput(key) {
    const el = document.querySelector(`[data-widget-key="${key}"] input`)
            || document.querySelector(`[data-widget-key="${key}"] select`);
    return el?.value ?? '';
  }

  async function save() {
    loading = true;
    // Check if NTP is toggled on
    const ntpToggle = document.querySelector(`[data-widget-key="time.ntp_enabled"] .toggle`);
    const ntpOn = ntpToggle?.classList?.contains('on') || false;

    const data = { mode: ntpOn ? 'ntp' : 'manual' };
    const tz = getInput('time.timezone').trim();
    if (tz) data.timezone = tz;

    if (!ntpOn) {
      const dtVal = getInput('time.manual_datetime');
      if (dtVal) data.time = Math.floor(new Date(dtVal).getTime() / 1000);
    }

    try {
      const r = await apiPost('/api/time', data);
      if (r.ok) toastSuccess($t['alert.saved']);
      else toastError($t['alert.error'] + ': ' + (r.error || 'unknown'));
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
