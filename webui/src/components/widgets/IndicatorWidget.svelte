<script>
  export let config;
  export let value;

  $: isOn = !!value;
  $: color = isOn ? (config.on_color || 'var(--success)') : (config.off_color || 'var(--fg-muted)');
  $: label = isOn ? (config.on_label || 'ON') : (config.off_label || 'OFF');
</script>

<div class="widget-row">
  <span class="label">{config.description || config.key}</span>
  <div class="indicator">
    <span class="dot" class:on={isOn} style="background: {color}; {isOn ? `box-shadow: 0 0 8px ${color}` : ''}"></span>
    <span class="indicator-label">{label}</span>
  </div>
</div>

<style>
  .widget-row {
    display: flex;
    align-items: center;
    justify-content: space-between;
    min-height: 48px;
    padding: 12px 0;
  }
  .label {
    font-size: 15px;
    font-weight: 500;
    color: var(--text-2);
  }
  .indicator {
    display: flex;
    align-items: center;
    gap: 10px;
  }
  .dot {
    width: 10px;
    height: 10px;
    border-radius: 50%;
    transition: all 0.3s cubic-bezier(0.16, 1, 0.3, 1);
  }
  .indicator-label {
    font-size: 14px;
    font-weight: 600;
    color: var(--text-1);
    letter-spacing: 0.5px;
  }
</style>
