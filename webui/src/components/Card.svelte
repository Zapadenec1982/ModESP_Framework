<script>
  import { slide } from "svelte/transition";
  import Icon from "./Icon.svelte";

  export let title = "";
  export let collapsible = false;
  /** @type {'default' | 'status' | 'alarm'} */
  export let variant = "default";

  // sessionStorage ключ для збереження collapsed стану
  const storageKey = title ? `card-collapsed-${title}` : null;

  // Ініціалізація: sessionStorage > mobile default
  const isMobile =
    typeof window !== "undefined" &&
    window.matchMedia("(max-width: 767px)").matches;

  function getInitialCollapsed() {
    if (!collapsible) return false;
    if (storageKey) {
      try {
        const saved = sessionStorage.getItem(storageKey);
        if (saved !== null) return saved === "1";
      } catch (e) {}
    }
    return isMobile;
  }

  let collapsed = getInitialCollapsed();

  function toggle() {
    if (!collapsible) return;
    collapsed = !collapsed;
    if (storageKey) {
      try {
        sessionStorage.setItem(storageKey, collapsed ? "1" : "0");
      } catch (e) {}
    }
  }
</script>

<div
  class="card"
  class:card-status={variant === "status"}
  class:card-alarm={variant === "alarm"}
>
  {#if title}
    <div
      class="card-title"
      class:collapsible
      on:click={toggle}
      on:keydown={(e) => e.key === "Enter" && toggle()}
      role={collapsible ? "button" : null}
      tabindex={collapsible ? 0 : -1}
    >
      {#if collapsible}
        <span class="arrow" class:open={!collapsed}>
          <Icon name="chevron-right" size={14} />
        </span>
      {/if}
      {title}
    </div>
  {/if}
  {#if !collapsed}
    <div class="card-body" transition:slide={{ duration: 200 }}>
      <slot />
    </div>
  {/if}
</div>

<style>
  .card {
    background: var(--surface);
    border-radius: var(--radius);
    border: 1px solid var(--border);
    margin-bottom: var(--sp-4);
    overflow: hidden;
    transition: all var(--transition-slow);
  }

  .card:hover {
    border-color: var(--border-accent);
    box-shadow: 0 4px 20px rgba(0, 0, 0, 0.2);
  }

  /* Variant: status — subtle accent bg */
  .card-status {
    border-color: var(--blue-glow-s);
    box-shadow: 0 0 10px var(--blue-glow);
  }

  /* Variant: alarm — red border + subtle red bg */
  .card-alarm {
    border-color: var(--danger-border);
    background: var(--danger-dim);
  }

  .card-title {
    font-size: 14px;
    font-weight: 500;
    color: var(--text-2);
    letter-spacing: 0.5px;
    padding: 16px 20px;
    border-bottom: 1px solid var(--border);
    display: flex;
    align-items: center;
    gap: 8px;
    background: var(--surface-2);
  }

  .card-alarm .card-title {
    color: var(--danger);
    border-bottom-color: var(--danger-border);
    background: var(--danger-dim);
  }

  .card-title.collapsible {
    cursor: pointer;
    user-select: none;
    transition: background 0.2s;
  }

  .card-title.collapsible:hover {
    color: var(--text-1);
    background: var(--surface-3);
  }

  .card-alarm .card-title.collapsible:hover {
    color: var(--danger);
  }

  .arrow {
    display: inline-flex;
    transition: transform var(--transition-normal);
    color: var(--text-3);
  }

  .arrow.open {
    transform: rotate(90deg);
  }

  .card-body {
    padding: 16px 20px;
  }
</style>
