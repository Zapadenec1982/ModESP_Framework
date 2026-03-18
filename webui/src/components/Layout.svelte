<script>
  import { onDestroy } from "svelte";
  import { fade } from "svelte/transition";
  import { pages, deviceName } from "../stores/ui.js";
  import { wsConnected, state } from "../stores/state.js";
  import { theme, toggleTheme } from "../stores/theme.js";
  import { t, language, cycleLanguage } from "../stores/i18n.js";
  import { toastSuccess } from "../stores/toast.js";
  import Icon from "./Icon.svelte";
  import Clock from "./Clock.svelte";

  export let currentPage = "dashboard";

  // AUDIT-009: alarm banner на всіх сторінках
  $: alarmActive = $state["protection.alarm_active"];
  $: alarmCode = $state["protection.alarm_code"];

  function navigate(id) {
    currentPage = id;
  }

  $: currentTitle = $pages.find((p) => p.id === currentPage)?.title || "";
  $: sortedPages = [...$pages].sort((a, b) => (a.order || 0) - (b.order || 0));

  // Mobile bottom tabs: max 4 visible + "More" if > 5 pages
  const MAX_TABS = 4;
  $: hasMore = sortedPages.length > MAX_TABS + 1;
  $: visibleTabs = hasMore ? sortedPages.slice(0, MAX_TABS) : sortedPages;
  $: moreTabs = hasMore ? sortedPages.slice(MAX_TABS) : [];

  // "More" overlay state
  let moreOpen = false;

  function navigateMore(id) {
    currentPage = id;
    moreOpen = false;
  }

  // Connection overlay — показувати через 5с після disconnect (уникнути flicker)
  let showOverlay = false;
  let overlayTimer = null;
  let wasDisconnected = false;

  const unsub = wsConnected.subscribe((connected) => {
    if (!connected) {
      if (!overlayTimer) {
        wasDisconnected = true;
        overlayTimer = setTimeout(() => {
          showOverlay = true;
        }, 5000);
      }
    } else {
      if (overlayTimer) {
        clearTimeout(overlayTimer);
        overlayTimer = null;
      }
      if (showOverlay) toastSuccess($t["conn.restored"]);
      showOverlay = false;
      wasDisconnected = false;
    }
  });

  onDestroy(() => {
    unsub();
    if (overlayTimer) clearTimeout(overlayTimer);
  });
</script>

<!-- Ambient background effect (Managed globally in App.svelte for noise/mesh) -->
<div class="bg-layer"></div>
<div class="bg-noise"></div>
<div
  class="ambient"
  class:alarm-ambient={alarmActive}
  class:ok-ambient={!alarmActive}
></div>

