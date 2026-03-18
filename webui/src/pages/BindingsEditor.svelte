<script>
  import { onMount } from 'svelte';
  import { apiGet, apiPost } from '../lib/api.js';
  import { pages } from '../stores/ui.js';
  import { state } from '../stores/state.js';
  import { t } from '../stores/i18n.js';
  import Card from '../components/Card.svelte';
  import NumberInput from '../components/widgets/NumberInput.svelte';
  import EquipmentStatus from './bindings/EquipmentStatus.svelte';
  import BindingCard from './bindings/BindingCard.svelte';
  import OneWireDiscovery from './bindings/OneWireDiscovery.svelte';

  // Roles/hardware metadata з ui.json bindings page
  $: bindingsPage = $pages.find(p => p.id === 'bindings') || {};
  $: roles = bindingsPage.roles || [];
  $: hwInventory = bindingsPage.hardware || [];

  // Поточні bindings (завантажуються з /api/bindings)
  let bindings = [];
  let loading = true;
  let error = null;
  let saving = false;
  let needsRestart = false;

  onMount(async () => {
    try {
      const data = await apiGet('/api/bindings');
      bindings = data.bindings || [];
    } catch (e) {
      error = e.message;
    } finally {
      loading = false;
    }
  });

  // ── Hardware helpers ──
  function compatibleHw(roleDef) {
    const types = roleDef.hw_types || (roleDef.hw_type ? [roleDef.hw_type] : []);
    return hwInventory.filter(h => types.some(t => t === h.hw_type));
  }

  // Визначити правильний драйвер за hw_type обраного hardware
  function driverForHw(roleDef, hwId) {
    const hw = hwInventory.find(h => h.id === hwId);
    if (!hw) return roleDef.driver || (roleDef.drivers && roleDef.drivers[0]) || '';
    const types = roleDef.hw_types || [];
    const drivers = roleDef.drivers || (roleDef.driver ? [roleDef.driver] : []);
    const idx = types.indexOf(hw.hw_type);
    return idx >= 0 && idx < drivers.length ? drivers[idx] : drivers[0] || '';
  }

  // Типи hardware що підтримують кілька ролей на одному порті
  const SHAREABLE_HW = new Set(['onewire_bus']);

  function usedHwIds(excludeRole) {
    return new Set(bindings
      .filter(b => b.role !== excludeRole)
      .map(b => b.hardware)
      .filter(hwId => {
        const hw = hwInventory.find(h => h.id === hwId);
        return hw && !SHAREABLE_HW.has(hw.hw_type);
      }));
  }

  function availableHw(roleDef) {
    const used = usedHwIds(roleDef.role);
    return compatibleHw(roleDef).filter(h => !used.has(h.id));
  }

  // ── Binding mutations ──
  function getBinding(role) {
    return bindings.find(b => b.role === role);
  }

  function setHardware(role, hwId) {
    const roleDef = roles.find(r => r.role === role);
    const oldBinding = bindings.find(b => b.role === role);
    const oldHw = oldBinding ? hwInventory.find(h => h.id === oldBinding.hardware) : null;
    const newHw = hwInventory.find(h => h.id === hwId);
    // Очищати адресу при зміні типу hardware (OW↔ADC) або зміні шини
    const clearAddr = (oldHw?.hw_type === 'onewire_bus' && newHw?.hw_type !== 'onewire_bus')
                   || (oldHw?.id !== hwId && newHw?.hw_type === 'onewire_bus');
    bindings = bindings.map(b =>
      b.role === role ? {
        ...b,
        hardware: hwId,
        driver: roleDef ? driverForHw(roleDef, hwId) : b.driver,
        ...(clearAddr ? { address: '' } : {})
      } : b
    );
  }

  function setAddress(role, addr) {
    bindings = bindings.map(b =>
      b.role === role ? { ...b, address: addr } : b
    );
  }

  function removeRole(role) {
    bindings = bindings.filter(b => b.role !== role);
  }

  function addRole(roleDef) {
    const hw = availableHw(roleDef);
    if (hw.length === 0) return;
    const autoAssign = hw.length === 1;
    bindings = [...bindings, {
      hardware: autoAssign ? hw[0].id : '',
      driver: autoAssign ? driverForHw(roleDef, hw[0].id) : '',
      role: roleDef.role,
      module: 'equipment',
    }];
  }

  // ── Derived state ──
  $: assignedRoles = new Set(bindings.map(b => b.role));
  $: assignedAddresses = new Set(bindings.filter(b => b.address).map(b => b.address));
  $: requiredRoles = roles.filter(r => !r.optional);
  $: missingRequired = requiredRoles.filter(r => !assignedRoles.has(r.role));
  $: hasEmptyHw = bindings.some(b => !b.hardware);
  $: hasEmptyAddr = bindings.some(b => {
    const hw = hwInventory.find(h => h.id === b.hardware);
    return hw && hw.hw_type === 'onewire_bus' && !b.address;
  });
  $: canSave = !hasEmptyHw && !hasEmptyAddr && !saving;

  $: assignedSensors = roles.filter(r => r.type === 'sensor' && assignedRoles.has(r.role));
  $: assignedActuators = roles.filter(r => r.type === 'actuator' && assignedRoles.has(r.role));

  $: hasNtc = !!$state['equipment.has_ntc_driver'];
  $: unassignedRoles = roles
    .filter(r => !assignedRoles.has(r.role))
    .filter(r => availableHw(r).length > 0);

  // ── Save / Restart ──
  async function save() {
    if (missingRequired.length > 0) {
      const names = missingRequired.map(r => r.label).join(', ');
      if (!confirm(`${$t['bind.confirm_missing'] || 'Відсутні обов\'язкові ролі'}: ${names}.\n${$t['bind.confirm_alarm'] || 'Система запуститься в аварійному режимі. Продовжити?'}`)) {
        return;
      }
    }
    saving = true;
    error = null;
    try {
      const res = await apiPost('/api/bindings', {
        manifest_version: 1,
        bindings: bindings,
      });
      if (res.needs_restart) needsRestart = true;
    } catch (e) {
      error = e.message;
    } finally {
      saving = false;
    }
  }

  async function restart() {
    try { await apiPost('/api/restart', {}); } catch (_) {}
    setTimeout(() => location.reload(), 5000);
  }
