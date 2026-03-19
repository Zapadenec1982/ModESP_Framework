<script>
  import { fly } from "svelte/transition";
  import { onDestroy } from "svelte";
  import { pages } from "../stores/ui.js";
  import { state } from "../stores/state.js";
  import { t } from "../stores/i18n.js";
  import { isVisible } from "../lib/visibility.js";
  import GroupAccordion from "../components/GroupAccordion.svelte";
  import WidgetRenderer from "../components/WidgetRenderer.svelte";
  import { apiPost } from "../lib/api.js";

  export let pageId;

  // Responsive: desktop = no accordions, mobile = all collapsed
  const isMobile = typeof window !== 'undefined' && window.matchMedia("(max-width: 767px)").matches;

  $: page = $pages.find((p) => p.id === pageId);

  // Адаптивний розмір карток: короткі парами, довгі на повну ширину
  const FULL_WIDTH_THRESHOLD = 7;

  $: filteredCards = page ? page.cards
    .map((card, origIdx) => ({
      card,
      origIdx,
      visible: isVisible(card.visible_when, $state),
      widgetCount: card.widgets.filter(w => isVisible(w.visible_when, $state)).length,
    }))
    .filter(c => c.visible)
    .sort((a, b) => {
      const aFull = !a.card.wide && a.widgetCount > FULL_WIDTH_THRESHOLD ? 1 : 0;
      const bFull = !b.card.wide && b.widgetCount > FULL_WIDTH_THRESHOLD ? 1 : 0;
      return aFull - bFull;
    })
    : [];

  // Hold-to-confirm reset
  let holdTimer = null;
  let holdProgress = 0;
  let holdActive = false;
  let holdInterval = null;

  function startHold() {
    holdActive = true;
    holdProgress = 0;
    holdInterval = setInterval(() => {
      holdProgress += 2;
      if (holdProgress >= 100) {
        stopHold();
        doReset();
      }
    }, 60); // ~3 seconds total (50 steps × 60ms)
  }

  function stopHold() {
    holdActive = false;
    holdProgress = 0;
    if (holdInterval) { clearInterval(holdInterval); holdInterval = null; }
  }

  async function doReset() {
    try {
      await apiPost("/api/settings", { "protection.reset_alarms": true });
    } catch (e) {
      console.error(e);
    }
  }

  onDestroy(() => {
    if (holdInterval) clearInterval(holdInterval);
  });
</script>

