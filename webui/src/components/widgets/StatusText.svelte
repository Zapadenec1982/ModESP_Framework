<script>
  export let config;
  export let value;

  let flash = false;
  let prevDisplay = '';

  // Generic semantic status → colour. Domain-agnostic: only universal states
  // (ok/on/active, warn, error/off, connectivity). Product-specific statuses
  // fall back to the muted default — no hardcoded product vocabulary here.
  const statusColors = {
    ok: 'var(--success)', on: 'var(--success)', active: 'var(--success)',
    running: 'var(--success)', connected: 'var(--success)',
    warn: 'var(--warning)', warning: 'var(--warning)', safe_mode: 'var(--warning)',
    error: 'var(--error)', fault: 'var(--error)', disconnected: 'var(--error)',
    off: 'var(--fg-muted)', idle: 'var(--fg-muted)',
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