</script>

{#if loading}
  <div class="center-msg">{$t['bind.loading']}</div>
{:else if error && !bindings.length}
  <div class="center-msg error">{error}</div>
{:else}
  {#if needsRestart}
    <div class="modal-overlay" on:click|self={() => needsRestart = false}>
      <div class="modal-dialog">
        <div class="modal-icon">✓</div>
        <div class="modal-title">{$t['bind.saved_title']}</div>
        <div class="modal-text">{$t['bind.saved_msg']}</div>
        <div class="modal-actions">
          <button class="modal-btn-restart" on:click={restart}>{$t['bind.restart']}</button>
          <button class="modal-btn-later" on:click={() => needsRestart = false}>{$t['bind.later']}</button>
        </div>
      </div>
    </div>
  {/if}

  {#if error}
    <div class="error-banner">{error}</div>
  {/if}

  {#if missingRequired.length > 0}
    <div class="warning-banner">
      {$t['bind.required']}: {missingRequired.map(r => r.label).join(', ')}
    </div>
  {/if}

  <!-- Live equipment status -->
  {#if assignedRoles.size > 0}
    <EquipmentStatus sensors={assignedSensors} actuators={assignedActuators} />
  {/if}

  <div class="bind-grid">
    <!-- Sensors card -->
    {#if assignedSensors.length > 0}
      <Card title={$t['bind.sensors']}>
        {#each assignedSensors as roleDef}
          {@const binding = getBinding(roleDef.role)}
          {#if binding}
            <BindingCard {roleDef} {binding}
              hwList={compatibleHw(roleDef)}
              usedIds={usedHwIds(roleDef.role)}
              {assignedAddresses}
              on:changeHw={e => setHardware(e.detail.role, e.detail.hw)}
              on:changeAddr={e => setAddress(e.detail.role, e.detail.addr)}
              on:remove={e => removeRole(e.detail)} />
          {/if}
        {/each}
      </Card>
    {/if}

    <!-- Actuators card -->
    {#if assignedActuators.length > 0}
      <Card title={$t['bind.actuators']}>
        {#each assignedActuators as roleDef}
          {@const binding = getBinding(roleDef.role)}
          {#if binding}
            <BindingCard {roleDef} {binding}
              hwList={compatibleHw(roleDef)}
              usedIds={usedHwIds(roleDef.role)}
              on:changeHw={e => setHardware(e.detail.role, e.detail.hw)}
              on:remove={e => removeRole(e.detail)} />
          {/if}
        {/each}
      </Card>
    {/if}

    <!-- DS18B20 settings -->
    <OneWireDiscovery />

    <!-- NTC settings -->
    {#if hasNtc}
      <Card title="NTC">
        <NumberInput
          config={{ key: 'equipment.ntc_beta', description: $t['eq.ntc_beta'] || 'B-коефіцієнт', min: 2000, max: 5000, step: 1 }}
          value={$state['equipment.ntc_beta']}
        />
        <NumberInput
          config={{ key: 'equipment.ntc_r_series', description: $t['eq.ntc_series'] || 'Послідовний резистор', unit: 'Ом', min: 1000, max: 100000, step: 100 }}
          value={$state['equipment.ntc_r_series']}
        />
        <NumberInput
          config={{ key: 'equipment.ntc_r_nominal', description: $t['eq.ntc_nominal'] || 'Номінальний опір (25°C)', unit: 'Ом', min: 1000, max: 100000, step: 100 }}
          value={$state['equipment.ntc_r_nominal']}
        />
      </Card>
    {/if}

    <!-- Add optional roles -->
    {#if unassignedRoles.length > 0}
      <Card title={$t['bind.add_equip']}>
        {#each unassignedRoles as roleDef}
          <button class="add-role-btn" on:click={() => addRole(roleDef)}>
            + {roleDef.label}
          </button>
        {/each}
      </Card>
    {/if}
  </div>

  <!-- Save button -->
  <div class="save-area">
    <button class="save-btn" disabled={!canSave} on:click={save}>
      {saving ? $t['bind.saving'] : $t['bind.save']}
    </button>
  </div>
{/if}

<style>
  .center-msg {
    text-align: center;
    color: var(--fg-muted);
    padding: 40px;
    font-size: 16px;
  }
  .center-msg.error { color: var(--error); }

  .bind-grid {
    display: grid;
    grid-template-columns: 1fr;
    gap: 0;
  }
  @media (min-width: 768px) {
    .bind-grid {
      grid-template-columns: repeat(2, 1fr);
      gap: 16px;
    }
  }

  .modal-overlay {
    position: fixed;
    inset: 0;
    background: rgba(0, 0, 0, 0.55);
    display: flex;
    align-items: center;
    justify-content: center;
    z-index: 200;
    padding: 24px;
    backdrop-filter: blur(4px);
    -webkit-backdrop-filter: blur(4px);
  }
  .modal-dialog {
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: 16px;
    padding: 32px 28px 24px;
    max-width: 340px;
    width: 100%;
    text-align: center;
    box-shadow: 0 16px 48px rgba(0, 0, 0, 0.3);
  }
  .modal-icon {
    width: 48px;
    height: 48px;
    border-radius: 50%;
    background: rgba(34, 197, 94, 0.15);
    color: var(--ok);
    font-size: 24px;
    font-weight: 700;
    display: flex;
    align-items: center;
    justify-content: center;
    margin: 0 auto 16px;
  }
  .modal-title {
    font-size: 17px;
    font-weight: 600;
    color: var(--text-1);
    margin-bottom: 8px;
  }
  .modal-text {
    font-size: 14px;
    color: var(--text-3);
    line-height: 1.5;
    margin-bottom: 24px;
  }
  .modal-actions {
    display: flex;
    flex-direction: column;
    gap: 8px;
  }
  .modal-btn-restart {
    padding: 12px;
    border-radius: 10px;
    border: none;
    background: var(--accent);
    color: white;
    font-size: 15px;
    font-weight: 600;
    cursor: pointer;
    transition: opacity 0.15s;
  }
  .modal-btn-restart:hover { opacity: 0.9; }
  .modal-btn-later {
    padding: 10px;
    border-radius: 10px;
    border: none;
    background: transparent;
    color: var(--text-3);
    font-size: 13px;
    cursor: pointer;
    transition: color 0.15s;
  }
  .modal-btn-later:hover { color: var(--text-1); }

  .error-banner {
    background: rgba(239, 68, 68, 0.15);
    border: 1px solid var(--error);
    border-radius: 8px;
    padding: 10px 16px;
    margin-bottom: 16px;
    font-size: 13px;
    color: var(--error);
  }

  .warning-banner {
    background: rgba(245, 158, 11, 0.15);
    border: 1px solid var(--warning);
    border-radius: 8px;
    padding: 10px 16px;
    margin-bottom: 16px;
    font-size: 13px;
    color: var(--warning);
  }

  .add-role-btn {
    display: block;
    width: 100%;
    padding: 10px;
    margin-bottom: 8px;
    border-radius: 6px;
    border: 1px dashed var(--border);
    background: transparent;
    color: var(--accent);
    cursor: pointer;
    font-size: 14px;
    text-align: left;
  }
  .add-role-btn:last-child { margin-bottom: 0; }
  .add-role-btn:hover { background: var(--accent-bg); border-color: var(--accent); }

  .save-area {
    padding: 16px 0;
  }
  .save-btn {
    width: 100%;
    padding: 12px;
    border-radius: 8px;
    border: none;
    background: var(--accent);
    color: white;
    font-size: 15px;
    font-weight: 600;
    cursor: pointer;
  }
  .save-btn:hover { opacity: 0.9; }
  .save-btn:disabled { opacity: 0.4; cursor: not-allowed; }
</style>
