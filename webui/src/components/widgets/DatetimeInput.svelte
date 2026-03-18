<script>
  export let config;
  export let value;
</script>

<div class="input-widget" data-widget-key={config.key}>
  <div class="input-label">{config.description || config.key}</div>
  <input type="datetime-local" class="text-input" />
</div>

<style>
  .input-widget { padding: 4px 0; }
  .input-label { font-size: 14px; color: var(--fg-muted); margin-bottom: 6px; }
  .text-input {
    background: var(--bg);
    border: 1px solid var(--border);
    color: var(--fg);
    border-radius: 8px;
    padding: 10px 12px;
    width: 100%;
    font-size: 14px;
    transition: border-color 0.2s;
    color-scheme: dark;
  }
  .text-input:focus { border-color: var(--accent); outline: none; }
</style>
