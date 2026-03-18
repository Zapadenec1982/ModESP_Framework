<script>
  export let config;
  export let value;

  let flash = false;
  let prevDisplay = '';

  function fmtDuration(sec) {
    if (typeof sec !== 'number' || sec < 0) return '—';
    const s = Math.floor(sec);
    const h = String(Math.floor(s / 3600)).padStart(2, '0');
    const m = String(Math.floor((s % 3600) / 60)).padStart(2, '0');
    const ss = String(s % 60).padStart(2, '0');
    return `${h}:${m}:${ss}`;
  }

  $: display = value === undefined || value === null
    ? '—'
    : config.format === 'duration'
      ? fmtDuration(value)
      : typeof value === 'number'
        ? (Number.isInteger(value) ? value : value.toFixed(1))
        : String(value);

  $: if (display !== prevDisplay && prevDisplay !== '') {
    flash = true;
    setTimeout(() => flash = false, 400);
  }
  $: prevDisplay = display;
</script>

<div class="widget-row">
  <span class="label">{config.description || config.key}</span>
  <span class="value-group">
    <span class="value" class:val-flash={flash}>{display}</span>
    {#if config.unit}
      <span class="unit">{config.unit}</span>
    {/if}
  </span>
</div>

<style>
  .widget-row {
    display: flex;
    align-items: center;
    justify-content: space-between;
    min-height: 40px;
    padding: 4px 0;
  }
  .label { font-size: 14px; color: var(--fg-muted); }
  .value { font-size: 16px; font-weight: 600; font-variant-numeric: tabular-nums; transition: background-color 0.4s; border-radius: 4px; padding: 0 4px; }
  .val-flash { background-color: var(--accent-bg); }
  .unit { font-size: 12px; color: var(--fg-muted); margin-left: 4px; }
  .value-group { display: flex; align-items: baseline; }
</style>