{#if page}
  <div class="page-grid page-padding">
    {#each filteredCards as { card, origIdx, widgetCount }, i}
      {@const isReadonly = card.widgets.every(w => !w.writable)}
      {@const isFullWidth = card.wide || widgetCount > FULL_WIDTH_THRESHOLD}
      <div class:card-full={isFullWidth} in:fly={{ y: 15, duration: 250, delay: i * 50 }}>
        <GroupAccordion
          title={card.title}
          icon={card.icon || ""}
          iconColor={card.icon_color || ""}
          subtitle={card.subtitle || ""}
          summaryKeys={card.summary_keys || []}
          collapsible={!isMobile ? false : (card.collapsible || false)}
          defaultOpen={!isMobile}
        >
          {#each card.widgets as widget}
            {#if isVisible(widget.visible_when, $state)}
              <WidgetRenderer {widget} value={$state[widget.key]} />
            {/if}
          {/each}
        </GroupAccordion>
      </div>
    {/each}
  </div>
{:else}
  <div class="not-found">{$t["page.not_found"]}</div>
{/if}

<style>
  .page-grid {
    max-width: 640px;
    margin: 0 auto;
    width: 100%;
  }

  @media (min-width: 1025px) {
    .page-grid {
      column-count: 2;
      column-gap: 16px;
      max-width: 1100px;
    }

    /* Картки не розриваються між колонками */
    .page-grid > :global(*) {
      break-inside: avoid;
    }

    /* Hero/діагностика/аларми — на повну ширину */
    .protect-status,
    .alarm-detail,
    .diag-card,
    .reset-wrap {
      column-span: all;
    }

    /* Довгі картки (>7 виджетів) — на повну ширину */
    .card-full {
      column-span: all;
    }

    /* Виджети всередині full-width карток — у 2 колонки */
    .card-full :global(.grp-body) {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 0 24px;
    }
  }

  .page-padding {
    padding-bottom: 32px;
  }

  .not-found {
    text-align: center;
    color: var(--text-4);
    padding: 60px 20px;
    font-size: 16px;
    font-weight: 500;
  }

  /* === Protection Status Component === */
  .protect-status {
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: var(--radius-2xl);
    padding: 24px;
    text-align: center;
    margin-bottom: 24px;
    display: flex;
    flex-direction: column;
    align-items: center;
  }

  .protect-ok {
    border-color: rgba(34, 197, 94, 0.3);
    box-shadow: 0 0 20px rgba(34, 197, 94, 0.05);
  }

  .protect-ok .p-icon {
    color: var(--green);
    background: var(--green-dim);
    box-shadow: 0 0 15px var(--green-glow);
  }

  .protect-alarm {
    border-color: var(--danger-border);
    background: var(--danger-dim);
    box-shadow: 0 0 20px var(--danger-glow);
  }

  .protect-alarm .p-icon {
    color: var(--danger);
    background: rgba(239, 68, 68, 0.2);
    box-shadow: 0 0 15px var(--danger-glow);
  }

  .p-icon {
    width: 64px;
    height: 64px;
    border-radius: 50%;
    display: flex;
    align-items: center;
    justify-content: center;
    margin: 0 auto 16px;
    font-size: 28px;
  }

  .p-title {
    font-size: 18px;
    font-weight: 700;
    color: var(--text-1);
    margin-bottom: 4px;
  }

  .protect-alarm .p-title {
    color: var(--danger);
  }

  .p-desc {
    font-size: 12px;
    color: var(--text-3);
    max-width: 280px;
    margin: 0 auto;
    line-height: 1.4;
  }

  .check-chips {
    display: flex;
    gap: 6px;
    justify-content: center;
    flex-wrap: wrap;
    margin-top: 16px;
  }
  .check-chip {
    display: inline-flex;
    align-items: center;
    gap: 5px;
    padding: 5px 12px;
    border-radius: 20px;
    background: var(--ok-dim);
    border: 1px solid var(--ok-border);
    font-size: 11px;
    font-weight: 500;
    color: var(--ok);
  }
  .check-chip-dot {
    width: 5px;
    height: 5px;
    border-radius: 50%;
    background: var(--ok);
  }

  .al-code {
    display: inline-block;
    margin-top: 16px;
    padding: 6px 16px;
    background: rgba(239, 68, 68, 0.15);
    color: var(--danger);
    border: 1px solid rgba(239, 68, 68, 0.3);
    border-radius: var(--radius-sm);
    font-weight: 700;
    font-size: 18px;
    letter-spacing: 1px;
    font-family: var(--font-mono);
  }

  /* Alarm Detail Cards */
  .alarm-detail {
    background: var(--danger-dim);
    border: 1px solid var(--danger-border);
    border-radius: var(--radius);
    padding: 16px;
  }
  .alarm-detail-header {
    display: flex;
    align-items: center;
    gap: 10px;
    color: var(--danger);
    margin-bottom: 8px;
  }
  .alarm-detail-name {
    font-size: 14px;
    font-weight: 600;
  }
  .alarm-detail-grid {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 12px;
    margin-top: 12px;
    padding-top: 12px;
    border-top: 1px solid var(--danger-border);
  }
  .alarm-detail-l {
    font-size: 10px;
    font-weight: 600;
    color: var(--text-3);
    text-transform: uppercase;
    letter-spacing: 0.8px;
    margin-bottom: 4px;
  }
  .alarm-detail-v {
    font-family: var(--font-mono);
    font-size: 20px;
    font-weight: 600;
    color: var(--text-1);
    letter-spacing: -0.5px;
  }
  .alarm-detail-v small {
    font-size: 11px;
    font-weight: 400;
    color: var(--text-3);
  }

  /* Hold-to-confirm Reset Button */
  .reset-wrap {
    display: flex;
    justify-content: center;
  }
  .al-btn {
    position: relative;
    display: flex;
    align-items: center;
    justify-content: center;
    width: 100%;
    max-width: 300px;
    padding: 14px;
    background: var(--danger);
    border: none;
    border-radius: var(--radius-sm);
    font-family: inherit;
    font-size: 14px;
    font-weight: 600;
    color: white;
    cursor: pointer;
    box-shadow: 0 4px 20px var(--danger-glow);
    transition: all 0.15s;
    letter-spacing: 0.02em;
    overflow: hidden;
    touch-action: manipulation;
    -webkit-tap-highlight-color: transparent;
    user-select: none;
  }
  .hold-fill {
    position: absolute;
    top: 0;
    left: 0;
    height: 100%;
    background: rgba(255, 255, 255, 0.2);
    transition: width 0.06s linear;
    pointer-events: none;
  }
  .hold-text {
    position: relative;
    z-index: 1;
  }

  .al-btn:hover {
    box-shadow: 0 4px 24px var(--danger-glow);
    filter: brightness(1.05);
  }

  /* Diagnostics block */
  .diag-card {
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    padding: 24px;
    margin-bottom: var(--sp-4);
  }
  .diag-header {
    display: flex;
    align-items: center;
    gap: 12px;
    margin-bottom: 20px;
  }
  .diag-icon {
    width: 34px;
    height: 34px;
    border-radius: 9px;
    background: var(--accent-dim);
    border: 1px solid var(--border-glow);
    display: flex;
    align-items: center;
    justify-content: center;
    font-size: 14px;
    color: var(--accent);
  }
  .diag-title {
    font-size: 14px;
    font-weight: 600;
    color: var(--text-1);
  }
  .diag-grid {
    display: grid;
    grid-template-columns: 1fr 1fr 1fr;
    gap: 16px;
  }
  .diag-metric-label {
    font-family: var(--font-mono);
    font-size: 9px;
    font-weight: 600;
    color: var(--text-4);
    text-transform: uppercase;
    letter-spacing: 1px;
    margin-bottom: 6px;
  }
  .diag-metric-value {
    font-family: var(--font-mono);
    font-size: 22px;
    font-weight: 600;
    color: var(--text-1);
    letter-spacing: -0.5px;
  }
  .diag-metric-value small {
    font-size: 11px;
    font-weight: 400;
    color: var(--text-3);
  }

  .duty-wrap {
    grid-column: 1 / -1;
    margin-top: 8px;
  }
  .duty-labels {
    display: flex;
    justify-content: space-between;
    margin-bottom: 8px;
  }
  .duty-labels span:first-child {
    font-family: var(--font-mono);
    font-size: 10px;
    font-weight: 600;
    color: var(--text-3);
    text-transform: uppercase;
    letter-spacing: 0.8px;
  }
  .duty-labels span:last-child {
    font-family: var(--font-mono);
    font-size: 14px;
    font-weight: 600;
    color: var(--text-1);
  }
  .duty-bar {
    width: 100%;
    height: 5px;
    background: var(--surface-3);
    border-radius: 3px;
    overflow: hidden;
  }
  .duty-fill {
    height: 100%;
    border-radius: 3px;
    background: linear-gradient(90deg, var(--accent), var(--info));
    box-shadow: 0 0 10px var(--accent-glow);
  }
</style>