<div class="layout">
  <!-- Sidebar (desktop) -->
  <aside class="sidebar">
    <div class="sidebar-header">
      <span class="logo">❄</span>
      <span class="logo-text">{$deviceName}</span>
    </div>
    <nav class="sidebar-nav">
      {#each sortedPages as page}
        <button
          class="nav-item"
          class:active={currentPage === page.id}
          on:click={() => navigate(page.id)}
        >
          <Icon name={page.icon || "home"} size={20} />
          <span class="nav-label">{page.title}</span>
        </button>
      {/each}
    </nav>
    <div class="sidebar-footer">
      <div class="ws-status" class:connected={$wsConnected}>
        <span class="ws-dot"></span>
        {$wsConnected ? $t["status.online"] : $t["status.offline"]}
      </div>
    </div>
  </aside>

  <!-- Main content -->
  <div class="main-area">
    <!-- AUDIT-009: alarm banner на всіх сторінках -->
    {#if alarmActive}
      <div
        class="alarm-banner"
        role="button"
        tabindex="0"
        on:click={() => navigate("protection")}
        on:keydown={(e) => e.key === "Enter" && navigate("protection")}
      >
        {$t["alarm.banner"]}: {alarmCode
          ? String(alarmCode).toUpperCase().replace("_", " ")
          : ""}
      </div>
    {/if}
    <header class="topbar">
      <h1 class="topbar-title">{currentTitle}</h1>
      <div class="topbar-right">
        <Clock />
        <button class="theme-tog" on:click={toggleTheme} title="Toggle Theme">
          {#if $theme === "dark"}
            <svg
              width="18"
              height="18"
              viewBox="0 0 24 24"
              fill="none"
              stroke="currentColor"
              stroke-width="2"
              ><circle cx="12" cy="12" r="5" /><path
                d="M12 1v2M12 21v2M4.22 4.22l1.42 1.42M18.36 18.36l1.42 1.42M1 12h2M21 12h2M4.22 19.78l1.42-1.42M18.36 5.64l1.42-1.42"
              /></svg
            >
          {:else}
            <svg
              width="18"
              height="18"
              viewBox="0 0 24 24"
              fill="none"
              stroke="currentColor"
              stroke-width="2"
              ><path d="M21 12.79A9 9 0 1 1 11.21 3 7 7 0 0 0 21 12.79z" /></svg
            >
          {/if}
        </button>
        <button class="topbar-btn" on:click={cycleLanguage} title="Language">
          {$language.toUpperCase()}
        </button>
        <div class="ws-badge" class:connected={$wsConnected}>
          <span class="ws-dot"></span>
        </div>
      </div>
    </header>
    <main class="content">
      <slot />
    </main>

    <!-- Connection overlay (після 5с disconnect) -->
    {#if showOverlay}
      <div class="conn-overlay" transition:fade={{ duration: 200 }}>
        <div class="conn-dialog">
          <div class="conn-spinner"></div>
          <div class="conn-text">{$t["conn.lost"]}</div>
          <button class="conn-retry" on:click={() => location.reload()}>
            {$t["conn.retry"]}
          </button>
        </div>
      </div>
    {/if}
  </div>

  <!-- Bottom tabs (mobile) using premium styling -->
  <nav class="bottom-tabs">
    <div class="tabbar-bg">
      {#each visibleTabs as page}
        <button
          class="tb"
          class:active={currentPage === page.id}
          on:click={() => navigate(page.id)}
        >
          <div class="ti">
            <Icon name={page.icon || "home"} size={22} />
          </div>
          <span class="tl">{page.title}</span>
        </button>
      {/each}
      {#if hasMore}
        <button
          class="tb"
          class:active={moreTabs.some((p) => p.id === currentPage)}
          on:click={() => (moreOpen = !moreOpen)}
        >
          <div class="ti">
            <Icon name="more-horizontal" size={22} />
          </div>
          <span class="tl">{$t["nav.more"]}</span>
        </button>
      {/if}
    </div>
  </nav>

  <!-- "More" overlay -->
  {#if moreOpen}
    <div class="more-overlay" transition:fade={{ duration: 150 }}>
      <div class="more-sheet">
        <div class="more-header">
          <span class="more-title">{$t["nav.more"]}</span>
          <button class="more-close" on:click={() => (moreOpen = false)}>
            <Icon name="x" size={20} />
          </button>
        </div>
        {#each moreTabs as page}
          <button
            class="more-item"
            class:active={currentPage === page.id}
            on:click={() => navigateMore(page.id)}
          >
            <Icon name={page.icon || "home"} size={22} />
            <span>{page.title}</span>
          </button>
        {/each}
      </div>
    </div>
  {/if}
</div>

<style>
  /* === Ambient Background === */
  .ambient {
    position: fixed;
    top: 0;
    left: 0;
    right: 0;
    bottom: 0;
    pointer-events: none;
    z-index: 0;
    overflow: hidden;
  }
  .ambient::before {
    content: "";
    position: absolute;
    top: -80px;
    left: 50%;
    transform: translateX(-50%);
    width: 350px;
    height: 350px;
    opacity: 0.4;
    transition: all 0.8s;
    background: radial-gradient(circle, var(--blue-glow) 0%, transparent 70%);
  }
  .alarm-ambient.ambient::before {
    background: radial-gradient(circle, var(--red-glow) 0%, transparent 70%);
    opacity: 0.6;
    animation: ambPulse 3s ease-in-out infinite;
  }
  .ok-ambient.ambient::before {
    background: radial-gradient(circle, var(--green-glow) 0%, transparent 70%);
    opacity: 0.4;
  }
  @keyframes ambPulse {
    0%,
    100% {
      opacity: 0.4;
    }
    50% {
      opacity: 0.7;
    }
  }

  /* === General Layout === */
  .layout {
    display: flex;
    min-height: 100vh;
    background: var(--bg);
    color: var(--text-1);
    position: relative;
    z-index: 1;
  }

  /* === Sidebar (desktop) === */
  .sidebar {
    width: var(--sidebar-width);
    background: var(--glass);
    backdrop-filter: blur(24px);
    -webkit-backdrop-filter: blur(24px);
    border-right: 1px solid var(--border);
    display: flex;
    flex-direction: column;
    position: fixed;
    top: 0;
    left: 0;
    bottom: 0;
    z-index: 20;
    box-shadow: 4px 0 24px rgba(0, 0, 0, 0.2);
  }

  .sidebar-header {
    padding: 24px 20px;
    display: flex;
    align-items: center;
    gap: 12px;
    border-bottom: 0.5px solid var(--border);
  }

  .logo {
    font-size: 20px;
  }

  .logo-text {
    font-size: 18px;
    font-weight: 700;
    letter-spacing: -0.5px;
    background: linear-gradient(135deg, var(--text-1), var(--text-2));
    -webkit-background-clip: text;
    -webkit-text-fill-color: transparent;
    background-clip: text;
  }

  .sidebar-nav {
    flex: 1;
    padding: 20px 12px;
    display: flex;
    flex-direction: column;
    gap: 6px;
    overflow-y: auto;
  }

  .nav-item {
    display: flex;
    align-items: center;
    gap: 12px;
    padding: 12px 16px;
    border: none;
    background: transparent;
    color: var(--text-2);
    font-size: 15px;
    font-weight: 500;
    border-radius: var(--radius-md);
    cursor: pointer;
    transition: all var(--transition-fast);
    text-align: left;
    width: 100%;
    position: relative;
    overflow: hidden;
  }

  .nav-item:hover {
    background: var(--surface-hover);
    color: var(--text-1);
  }

  .nav-item.active {
    background: linear-gradient(90deg, var(--accent-dim), transparent);
    color: var(--accent-bright);
    font-weight: 600;
  }

  .nav-item.active::before {
    content: "";
    position: absolute;
    left: 0;
    top: 50%;
    transform: translateY(-50%);
    width: 3px;
    height: 18px;
    background: var(--accent);
    border-radius: 0 4px 4px 0;
    box-shadow: 0 0 10px var(--accent-glow-s);
  }

  .nav-label {
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
  }

  .sidebar-footer {
    padding: 16px 20px;
    border-top: 0.5px solid var(--border);
  }

  .ws-status {
    display: flex;
    align-items: center;
    gap: 10px;
    font-size: 13px;
    font-weight: 500;
    color: var(--text-4);
  }

  .ws-dot {
    width: 8px;
    height: 8px;
    border-radius: 50%;
    background: var(--red);
    box-shadow: 0 0 6px var(--red-glow);
    display: inline-block;
  }

  .ws-status.connected .ws-dot,
  .ws-badge.connected .ws-dot {
    background: var(--green);
    box-shadow: 0 0 8px var(--green-glow);
  }

  /* === Alarm banner === */
  .alarm-banner {
    position: relative;
    z-index: 10;
    background: linear-gradient(
      90deg,
      rgba(239, 68, 68, 0.15),
      rgba(239, 68, 68, 0.08)
    );
    border-bottom: 1px solid rgba(239, 68, 68, 0.2);
    padding: 11px 20px;
    display: flex;
    align-items: center;
    justify-content: center;
    gap: 10px;
    backdrop-filter: blur(10px);
    font-size: 12px;
    font-weight: 600;
    color: var(--red);
    letter-spacing: 1px;
    text-transform: uppercase;
    cursor: pointer;
    animation: abBlink 1.5s ease-in-out infinite;
  }

  @keyframes abBlink {
    0%,
    100% {
      opacity: 1;
    }
    50% {
      opacity: 0.7;
    }
  }

  /* === Connection overlay === */
  .conn-overlay {
    position: fixed;
    inset: 0;
    background: rgba(0, 0, 0, 0.5);
    display: flex;
    align-items: center;
    justify-content: center;
    z-index: 9000;
  }

  .conn-dialog {
    background: var(--card);
    border-radius: var(--radius-2xl);
    padding: var(--sp-8) var(--sp-6);
    text-align: center;
    box-shadow: var(--shadow-lg);
    max-width: 320px;
    width: 90%;
  }

  .conn-spinner {
    width: 32px;
    height: 32px;
    border: 3px solid var(--border);
    border-top-color: var(--accent);
    border-radius: var(--radius-full);
    animation: conn-spin 0.8s linear infinite;
    margin: 0 auto var(--sp-4);
  }

  @keyframes conn-spin {
    to {
      transform: rotate(360deg);
    }
  }

  .conn-text {
    font-size: var(--text-md);
    color: var(--fg-muted);
    margin-bottom: var(--sp-4);
  }

  .conn-retry {
    background: var(--accent);
    color: #fff;
    border: none;
    border-radius: var(--radius-lg);
    padding: var(--sp-2-5) var(--sp-6);
    font-size: var(--text-md);
    font-weight: var(--fw-semibold);
    cursor: pointer;
    min-height: var(--touch-min);
    transition: opacity var(--transition-fast);
  }

  .conn-retry:hover {
    opacity: 0.9;
  }

  /* === Main area === */
  .main-area {
    flex: 1;
    margin-left: var(--sidebar-width);
    display: flex;
    flex-direction: column;
    min-height: 100vh;
    position: relative;
    z-index: 2;
  }

  .topbar {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 0 24px;
    height: var(--header-height);
    background: var(--glass);
    backdrop-filter: blur(24px);
    -webkit-backdrop-filter: blur(24px);
    border-bottom: 1px solid var(--border);
    position: sticky;
    top: 0;
    z-index: 10;
  }

  .topbar-title {
    font-size: 18px;
    font-weight: 600;
    color: var(--text-1);
    letter-spacing: -0.3px;
  }

  .topbar-right {
    display: flex;
    align-items: center;
    gap: 12px;
    color: var(--text-3);
  }

  .theme-tog,
  .topbar-btn {
    background: var(--surface-2);
    border: 1px solid var(--border);
    color: var(--text-2);
    width: 32px;
    height: 32px;
    border-radius: var(--radius-full);
    cursor: pointer;
    display: flex;
    align-items: center;
    justify-content: center;
    transition: all var(--transition-fast);
  }

  .topbar-btn {
    width: auto;
    padding: 0 12px;
    border-radius: var(--radius-pill);
    font-size: 12px;
    font-weight: 600;
    font-family: inherit;
  }

  .theme-tog:hover,
  .topbar-btn:hover {
    background: var(--surface-3);
    color: var(--text-1);
    border-color: var(--border-accent);
  }

  .ws-badge {
    display: none;
  }

  .content {
    flex: 1;
    padding: 24px 32px;
    max-width: var(--content-max);
    width: 100%;
    margin: 0 auto;
    display: flex;
    flex-direction: column;
    gap: 16px;
  }

  /* === Bottom tabs Concept Layout (mobile) === */
  .bottom-tabs {
    display: none;
    position: fixed;
    bottom: 0;
    left: 50%;
    transform: translateX(-50%);
    width: 100%;
    z-index: 100;
  }

  .tabbar-bg {
    background: var(--glass-heavy);
    backdrop-filter: blur(24px);
    -webkit-backdrop-filter: blur(24px);
    border-top: 1px solid var(--border);
    display: flex;
    justify-content: space-around;
    align-items: flex-start;
    padding: 8px 4px 28px;
    padding-bottom: calc(28px + env(safe-area-inset-bottom, 0));
  }

  .tb {
    background: none;
    border: none;
    cursor: pointer;
    display: flex;
    flex-direction: column;
    align-items: center;
    gap: 3px;
    padding: 4px 10px;
    position: relative;
    min-width: 52px;
    transition: transform 0.1s;
    font-family: inherit;
  }

  .tb:active {
    transform: scale(0.9);
  }

  .tb .ti {
    width: 24px;
    height: 24px;
    display: flex;
    align-items: center;
    justify-content: center;
    color: var(--text-4);
    transition: color 0.15s;
  }

  .tb .tl {
    font-size: 10px;
    font-weight: 500;
    transition: color 0.15s;
    color: var(--text-4);
  }

  .tb.active .ti {
    color: var(--blue);
    filter: drop-shadow(0 0 6px var(--blue-glow-s));
  }

  .tb.active .tl {
    color: var(--blue);
  }

  .tb:not(.active) .ti {
    color: var(--text-4);
  }

  .tb.active::before {
    content: "";
    position: absolute;
    top: -8px;
    left: 50%;
    transform: translateX(-50%);
    width: 20px;
    height: 2px;
    border-radius: 1px;
    background: var(--blue);
    box-shadow: 0 0 8px var(--blue-glow-s);
  }

  /* === "More" overlay === */
  .more-overlay {
    position: fixed;
    inset: 0;
    background: rgba(0, 0, 0, 0.6);
    backdrop-filter: blur(4px);
    z-index: 120;
    display: flex;
    align-items: flex-end;
    justify-content: center;
  }

  .more-sheet {
    background: var(--surface);
    border-radius: 24px 24px 0 0;
    width: 100%;
    max-width: 480px;
    padding: 12px 16px;
    padding-bottom: calc(40px + env(safe-area-inset-bottom, 0));
    border: 1px solid var(--border);
    border-bottom: none;
  }

  .more-header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 12px 16px 20px;
  }

  .more-title {
    font-size: 16px;
    font-weight: 600;
    color: var(--text-2);
  }

  .more-close {
    background: var(--surface-2);
    border: none;
    color: var(--text-3);
    cursor: pointer;
    width: 32px;
    height: 32px;
    display: flex;
    align-items: center;
    justify-content: center;
    border-radius: var(--radius-full);
    transition: all 0.15s;
  }

  .more-close:hover {
    color: var(--text-1);
    background: var(--surface-3);
  }

  .more-item {
    display: flex;
    align-items: center;
    gap: 14px;
    padding: 16px 20px;
    width: 100%;
    border: none;
    background: var(--surface-2);
    color: var(--text-2);
    font-size: 15px;
    font-weight: 500;
    cursor: pointer;
    border-radius: var(--radius-sm);
    margin-bottom: 8px;
    transition: all 0.15s;
    font-family: inherit;
  }

  .more-item:hover {
    background: var(--surface-3);
    color: var(--text-1);
  }

  .more-item.active {
    color: var(--blue);
    background: var(--blue-dim);
    box-shadow: inset 2px 0 0 var(--blue);
  }

  /* === Responsive === */
  @media (max-width: 768px) {
    .sidebar {
      display: none;
    }
    .main-area {
      margin-left: 0;
    }
    .bottom-tabs {
      display: block;
    }
    .ws-badge {
      display: block;
    }
    .content {
      padding: 16px;
      padding-bottom: calc(110px + env(safe-area-inset-bottom, 0));
    }
    .topbar {
      padding: 14px 20px;
    }
    .topbar-title {
      font-size: 16px;
    }
  }

  @media (min-width: 769px) and (max-width: 1024px) {
    .sidebar {
      display: none;
    }
    .main-area {
      margin-left: 0;
    }
    .bottom-tabs {
      display: block;
    }
    .ws-badge {
      display: block;
    }
    .content {
      padding: 20px 24px;
      padding-bottom: calc(110px + env(safe-area-inset-bottom, 0));
    }
  }
</style>
