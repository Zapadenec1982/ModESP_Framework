<script>
  import { fade } from 'svelte/transition';
  import { t } from '../stores/i18n.js';
  import { needsLogin, setAuth } from '../lib/api.js';
  import { apiPost } from '../lib/api.js';

  let user = 'admin';
  let pass = '';
  let error = false;
  let loading = false;

  async function login() {
    if (!user || !pass) return;
    error = false;
    loading = true;

    // Зберігаємо credentials і пробуємо запит
    setAuth(user, pass);
    try {
      // Тестовий POST — якщо 401, credentials невірні
      await apiPost('/api/settings', {});
      needsLogin.set(false);
    } catch (e) {
      if (e.message === 'Unauthorized') {
        error = true;
        setAuth('', ''); // очистити невірні
      } else {
        // Інша помилка (400 тощо) — auth пройшов
        needsLogin.set(false);
      }
    } finally {
      loading = false;
    }
  }

  function onKeydown(e) {
    if (e.key === 'Enter') login();
  }
</script>

<div class="login-overlay" transition:fade={{ duration: 150 }}>
  <div class="login-card">
    <div class="login-title">{$t['auth.title']}</div>

    <label class="login-label" for="login-user">{$t['auth.user']}</label>
    <input
      id="login-user"
      class="login-input"
      type="text"
      bind:value={user}
      on:keydown={onKeydown}
      autocomplete="username"
    />

    <label class="login-label" for="login-pass">{$t['auth.pass']}</label>
    <input
      id="login-pass"
      class="login-input"
      type="password"
      bind:value={pass}
      on:keydown={onKeydown}
      autocomplete="current-password"
    />

    {#if error}
      <div class="login-error">{$t['auth.error']}</div>
    {/if}

    <button class="login-btn" on:click={login} disabled={loading || !pass}>
      {loading ? '...' : $t['auth.login']}
    </button>
  </div>
</div>

<style>
  .login-overlay {
    position: fixed;
    inset: 0;
    background: rgba(0, 0, 0, 0.5);
    display: flex;
    align-items: center;
    justify-content: center;
    z-index: 9500;
  }

  .login-card {
    background: var(--card, #fff);
    border-radius: 16px;
    padding: 32px 24px;
    width: 90%;
    max-width: 340px;
    box-shadow: 0 8px 32px rgba(0, 0, 0, 0.3);
  }

  .login-title {
    font-size: 20px;
    font-weight: 700;
    color: var(--fg);
    text-align: center;
    margin-bottom: 24px;
  }

  .login-label {
    display: block;
    font-size: 13px;
    color: var(--fg-muted);
    margin-bottom: 4px;
    margin-top: 12px;
  }

  .login-input {
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

  .login-input:focus {
    outline: none;
    border-color: var(--accent);
  }

  .login-error {
    color: var(--error, #ef4444);
    font-size: 13px;
    margin-top: 12px;
    text-align: center;
  }

  .login-btn {
    width: 100%;
    margin-top: 20px;
    padding: 12px;
    min-height: 44px;
    background: var(--accent);
    color: #fff;
    border: none;
    border-radius: 10px;
    font-size: 15px;
    font-weight: 600;
    cursor: pointer;
    transition: opacity 0.15s;
  }

  .login-btn:hover { opacity: 0.9; }
  .login-btn:disabled { opacity: 0.5; cursor: not-allowed; }
</style>
