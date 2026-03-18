<script>
  import { apiPost } from '../../lib/api.js';

  export let config;

  let loading = false;
  let status = '';
  let fileInput;

  async function onFile(e) {
    const file = e.target.files[0];
    if (!file) return;

    if (config.confirm && !confirm(config.confirm)) {
      fileInput.value = '';
      return;
    }

    loading = true;
    status = '';

    try {
      const text = await file.text();
      const data = JSON.parse(text);
      const result = await apiPost(config.api_endpoint, data);
      status = result.message || 'OK';
      // Перезавантаження після restore
      setTimeout(() => location.reload(), 5000);
    } catch (e) {
      status = 'Error: ' + e.message;
    } finally {
      loading = false;
      fileInput.value = '';
    }
  }
</script>

<div class="upload-widget">
  <input
    type="file"
    accept={config.accept || '.json'}
    bind:this={fileInput}
    on:change={onFile}
    style="display:none"
  />
  <button
    class="action-btn"
    disabled={loading}
    on:click={() => fileInput.click()}
  >
    {loading ? '...' : (config.label || 'Upload')}
  </button>
  {#if status}
    <span class="upload-status">{status}</span>
  {/if}
</div>

<style>
  .upload-widget { padding: 4px 0; }
  .action-btn {
    background: linear-gradient(135deg, var(--accent), #0369a1);
    color: #fff; border: none;
    padding: 12px 24px; border-radius: 10px;
    cursor: pointer; font-size: 14px; font-weight: 600;
    width: 100%; transition: all 0.2s;
    box-shadow: 0 2px 8px rgba(59,130,246,0.3);
  }
  .action-btn:hover:not(:disabled) {
    transform: translateY(-1px);
    box-shadow: 0 4px 12px rgba(59,130,246,0.4);
  }
  .action-btn:active:not(:disabled) { transform: translateY(0); }
  .action-btn:disabled { opacity: 0.6; cursor: not-allowed; }
  .upload-status {
    font-size: 12px; color: var(--fg-muted);
    margin-top: 8px; display: block;
  }
</style>
