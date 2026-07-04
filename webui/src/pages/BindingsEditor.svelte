<script>
  import { onMount } from 'svelte';
  import { apiGet, apiPost } from '../lib/api.js';
  import { pages } from '../stores/ui.js';
  import { t } from '../stores/i18n.js';
  import Card from '../components/Card.svelte';
  import EquipmentStatus from './bindings/EquipmentStatus.svelte';
  import BindingCard from './bindings/BindingCard.svelte';

  // Roles/hardware metadata з ui.json bindings page
  $: bindingsPage = $pages.find(p => p.id === 'bindings') || {};
  $: roles = bindingsPage.roles || [];

  // Hardware inventory = board.json (build-time, from ui.json) ∪ runtime-subscribed
  // devices (GET /api/devices — remote devices the user added on the Devices page),
  // runtime-wins-by-id. A subscribed device surfaces in the hardware dropdown for every
  // compatible role and the role binds to it exactly like a factory device — no MAC in
  // the binding. Mirrors find_ble_device's merge on the firmware side.
  $: boardHw = bindingsPage.hardware || [];
  $: hwInventory = mergeHw(boardHw, runtimeDevices);

  function mergeHw(board, runtime) {
    const byId = new Map(board.map(h => [h.id, h]));
    for (const d of runtime) {
      byId.set(d.id, {
        id: d.id,
        hw_type: d.hw_type || 'ble',
        // The device's identified driver (e.g. ble_nrf_tilt). hw_type "ble" is shared by
        // several drivers, so a role accepts a device only when the device's driver is one
        // of the role's drivers — carried here for compatibleHw()/driverForHw().
        driver: d.driver || '',
        label: d.label || d.id,
        shareable: true,   // a remote device backs several channel-roles (temp/hum/batt)
      });
    }
    return [...byId.values()];
  }

  // Поточні bindings (завантажуються з /api/bindings)
  let bindings = [];
  let runtimeDevices = [];   // GET /api/devices — user-subscribed remote devices
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
    }
    // Runtime device registry — degrade gracefully: if it fails or is empty the editor
    // still lists board.json hardware, so a BLE-less build is unaffected.
    try {
      const dd = await apiGet('/api/devices');
      runtimeDevices = dd.devices || [];
    } catch (e) {
      /* no runtime devices — factory hardware only */
    }
    loading = false;
  });

  // ── Hardware helpers ──
  function compatibleHw(roleDef) {
    const types = roleDef.hw_types || (roleDef.hw_type ? [roleDef.hw_type] : []);
    const drivers = roleDef.drivers || (roleDef.driver ? [roleDef.driver] : []);
    // A device that declares its own driver (a subscribed BLE device) matches only when
    // that driver is one the role accepts — so an nRF tilt device is NOT offered to a
    // Xiaomi temperature role even though both are hw_type "ble". A driverless "ble" row
    // (a legacy pre-F8 subscription) is ambiguous — never offer it to a typed role. Board
    // hardware (no per-device driver, wired hw_type) matches by hw_type as before.
    return hwInventory.filter(h => {
      if (h.driver) return drivers.includes(h.driver);
      if (h.hw_type === 'ble') return false;
      return types.some(t => t === h.hw_type);
    });
  }

  // Визначити правильний драйвер обраного hardware. A subscribed device carries its own
  // identified driver (disambiguates the shared "ble" hw_type); otherwise pick the role's
  // driver by the hw_type index.
  function driverForHw(roleDef, hwId) {
    const hw = hwInventory.find(h => h.id === hwId);
    if (!hw) return roleDef.driver || (roleDef.drivers && roleDef.drivers[0]) || '';
    if (hw.driver) return hw.driver;
    const types = roleDef.hw_types || [];
    const drivers = roleDef.drivers || (roleDef.driver ? [roleDef.driver] : []);
    const idx = types.indexOf(hw.hw_type);
    return idx >= 0 && idx < drivers.length ? drivers[idx] : drivers[0] || '';
  }

  function usedHwIds(excludeRole) {
    // A shareable bus (driver multiple_per_bus → ui.json hw.shareable) carries several
    // roles at once, so it never counts as "used". Manifest-driven, no hardcoded set.
    return new Set(bindings
      .filter(b => b.role !== excludeRole)
      .map(b => b.hardware)
      .filter(hwId => {
        const hw = hwInventory.find(h => h.id === hwId);
        return hw && !hw.shareable;
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
    // Clear the address when leaving a shareable bus, or moving onto a different
    // shareable bus (the old per-device address no longer applies). hw.shareable is
    // manifest-driven (driver multiple_per_bus), not a hardcoded hw_type.
    const clearAddr = (oldHw?.shareable && !newHw?.shareable)
                   || (oldHw?.id !== hwId && newHw?.shareable);
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
      // Role's module comes from ui.json (the generator emits role.module for every
      // role provider), so the editor binds display/player/any module correctly.
      module: roleDef.module,
    }];
  }

  // ── Derived state ──
  $: assignedRoles = new Set(bindings.map(b => b.role));
  $: assignedAddresses = new Set(bindings.filter(b => b.address).map(b => b.address));
  $: requiredRoles = roles.filter(r => !r.optional);
  $: missingRequired = requiredRoles.filter(r => !assignedRoles.has(r.role));
  $: hasEmptyHw = bindings.some(b => !b.hardware);
  $: hasEmptyAddr = bindings.some(b => {
    // A binding needs an address only if its role's driver requires one
    // (requires_address, from ui.json) — not "any OneWire bus".
    const roleDef = roles.find(r => r.role === b.role);
    return roleDef && roleDef.requires_address && !b.address;
  });
  $: canSave = !hasEmptyHw && !hasEmptyAddr && !saving;

  $: assignedSensors = roles.filter(r => r.type === 'sensor' && assignedRoles.has(r.role));
  $: assignedActuators = roles.filter(r => r.type === 'actuator' && assignedRoles.has(r.role));

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
              {assignedAddresses}
              on:changeHw={e => setHardware(e.detail.role, e.detail.hw)}
              on:changeAddr={e => setAddress(e.detail.role, e.detail.addr)}
              on:remove={e => removeRole(e.detail)} />
          {/if}
        {/each}
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
