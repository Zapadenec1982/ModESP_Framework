<script>
  import { apiPost } from '../../lib/api.js';
  import { toastError } from '../../stores/toast.js';
  import { t } from '../../stores/i18n.js';

  export let config;

  let loadingId = null;
  let fileInput;
  let pendingAction = null;

  async function handleAction(action) {
    if (loadingId) return;
    if (action.confirm && !confirm(action.confirm)) return;

    if (action.accept) {
      pendingAction = action;
      fileInput.click();
      return;
    }

    loadingId = action.id;
    try {
      if (action.download) {
        const r = await fetch(action.api_endpoint);
        if (!r.ok) throw new Error(`${r.status}`);
        const blob = await r.blob();
        const cd = r.headers.get('Content-Disposition') || '';
        const m = cd.match(/filename="?([^";\s]+)"?/);
        const a = document.createElement('a');
        a.href = URL.createObjectURL(blob);
        a.download = m ? m[1] : 'backup.json';
        a.click();
        URL.revokeObjectURL(a.href);
      } else {
        await apiPost(action.api_endpoint, {});
      }
    } catch (e) {
      toastError(($t['btn.error'] || 'Error') + ': ' + e.message);
    } finally {
      loadingId = null;
    }
  }

  async function onFile(e) {
    const file = e.target.files[0];
    if (!file || !pendingAction) { fileInput.value = ''; return; }

    const action = pendingAction;
    pendingAction = null;
    loadingId = action.id;

    try {
      const text = await file.text();
      const data = JSON.parse(text);
      await apiPost(action.api_endpoint, data);
      setTimeout(() => location.reload(), 5000);
    } catch (e) {
      toastError(($t['btn.error'] || 'Error') + ': ' + e.message);
    } finally {
      loadingId = null;
      fileInput.value = '';
    }
  }
</script>

<input
  type="file"
  accept=".json"
  bind:this={fileInput}
  on:change={onFile}
  style="display:none"
/>

<div class="actions-grid">
  {#each config.actions as action}
    <button
      class="grid-btn"
      class:danger={action.style === 'danger'}
      disabled={loadingId !== null}
      on:click={() => handleAction(action)}
    >
      {#if loadingId === action.id}
        <span class="icon spin">⏳</span>
      {:else}
        <span class="icon">{action.icon}</span>
      {/if}
      <span class="label">{action.label}</span>
    </button>
  {/each}
</div>

<style>
  .actions-grid {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 8px;
  }
  .grid-btn {
    display: flex;
    align-items: center;
    justify-content: center;
    gap: 6px;
    padding: 10px 8px;
    border: 1px solid var(--border);
    border-radius: var(--radius-lg, 10px);
    background: var(--bg);
    color: var(--fg);
    font-size: 13px;
    cursor: pointer;
    transition: border-color 0.15s, color 0.15s, background 0.15s;
    min-height: 44px;
    line-height: 1.2;
    text-align: center;
  }
  .grid-btn:hover:not(:disabled) {
    border-color: var(--accent);
    color: var(--accent);
  }
  .grid-btn:disabled { opacity: 0.5; cursor: not-allowed; }
  .grid-btn.danger { color: var(--error, #dc2626); border-color: var(--error, #dc2626); }
  .grid-btn.danger:hover:not(:disabled) {
    background: var(--error, #dc2626);
    color: #fff;
  }
  .icon { font-size: 16px; flex-shrink: 0; }
  .label { font-size: 12px; }
  .spin { animation: spin 1s linear infinite; }
  @keyframes spin {
    from { transform: rotate(0deg); }
    to { transform: rotate(360deg); }
  }
</style>
