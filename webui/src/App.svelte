<script>
  import { onMount } from "svelte";
  import { fade, fly, scale } from "svelte/transition";
  import {
    loadUiConfig,
    uiLoading,
    uiError,
    deviceName,
    pages,
    navigateTo,
  } from "./stores/ui.js";
  import { initWebSocket, state } from "./stores/state.js";
  import { apiGet, apiPost, needsLogin } from "./lib/api.js";
  import { t } from "./stores/i18n.js";
  import "./stores/theme.js";
  import Layout from "./components/Layout.svelte";
  import Toast from "./components/Toast.svelte";
  import LoginModal from "./components/LoginModal.svelte";
  import Dashboard from "./pages/Dashboard.svelte";
  import DynamicPage from "./pages/DynamicPage.svelte";
  import BindingsEditor from "./pages/BindingsEditor.svelte";

  let currentPage = "dashboard";

  onMount(async () => {
    await loadUiConfig();
    try {
      const s = await apiGet("/api/state");
      state.update((st) => ({ ...st, ...s }));
    } catch (e) {
      console.warn("Initial state load failed", e);
    }
    // Auth probe: POST з пустим body → 401 якщо auth потрібен
    try {
      await apiPost("/api/settings", {});
    } catch (e) {
      /* 401 → needsLogin */
    }
    initWebSocket();
  });

  $: document.title = $deviceName;
  $: if ($navigateTo) {
    currentPage = $navigateTo;
    $navigateTo = null;
  }
</script>

{#if $uiLoading}
  <div class="loading-screen" in:fade={{ duration: 200 }}>
    <div class="loading-spinner"></div>
    <div class="loading-text">{$t["app.loading"]}</div>
  </div>
{:else if $uiError}
  <div class="error-screen" in:scale={{ start: 0.95, duration: 200 }}>
    <div class="error-icon">!</div>
    <div class="error-title">{$t["app.error"]}</div>
    <div class="error-msg">{$uiError}</div>
    <button class="retry-btn" on:click={() => location.reload()}
      >{$t["app.retry"]}</button
    >
  </div>
{:else}
  <Layout bind:currentPage>
    {#key currentPage}
      <div
        in:fly={{ x: 20, duration: 200, delay: 50 }}
        out:fade={{ duration: 100 }}
      >
        {#if currentPage === "dashboard"}
          <Dashboard />
        {:else if currentPage === "bindings"}
          <BindingsEditor />
        {:else}
          <DynamicPage pageId={currentPage} />
        {/if}
      </div>
    {/key}
  </Layout>
{/if}

{#if $needsLogin}
  <LoginModal />
{/if}

<Toast />

<style>
  /* Theme colors (handled by tokens.css primarily) */
  :global(:root) {
    /* Legacy variables mapping to new tokens */
    --color-scheme: dark;
  }

  :global(:root[data-theme="light"]) {
    /* Light mode is deprecated, mapped to dark for now */
    --color-scheme: light;
  }

  :global(*) {
    margin: 0;
    padding: 0;
    box-sizing: border-box;
    -webkit-tap-highlight-color: transparent;
  }

  :global(body) {
    background: var(--bg);
    color: var(--text-1);
    font-family: var(--font-family);
    -webkit-font-smoothing: antialiased;
    -moz-osx-font-smoothing: grayscale;
    min-height: 100vh;
    max-width: 100%;
    overflow-x: hidden;
    transition:
      background var(--transition-slow),
      color var(--transition-slow);
  }

  /* ── Ambient Background & Noise ── */
  :global(.bg-layer) {
    position: fixed;
    inset: 0;
    z-index: -2;
    background: radial-gradient(
      circle at 50% -20%,
      var(--surface-2) 0%,
      var(--bg) 80%
    );
    transition: background 0.5s;
  }

  :global(.bg-noise) {
    position: fixed;
    inset: 0;
    z-index: -1;
    opacity: 0.015;
    pointer-events: none;
    background-image: url("data:image/svg+xml,%3Csvg viewBox='0 0 200 200' xmlns='http://www.w3.org/2000/svg'%3E%3Cfilter id='noiseFilter'%3E%3CfeTurbulence type='fractalNoise' baseFrequency='0.85' numOctaves='3' stitchTiles='stitch'/%3E%3C/filter%3E%3Crect width='100%25' height='100%25' filter='url(%23noiseFilter)'/%3E%3C/svg%3E");
    background-repeat: repeat;
  }

  :global([data-theme="light"] .bg-noise) {
    opacity: 0.03;
  }

  /* Theme-specific mesh gradients */
  :global([data-theme="dark"] .bg-layer::after) {
    content: "";
    position: absolute;
    top: -20%;
    left: -10%;
    width: 60%;
    height: 60%;
    background: radial-gradient(circle, var(--accent-glow) 0%, transparent 60%);
    filter: blur(100px);
    opacity: 0.6;
    animation: bgPulse 12s ease-in-out infinite alternate;
    will-change: transform;
  }
  :global([data-theme="light"] .bg-layer::after) {
    content: "";
    position: absolute;
    top: -10%;
    left: 20%;
    width: 60%;
    height: 40%;
    background: radial-gradient(
      ellipse,
      var(--accent-glow-s) 0%,
      transparent 60%
    );
    filter: blur(80px);
    opacity: 0.8;
  }

  @keyframes bgPulse {
    0% {
      transform: translate(0, 0) scale(1);
    }
    100% {
      transform: translate(5%, 5%) scale(1.1);
    }
  }

  .loading-screen {
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    min-height: 100vh;
    gap: 16px;
  }

  .loading-spinner {
    width: 32px;
    height: 32px;
    border: 3px solid var(--border);
    border-top-color: var(--accent);
    border-radius: 50%;
    animation: spin 0.8s linear infinite;
  }

  @keyframes spin {
    to {
      transform: rotate(360deg);
    }
  }

  .loading-text {
    font-size: 14px;
    color: var(--fg-muted);
  }

  .error-screen {
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    min-height: 100vh;
    gap: 12px;
  }

  .error-icon {
    width: 48px;
    height: 48px;
    border-radius: 50%;
    background: var(--error);
    color: white;
    font-size: 24px;
    font-weight: 700;
    display: flex;
    align-items: center;
    justify-content: center;
  }

  .error-title {
    font-size: 18px;
    font-weight: 600;
  }

  .error-msg {
    font-size: 14px;
    color: var(--fg-muted);
  }

  .retry-btn {
    margin-top: 8px;
    padding: 10px 24px;
    border-radius: 8px;
    border: 1px solid var(--accent);
    background: transparent;
    color: var(--accent);
    font-size: 14px;
    cursor: pointer;
  }

  .retry-btn:hover {
    background: var(--accent-bg);
  }

  /* ===== Light Theme: Frosted Glass ===== */
  @supports (backdrop-filter: blur(1px)) {
    :global([data-theme="light"] .card),
    :global([data-theme="light"] .group),
    :global([data-theme="light"] .met),
    :global([data-theme="light"] .eq-cell),
    :global([data-theme="light"] .prot-row),
    :global([data-theme="light"] .diag-card),
    :global([data-theme="light"] .protect-status) {
      background: rgba(248, 250, 254, 0.72);
      backdrop-filter: blur(12px);
      -webkit-backdrop-filter: blur(12px);
      border-color: rgba(50, 80, 140, 0.14);
    }
  }

  :global([data-theme="light"] .card),
  :global([data-theme="light"] .group),
  :global([data-theme="light"] .protect-status),
  :global([data-theme="light"] .diag-card) {
    border-radius: 20px;
  }
</style>
