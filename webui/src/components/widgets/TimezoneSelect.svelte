<script>
  import { t } from '../../stores/i18n.js';

  export let config;
  export let value;

  const timezones = [
    { label: 'UTC-12', tz: 'UTC12' },
    { label: 'UTC-11', tz: 'UTC11' },
    { label: 'UTC-10 (Hawaii)', tz: 'HST10' },
    { label: 'UTC-9 (Alaska)', tz: 'AKST9AKDT,M3.2.0,M11.1.0' },
    { label: 'UTC-8 (Los Angeles)', tz: 'PST8PDT,M3.2.0,M11.1.0' },
    { label: 'UTC-7 (Denver)', tz: 'MST7MDT,M3.2.0,M11.1.0' },
    { label: 'UTC-6 (Chicago)', tz: 'CST6CDT,M3.2.0,M11.1.0' },
    { label: 'UTC-5 (New York)', tz: 'EST5EDT,M3.2.0,M11.1.0' },
    { label: 'UTC-4', tz: 'AST4' },
    { label: 'UTC-3 (Sao Paulo)', tz: 'BRT3' },
    { label: 'UTC-2', tz: 'BRST2' },
    { label: 'UTC-1', tz: 'CVT1' },
    { label: 'UTC+0 (London)', tz: 'GMT0BST,M3.5.0/1,M10.5.0' },
    { label: 'UTC+1 (Berlin)', tz: 'CET-1CEST,M3.5.0,M10.5.0/3' },
    { label: 'UTC+2 (Kyiv)', tz: 'EET-2EEST,M3.5.0/3,M10.5.0/4' },
    { label: 'UTC+3 (Moscow)', tz: 'MSK-3' },
    { label: 'UTC+4 (Dubai)', tz: 'GST-4' },
    { label: 'UTC+5 (Tashkent)', tz: 'PKT-5' },
    { label: 'UTC+5:30 (India)', tz: 'IST-5:30' },
    { label: 'UTC+6 (Almaty)', tz: 'ALMT-6' },
    { label: 'UTC+7 (Bangkok)', tz: 'ICT-7' },
    { label: 'UTC+8 (Singapore)', tz: 'SGT-8' },
    { label: 'UTC+9 (Tokyo)', tz: 'JST-9' },
    { label: 'UTC+10 (Sydney)', tz: 'AEST-10AEDT,M10.1.0,M4.1.0/3' },
    { label: 'UTC+12 (Auckland)', tz: 'NZST-12NZDT,M9.5.0,M4.1.0/3' },
  ];
</script>

<div class="tz-widget">
  <div class="tz-label">{config.description || $t['tz.label']}</div>
  <select class="tz-select">
    {#each timezones as z}
      <option value={z.tz}>{z.label}</option>
    {/each}
  </select>
</div>

<style>
  .tz-widget { padding: 4px 0; }
  .tz-label { font-size: 14px; color: var(--fg-muted); margin-bottom: 8px; }
  .tz-select {
    width: 100%;
    padding: 10px 12px;
    font-size: 14px;
    min-height: 44px;
    background: var(--bg);
    border: 1px solid var(--border);
    border-radius: 6px;
    color: var(--fg);
    cursor: pointer;
    appearance: auto;
  }
  .tz-select:focus { outline: none; border-color: var(--accent); }
</style>
