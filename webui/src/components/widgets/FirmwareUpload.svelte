<script>
  import { apiGet, apiUpload } from '../../lib/api.js';
  import { setStateKey } from '../../stores/state.js';
  import { t } from '../../stores/i18n.js';
  import { toastWarn, toastSuccess } from '../../stores/toast.js';
  import { onMount } from 'svelte';

  export let config;

  let progress = 0;
  let uploading = false;
  let status = '';
  let fileInput;
  let selectedFile = null;
  let countdown = 0;
  let countdownTimer = null;

  const MAX_SAFE_SIZE = 1400 * 1024; // 1.4MB — margin від 1.5MB partition

  onMount(async () => {
    try {
      const d = await apiGet('/api/ota');
      setStateKey('_ota.version', d.version || '');
      setStateKey('_ota.partition', d.partition || '');
      setStateKey('_ota.idf', d.idf || '');
      setStateKey('_ota.date', `${d.date || ''} ${d.time || ''}`);
      if (d.board) setStateKey('_ota.board', d.board);
    } catch (e) {}
    return () => { if (countdownTimer) clearInterval(countdownTimer); };
  });

  function formatSize(bytes) {
    return bytes < 1024 * 1024
      ? (bytes / 1024).toFixed(0) + ' KB'
      : (bytes / (1024 * 1024)).toFixed(2) + ' MB';
  }

  function onSelect(e) {
    const file = e.target.files[0];
    if (!file) { selectedFile = null; return; }
    if (!file.name.endsWith('.bin')) {
      toastWarn($t['alert.only_bin']);
      fileInput.value = '';
      selectedFile = null;
      return;
    }
    selectedFile = file;
  }

  async function doUpload() {
    if (!selectedFile) return;
    if (!confirm($t['alert.confirm_ota'])) return;

    uploading = true;
    status = $t['ota.uploading'];
    progress = 0;

    try {
      await apiUpload('/api/ota', selectedFile, (pct, bytes) => {
        progress = pct;
        status = `${pct}% (${Math.round(bytes / 1024)} KB)`;
      });
      progress = 100;
      countdown = 5;
      status = $t['ota.restarting'].replace('{0}', String(countdown));
      toastSuccess($t['ota.done']);
      countdownTimer = setInterval(() => {
        countdown--;
        if (countdown <= 0) {
          clearInterval(countdownTimer);
          location.reload();
        } else {
          status = $t['ota.restarting'].replace('{0}', String(countdown));
        }
      }, 1000);
    } catch (e) {
      // Перевірка board mismatch помилки
      let msg = e.message;
      try {
        const err = JSON.parse(msg);
        if (err.error === 'board_mismatch') {
          msg = ($t['ota.board_mismatch'] || 'Board mismatch: {incoming} (expected {running})')
            .replace('{incoming}', err.incoming)
            .replace('{running}', err.running);
          toastWarn(msg);
        }
      } catch (_) {}
      status = 'Error: ' + msg;
      uploading = false;
    }
    fileInput.value = '';
    selectedFile = null;
  }
</script>

<div class="upload-widget">
  <input
    type="file"
    accept=".bin"
    bind:this={fileInput}
    on:change={onSelect}
    style="display:none"
  />

  {#if selectedFile}
    <div class="file-info">
      <span class="file-name">{selectedFile.name}</span>
      <span class="file-size" class:warn={selectedFile.size > MAX_SAFE_SIZE}>
        {formatSize(selectedFile.size)}
      </span>
    </div>
    {#if selectedFile.size > MAX_SAFE_SIZE}
      <div class="size-warn">{$t['ota.too_large']}</div>
    {/if}
    <button class="action-btn" disabled={uploading} on:click={doUpload}>
      {$t['ota.upload']}
    </button>
  {:else}
    <button
      class="action-btn select-btn"
      disabled={uploading}
      on:click={() => fileInput.click()}
    >
      {config.label || $t['ota.select']}
    </button>
  {/if}

  {#if uploading || countdown > 0}
    <div class="progress-area">
      <div class="progress-bar">
        <div class="progress-fill" style="width: {progress}%"></div>
      </div>
      <span class="progress-status">{status}</span>
    </div>
  {/if}
</div>

<style>
  .upload-widget { padding: 4px 0; }
  .file-info {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 8px 12px;
    margin-bottom: 8px;
    background: var(--bg);
    border: 1px solid var(--border);
    border-radius: 8px;
    font-size: 13px;
    color: var(--fg);
  }
  .file-name {
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
    margin-right: 8px;
  }
  .file-size { color: var(--fg-muted); white-space: nowrap; }
  .file-size.warn { color: var(--warning); font-weight: 600; }
  .size-warn {
    font-size: 12px;
    color: var(--warning);
    margin-bottom: 8px;
    padding: 0 4px;
  }
  .action-btn {
    background: linear-gradient(135deg, var(--accent), #0369a1);
    color: #fff; border: none;
    padding: 12px 24px; border-radius: 10px;
    cursor: pointer; font-size: 14px; font-weight: 600;
    width: 100%; transition: all 0.2s;
    box-shadow: 0 2px 8px rgba(59,130,246,0.3);
  }
  .select-btn {
    background: var(--bg);
    color: var(--fg);
    border: 1px dashed var(--border);
    box-shadow: none;
  }
  .select-btn:hover:not(:disabled) { border-color: var(--accent); color: var(--accent); }
  .action-btn:hover:not(:disabled) { transform: translateY(-1px); }
  .action-btn:disabled { opacity: 0.6; cursor: not-allowed; }
  .progress-area { margin-top: 12px; }
  .progress-bar {
    height: 8px; background: var(--border);
    border-radius: 4px; overflow: hidden;
  }
  .progress-fill {
    height: 100%; background: var(--accent);
    transition: width 0.3s; border-radius: 4px;
  }
  .progress-status {
    font-size: 12px; color: var(--fg-muted);
    margin-top: 6px; display: block;
  }
</style>
