<script>
  export let config;
  export let value;

  let flash = false;
  let prevDisplay = '';

  const statusColors = {
    // Thermostat
    idle: 'var(--fg-muted)',
    cooling: 'var(--accent)',
    safe_mode: 'var(--warning)',
    startup: 'var(--fg-muted)',
    // Defrost (AUDIT-008)
    stabilize: '#f59e0b',
    valve_open: '#f59e0b',
    active: '#ef4444',
    equalize: '#f59e0b',
    drip: '#8b5cf6',
    fad: '#06b6d4',
    // Network
    connected: 'var(--success)',
    disconnected: 'var(--error)',
    error: 'var(--error)'
  };

  $: display = value !== undefined && value !== null ? String(value) : '—';
  $: badgeColor = statusColors[display] || 'var(--fg-muted)';

  $: if (display !== prevDisplay && prevDisplay !== '') {
    flash = true;
    setTimeout(() => flash = false, 400);
  }
  $: prevDisplay = display;
</script>

<div class="widget-row">
  <span class="label">{config.description || config.key}</span>
  <span class="badge" class:status-flash={flash} style="color: {badgeColor}; border-color: {badgeColor}">{display}</span>
</div>

<style>
  .widget-row {
    display: flex; align-items: center; justify-content: space-between;
    min-height: 40px; padding: 4px 0;
  }
  .label { font-size: 14px; color: var(--fg-muted); }
  .badge {
    font-size: 13px; font-weight: 600;
    padding: 3px 12px;
    border: 1px solid;
    border-radius: 20px;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    transition: background-color 0.4s;
  }
  .status-flash { background-color: var(--accent-bg); }
</style>
