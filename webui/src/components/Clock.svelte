<script>
  import { onMount, onDestroy } from 'svelte';
  import { state } from '../stores/state.js';
  import Icon from './Icon.svelte';

  let clockTime = '';
  let clockDate = '';
  let tickTimer = null;
  let serverSeconds = 0;

  function parseHMS(s) {
    if (!s || s === '--:--:--') return -1;
    const p = s.split(':');
    return (+p[0]) * 3600 + (+p[1]) * 60 + (+p[2] || 0);
  }

  function fmtHMS(sec) {
    const s = ((sec % 86400) + 86400) % 86400;
    const h = String(Math.floor(s / 3600)).padStart(2, '0');
    const m = String(Math.floor((s % 3600) / 60)).padStart(2, '0');
    const ss = String(s % 60).padStart(2, '0');
    return `${h}:${m}:${ss}`;
  }

  // Оновлювати лише коли server time СПРАВДІ змінився (не кожен WS broadcast)
  let prevServerTime = '';
  $: {
    const st = $state['system.time'];
    const sd = $state['system.date'];
    if (st && st !== '--:--:--' && st !== prevServerTime) {
      prevServerTime = st;
      serverSeconds = parseHMS(st);
      clockTime = st;
    }
    if (sd && sd !== '--.--.----') clockDate = sd;
  }

  onMount(() => {
    tickTimer = setInterval(() => {
      if (serverSeconds >= 0) {
        serverSeconds = (serverSeconds + 1) % 86400;
        clockTime = fmtHMS(serverSeconds);
      }
    }, 1000);
  });

  onDestroy(() => {
    if (tickTimer) clearInterval(tickTimer);
  });
</script>

{#if clockTime}
  <div class="topbar-clock">
    <Icon name="clock" size={14} />
    <span class="clock-time">{clockTime}</span>
    <span class="clock-date">{clockDate}</span>
  </div>
{/if}

<style>
  .topbar-clock {
    display: flex;
    align-items: center;
    gap: 6px;
    color: var(--fg-muted);
    font-size: 13px;
    font-variant-numeric: tabular-nums;
  }
  .clock-time {
    font-weight: 500;
    color: var(--fg);
  }
  .clock-date {
    opacity: 0.7;
  }
</style>
