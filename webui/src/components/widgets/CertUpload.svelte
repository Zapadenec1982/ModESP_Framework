<script>
  import { apiPost } from '../../lib/api.js';
  import { setStateKey } from '../../stores/state.js';
  import { t } from '../../stores/i18n.js';
  import { toastSuccess, toastError } from '../../stores/toast.js';

  export let config;

  let certPem = '';
  let keyPem = '';
  let loading = false;
  let expanded = false;

  async function upload() {
    if (!certPem.trim() || !keyPem.trim()) {
      toastError('Certificate and private key are required');
      return;
    }
    loading = true;
    try {
      const r = await apiPost(config.api_endpoint || '/api/cloud', {
        cert: certPem.trim(),
        key: keyPem.trim(),
      });
      if (r.ok) {
        toastSuccess('Certificate uploaded!');
        setStateKey('cloud.cert_loaded', true);
        certPem = '';
        keyPem = '';
        expanded = false;
      } else {
        toastError('Upload failed');
      }
    } catch (e) {
      toastError('Error: ' + e.message);
    } finally {
      loading = false;
    }
  }
</script>

<div class="cert-widget">
  <button class="toggle-btn" on:click={() => expanded = !expanded}>
    {expanded ? '▾' : '▸'} {config.label || 'Upload Certificate'}
  </button>

  {#if expanded}
    <div class="cert-form">
      <label class="cert-label">
        Device Certificate (PEM)
        <textarea
          class="cert-textarea"
          bind:value={certPem}
          placeholder="-----BEGIN CERTIFICATE-----&#10;...&#10;-----END CERTIFICATE-----"
          rows="6"
        ></textarea>
      </label>

      <label class="cert-label">
        Private Key (PEM)
        <textarea
          class="cert-textarea"
          bind:value={keyPem}
          placeholder="-----BEGIN RSA PRIVATE KEY-----&#10;...&#10;-----END RSA PRIVATE KEY-----"
          rows="6"
        ></textarea>
      </label>

      <button class="upload-btn" disabled={loading} on:click={upload}>
        {loading ? 'Uploading...' : 'Upload'}
      </button>
    </div>
  {/if}
</div>

<style>
  .cert-widget { padding: 4px 0; }
  .toggle-btn {
    background: none; border: 1px solid var(--border);
    color: var(--text); padding: 8px 16px; border-radius: 8px;
    cursor: pointer; font-size: 13px; width: 100%; text-align: left;
    transition: background 0.2s;
  }
  .toggle-btn:hover { background: var(--bg-card-hover, rgba(255,255,255,0.05)); }
  .cert-form { margin-top: 12px; display: flex; flex-direction: column; gap: 12px; }
  .cert-label { display: flex; flex-direction: column; gap: 4px; font-size: 12px; color: var(--text-secondary); }
  .cert-textarea {
    background: var(--bg-input, rgba(0,0,0,0.2)); color: var(--text);
    border: 1px solid var(--border); border-radius: 8px;
    padding: 8px 12px; font-family: monospace; font-size: 11px;
    resize: vertical; min-height: 80px;
  }
  .cert-textarea:focus { outline: none; border-color: var(--accent); }
  .upload-btn {
    background: linear-gradient(135deg, #16a34a, #15803d);
    color: #fff; border: none;
    padding: 10px 20px; border-radius: 10px;
    cursor: pointer; font-size: 14px; font-weight: 600;
    transition: all 0.2s;
  }
  .upload-btn:hover:not(:disabled) { transform: translateY(-1px); }
  .upload-btn:disabled { opacity: 0.6; cursor: not-allowed; }
</style>
