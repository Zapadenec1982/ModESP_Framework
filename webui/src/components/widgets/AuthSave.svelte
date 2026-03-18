<script>
  import { onMount } from 'svelte';
  import { apiGet, needsLogin, setAuth, clearAuth } from '../../lib/api.js';
  import { t } from '../../stores/i18n.js';
  import { toastSuccess, toastError, toastWarn } from '../../stores/toast.js';

  export let config;
  export let value;

  let loading = false;
  let enabled = true;
  let username = 'admin';
  let currentPass = '';
  let newPass = '';
  let confirmPass = '';

  onMount(async () => {
    try {
      const d = await apiGet('/api/auth');
      enabled = d.enabled;
      username = d.username || 'admin';
    } catch (e) {}
  });

  function authHeaders() {
    const creds = sessionStorage.getItem('modesp_auth');
    const h = { 'Content-Type': 'application/json' };
    if (creds) h['Authorization'] = `Basic ${creds}`;
    return h;
  }

  async function postAuth(data) {
    const resp = await fetch('/api/auth', {
      method: 'POST',
      headers: authHeaders(),
      body: JSON.stringify(data)
    });
    return { status: resp.status, data: await resp.json().catch(() => ({})) };
  }

  async function save() {
    if (newPass && newPass !== confirmPass) {
      toastWarn($t['auth.pass_mismatch']);
      return;
    }
    if (newPass && newPass.length < 4) {
      toastWarn($t['auth.pass_short']);
      return;
    }

    loading = true;
    const body = { enabled };
    if (username) body.username = username;
    if (newPass) {
      if (!currentPass) { toastWarn($t['auth.current_pass_required']); loading = false; return; }
      body.new_pass = newPass;
      body.current_pass = currentPass;
    }

    try {
      const r = await postAuth(body);
      if (r.status === 200 && r.data.ok) {
        toastSuccess($t['auth.saved']);
        if (!enabled) {
          clearAuth();
          needsLogin.set(false);
        } else if (newPass) {
          setAuth(username, newPass);
        } else if (username) {
          // Username змінився — потрібно оновити credentials
          // Але пароль не змінився — беремо currentPass або існуючий
          const creds = sessionStorage.getItem('modesp_auth');
          if (creds) {
            try {
              const decoded = atob(creds);
              const oldPass = decoded.substring(decoded.indexOf(':') + 1);
              setAuth(username, oldPass);
            } catch (e) {}
          }
        }
        currentPass = '';
        newPass = '';
        confirmPass = '';
      } else if (r.data.error === 'wrong_password') {
        toastError($t['auth.error']);
      } else {
        toastError($t['alert.error']);
      }
    } catch (e) {
      toastError($t['alert.error']);
    } finally {
      loading = false;
    }
  }

  async function resetDefaults() {
    if (!currentPass) {
      toastWarn($t['auth.current_pass_required']);
      return;
    }
    if (!confirm($t['auth.reset_confirm'])) return;

    loading = true;
    try {
      const r = await postAuth({ reset: true, current_pass: currentPass });
      if (r.status === 200 && r.data.ok) {
        toastSuccess($t['auth.reset_done']);
        enabled = true;
        username = 'admin';
        setAuth('admin', 'modesp');
        currentPass = '';
        newPass = '';
        confirmPass = '';
      } else if (r.data.error === 'wrong_password') {
        toastError($t['auth.error']);
      } else {
        toastError($t['alert.error']);
      }
    } catch (e) {
      toastError($t['alert.error']);
    } finally {
      loading = false;
    }
  }
</script>

<div class="auth-form">
  <div class="auth-row">
    <span class="auth-label">{$t['auth.enabled']}</span>
    <button class="toggle" class:on={enabled} on:click={() => enabled = !enabled}
            type="button" aria-label="Toggle auth">
      <span class="toggle-thumb"></span>
    </button>
  </div>

  <label class="field-label" for="auth-user">{$t['auth.user']}</label>
  <input id="auth-user" class="field-input" type="text" bind:value={username}
         autocomplete="username" />

  <label class="field-label" for="auth-cur">{$t['auth.current_pass']}</label>
  <input id="auth-cur" class="field-input" type="password" bind:value={currentPass}
         autocomplete="current-password" />

  <label class="field-label" for="auth-new">{$t['auth.new_pass']}</label>
  <input id="auth-new" class="field-input" type="password" bind:value={newPass}
         autocomplete="new-password" />

  <label class="field-label" for="auth-confirm">{$t['auth.confirm_pass']}</label>
  <input id="auth-confirm" class="field-input" type="password" bind:value={confirmPass}
         autocomplete="new-password" />

  <div class="auth-buttons">
    <button class="save-btn" disabled={loading} on:click={save}>
      {loading ? '...' : $t['btn.save']}
    </button>
    <button class="reset-btn" disabled={loading} on:click={resetDefaults}>
      {$t['auth.reset']}
    </button>
  </div>
</div>

<style>
  .auth-form { padding: 4px 0; }

  .auth-row {
    display: flex;
    align-items: center;
    justify-content: space-between;
    min-height: 44px;
    margin-bottom: 12px;
  }

  .auth-label {
    font-size: 14px;
    color: var(--fg);
    font-weight: 500;
  }

  .toggle {
    position: relative;
    width: 48px;
    height: 28px;
    border-radius: 14px;
    border: none;
    background: var(--border);
    cursor: pointer;
    transition: background 0.2s;
    padding: 0;
    flex-shrink: 0;
  }
  .toggle.on { background: var(--accent); }
  .toggle-thumb {
    position: absolute;
    top: 3px;
    left: 3px;
    width: 22px;
    height: 22px;
    border-radius: 50%;
    background: #fff;
    transition: transform 0.2s;
  }
  .toggle.on .toggle-thumb { transform: translateX(20px); }

  .field-label {
    display: block;
    font-size: 13px;
    color: var(--fg-muted);
    margin-bottom: 4px;
    margin-top: 10px;
  }

  .field-input {
    width: 100%;
    padding: 10px 12px;
    font-size: 14px;
    min-height: 44px;
    background: var(--bg);
    border: 1px solid var(--border);
    border-radius: 8px;
    color: var(--fg);
    box-sizing: border-box;
  }
  .field-input:focus {
    outline: none;
    border-color: var(--accent);
  }

  .auth-buttons {
    display: flex;
    gap: 8px;
    margin-top: 16px;
  }

  .save-btn {
    flex: 1;
    padding: 12px;
    min-height: 44px;
    background: var(--accent);
    color: #fff;
    border: none;
    border-radius: 10px;
    font-size: 14px;
    font-weight: 600;
    cursor: pointer;
    transition: opacity 0.15s;
  }
  .save-btn:hover:not(:disabled) { opacity: 0.9; }
  .save-btn:disabled { opacity: 0.5; cursor: not-allowed; }

  .reset-btn {
    flex: 1;
    padding: 12px;
    min-height: 44px;
    background: transparent;
    color: var(--error, #ef4444);
    border: 1px solid var(--error, #ef4444);
    border-radius: 10px;
    font-size: 14px;
    font-weight: 600;
    cursor: pointer;
    transition: all 0.15s;
  }
  .reset-btn:hover:not(:disabled) { background: rgba(239, 68, 68, 0.1); }
  .reset-btn:disabled { opacity: 0.5; cursor: not-allowed; }
</style>
