<script>
  import { state } from '../../stores/state.js';
  import { t } from '../../stores/i18n.js';
  import Card from '../../components/Card.svelte';

  export let sensors = [];
  export let actuators = [];

  // Generic: state key = "equipment.{role}" for all roles
  function stateKey(role) { return `equipment.${role}`; }
  function okKey(role) { return `equipment.${role}_ok`; }

  function formatValue(role, val) {
    if (val === undefined || val === null) return '--';
    if (typeof val === 'number') return val.toFixed(1) + ' °C';
    return val ? 'ON' : 'OFF';
  }
</script>

<Card title={$t['bind.status']}>
  <div class="status-grid">
    {#each [...sensors, ...actuators] as roleDef}
      {@const stKey = stateKey(roleDef.role)}
      {@const val = $state[stKey]}
      {@const ok = $state[okKey(roleDef.role)]}
      <div class="status-item">
        <span class="status-label">{roleDef.label}</span>
        <span class="status-value" class:on={val === true} class:off={val === false}
              class:err={ok === false}>
          {formatValue(roleDef.role, val)}
        </span>
      </div>
    {/each}
  </div>
</Card>

<style>
  .status-grid {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(140px, 1fr));
    gap: 8px;
  }
  .status-item {
    display: flex;
    flex-direction: column;
    gap: 2px;
    padding: 8px 10px;
    background: var(--bg);
    border-radius: 6px;
    border: 1px solid var(--border);
  }
  .status-label {
    font-size: 11px;
    color: var(--fg-muted);
    text-transform: uppercase;
    letter-spacing: 0.5px;
  }
  .status-value {
    font-size: 16px;
    font-weight: 600;
    color: var(--fg);
    font-variant-numeric: tabular-nums;
  }
  .status-value.on { color: var(--success); }
  .status-value.off { color: var(--fg-muted); }
  .status-value.err { color: var(--error); }
</style>
