<script>
  import { fly } from "svelte/transition";
  import { pages } from "../stores/ui.js";
  import { state } from "../stores/state.js";
  import { t } from "../stores/i18n.js";
  import { isVisible } from "../lib/visibility.js";
  import GroupAccordion from "../components/GroupAccordion.svelte";
  import WidgetRenderer from "../components/WidgetRenderer.svelte";

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

</style>
