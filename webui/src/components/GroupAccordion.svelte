<script>
  import { slide } from "svelte/transition";
  import { state } from "../stores/state.js";
  import Icon from "./Icon.svelte";

  export let title = "";
  export let icon = "";
  export let iconColor = "";
  export let subtitle = "";
  /** @type {string[]} */
  export let summaryKeys = [];
  export let collapsible = false;
  export let defaultOpen = true;

  const storageKey = title ? `grp-${title}` : null;
  const reducedMotion =
    typeof window !== "undefined" &&
    window.matchMedia("(prefers-reduced-motion: reduce)").matches;

  function getInitial() {
    if (!collapsible) return true;
    if (storageKey) {
      try {
        const saved = sessionStorage.getItem(storageKey);
        if (saved !== null) return saved === "1";
      } catch (e) {}
    }
    return defaultOpen;
  }

  let open = getInitial();
  let wasEverOpen = open;

  function toggle() {
    if (!collapsible) return;
    open = !open;
    if (open) wasEverOpen = true;
    if (storageKey) {
      try {
        sessionStorage.setItem(storageKey, open ? "1" : "0");
      } catch (e) {}
    }
  }

  // Unified icon color — всі іконки одного акцентного кольору
  $: color = "var(--accent)";
  $: dimColor = "var(--accent-dim)";

  // Format summary value for display
  function fmtVal(v) {
    if (v === undefined || v === null) return null;
    if (typeof v === "boolean") return v ? "ON" : "OFF";
    if (typeof v === "number") {
      if (Number.isInteger(v)) return String(v);
      return v.toFixed(1);
    }
    return String(v);
  }
</script>

<div class="group" class:open>
  <div
    class="grp-h"
    class:collapsible
    on:click={toggle}
    on:keydown={(e) => e.key === "Enter" && toggle()}
    role={collapsible ? "button" : null}
    tabindex={collapsible ? 0 : -1}
  >
    {#if icon}
      <div class="grp-icon" style="color:{color};background:{dimColor}">
        <Icon name={icon} size={16} />
      </div>
    {/if}
    <div class="grp-text">
      <div class="grp-title">{title}</div>
      {#if subtitle}
        <div class="grp-sub">{subtitle}</div>
      {/if}
    </div>
    {#if collapsible}
      <span class="grp-chev" class:open>
        <Icon name="chevron-right" size={14} />
      </span>
    {/if}
  </div>

  {#if collapsible && !open && summaryKeys.length > 0}
    <div class="sum-strip">
      {#each summaryKeys as sk}
        {@const v = fmtVal($state[sk])}
        {#if v !== null}
          <span class="sum-chip">{sk.split(".").pop()}: {v}</span>
        {/if}
      {/each}
    </div>
  {/if}

  {#if !collapsible}
    <div class="grp-body">
      <slot />
    </div>
  {:else if open}
    <div
      class="grp-body"
      transition:slide={{ duration: reducedMotion ? 0 : 200 }}
    >
      {#if wasEverOpen}
        <slot />
      {/if}
    </div>
  {/if}
</div>

<style>
  .group {
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    margin-bottom: var(--sp-4);
    overflow: hidden;
    transition: border-color var(--transition-fast);
  }

  .group:hover {
    border-color: var(--border-accent);
  }

  .grp-h {
    display: flex;
    align-items: center;
    gap: 12px;
    padding: 14px 16px;
  }

  .grp-h.collapsible {
    cursor: pointer;
    user-select: none;
    transition: background var(--transition-fast);
  }

  .grp-h.collapsible:hover {
    background: var(--surface-2);
  }

  .grp-icon {
    width: 34px;
    height: 34px;
    min-width: 34px;
    border-radius: 9px;
    display: flex;
    align-items: center;
    justify-content: center;
  }

  .grp-text {
    flex: 1;
    min-width: 0;
  }

  .grp-title {
    font-size: 14px;
    font-weight: 600;
    color: var(--text-1);
    line-height: 1.2;
  }

  .grp-sub {
    font-size: 11px;
    color: var(--text-3);
    margin-top: 2px;
  }

  .grp-chev {
    display: inline-flex;
    color: var(--text-4);
    transition: transform var(--transition-normal);
    margin-left: auto;
  }

  .grp-chev.open {
    transform: rotate(90deg);
  }

  .sum-strip {
    display: flex;
    gap: 6px;
    padding: 0 16px 12px;
    flex-wrap: wrap;
  }

  .sum-chip {
    display: inline-block;
    padding: 3px 10px;
    border-radius: var(--radius-pill);
    background: var(--surface-2);
    border: 1px solid var(--border);
    font-size: 10px;
    font-weight: 500;
    color: var(--text-2);
    font-variant-numeric: tabular-nums;
  }

  .grp-body {
    padding: 4px 16px 16px;
  }
</style>
