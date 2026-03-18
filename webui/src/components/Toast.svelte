<script>
  import { fly, fade } from 'svelte/transition';
  import { toasts, dismissToast } from '../stores/toast.js';
</script>

{#if $toasts.length}
  <div class="toast-container">
    {#each $toasts as toast (toast.id)}
      <div class="toast toast-{toast.type}"
           in:fly={{ y: 30, duration: 200 }}
           out:fade={{ duration: 150 }}>
        <span class="toast-icon">
          {#if toast.type === 'success'}&#10003;{:else if toast.type === 'error'}&#10007;{:else}&#9888;{/if}
        </span>
        <span class="toast-msg">{toast.msg}</span>
        <button class="toast-close" on:click|stopPropagation={() => dismissToast(toast.id)} aria-label="Close">
          &times;
        </button>
      </div>
    {/each}
  </div>
{/if}

<style>
  .toast-container {
    position: fixed;
    bottom: var(--sp-4);
    left: 50%;
    transform: translateX(-50%);
    z-index: 10000;
    display: flex;
    flex-direction: column;
    gap: var(--sp-2);
    max-width: var(--toast-width);
    width: calc(100% - var(--sp-8));
    pointer-events: none;
  }

  /* На мобільному — вище bottom tabs */
  @media (max-width: 768px) {
    .toast-container {
      bottom: 72px;
    }
  }

  .toast {
    display: flex;
    align-items: center;
    gap: var(--sp-2);
    padding: var(--sp-2-5) var(--sp-4);
    border-radius: var(--radius-lg);
    font-size: var(--text-base);
    color: #fff;
    box-shadow: var(--shadow-toast);
    pointer-events: auto;
  }

  .toast-success { background: var(--success, #22c55e); }
  .toast-error { background: var(--error, #ef4444); }
  .toast-warn { background: var(--warning, #f59e0b); color: #000; }

  .toast-icon {
    font-size: var(--text-lg);
    flex-shrink: 0;
  }

  .toast-msg {
    flex: 1;
    line-height: 1.3;
  }

  .toast-close {
    background: none;
    border: none;
    color: inherit;
    font-size: var(--text-xl);
    cursor: pointer;
    padding: 0 var(--sp-1);
    opacity: 0.7;
    line-height: 1;
    flex-shrink: 0;
  }

  .toast-close:hover {
    opacity: 1;
  }

  .toast-warn .toast-close {
    color: #000;
  }
</style>
