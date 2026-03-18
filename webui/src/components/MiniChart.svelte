<script>
  import { onMount, onDestroy } from 'svelte';
  import { state } from '../stores/state.js';
  import { navigateTo } from '../stores/ui.js';
  import { t } from '../stores/i18n.js';
  import { apiGet } from '../lib/api.js';
  import { downsampleAvg } from '../lib/downsample.js';
  import { buildSegments, tempRange, computeTimeLabels, computeTempLabels } from '../lib/chart.js';

  const W = 654, H = 240;
  const PAD = { top: 14, right: 14, bottom: 36, left: 56 };
  const CW = W - PAD.left - PAD.right;
  const CH = H - PAD.top - PAD.bottom;

  let chartData = null;
  let refreshTimer;
  let lastLiveTs = 0;
  const LIVE_THROTTLE = 10;
  let svgEl;

  // Tooltip
  let tooltip = null;

  async function loadChart() {
    try { chartData = await apiGet('/api/log?hours=24'); } catch {}
  }

  onMount(() => { loadChart(); refreshTimer = setInterval(loadChart, 300000); });
  onDestroy(() => { if (refreshTimer) clearInterval(refreshTimer); });

  // Reactive live update: додаємо точку при зміні air_temp (throttle 10с)
  $: liveAir = $state['equipment.air_temp'];
  $: if (chartData?.temp && typeof liveAir === 'number') {
    const now = Math.floor(Date.now() / 1000);
    if (now - lastLiveTs >= LIVE_THROTTLE) {
      lastLiveTs = now;
      const aIdx = chartData.channels.indexOf('air');
      if (aIdx >= 0) {
        const point = new Array(chartData.channels.length + 1).fill(null);
        point[0] = now;
        point[aIdx + 1] = Math.round(liveAir * 10);
        const cutoff = now - 24 * 3600;
        while (chartData.temp.length > 0 && chartData.temp[0][0] < cutoff) chartData.temp.shift();
        chartData.temp.push(point);
        chartData = { ...chartData, temp: chartData.temp.slice() };
      }
    }
  }

  $: airIdx = chartData?.channels ? chartData.channels.indexOf('air') + 1 : 1;
  $: pts = chartData?.temp || [];
  $: sampled = downsampleAvg(pts, 300, airIdx);

  $: tMin = pts.length > 0 ? pts[0][0] : 0;
  $: tMax = pts.length > 0 ? pts[pts.length - 1][0] : 1;
  $: [vMin, vMax] = tempRange(pts, [airIdx]);

  $: xFn = (ts) => tMax === tMin ? CW / 2 : ((ts - tMin) / (tMax - tMin)) * CW;
  $: yFn = (v) => vMax === vMin ? CH / 2 : CH - ((v - vMin) / (vMax - vMin)) * CH;

  $: pathSegs = buildSegments(sampled, airIdx, PAD, xFn, yFn);

  $: setpoint = $state['thermostat.setpoint'];
  $: spY = typeof setpoint === 'number' ? PAD.top + yFn(setpoint) : null;

  $: timeLabels = computeTimeLabels(tMin, tMax, 6, PAD.left, xFn);
  $: yLabels = computeTempLabels(vMin, vMax, 5, PAD.top, yFn);

  // Відносний час останньої точки
  $: lastTs = pts.length > 0 ? pts[pts.length - 1][0] : 0;
  $: agoMin = lastTs > 0 ? Math.max(0, Math.round((Date.now() / 1000 - lastTs) / 60)) : null;

  function handlePointer(e) {
    if (!svgEl || pts.length < 2) return;
    const rect = svgEl.getBoundingClientRect();
    const clientX = e.touches ? e.touches[0].clientX : e.clientX;
    const relX = (clientX - rect.left) / rect.width;
    const svgX = relX * W;
    const chartX = svgX - PAD.left;
    if (chartX < 0 || chartX > CW) { tooltip = null; return; }
    const ratio = chartX / CW;
    const targetTs = tMin + ratio * (tMax - tMin);
    let closest = null, closestDist = Infinity;
    for (const p of sampled) {
      const raw = p[airIdx];
      if (raw == null) continue;
      const dist = Math.abs(p[0] - targetTs);
      if (dist < closestDist) { closestDist = dist; closest = p; }
    }
    if (!closest) { tooltip = null; return; }
    const val = closest[airIdx] / 10;
    const d = new Date(closest[0] * 1000);
    const timeStr = `${String(d.getHours()).padStart(2,'0')}:${String(d.getMinutes()).padStart(2,'0')}`;
    // SVG координати для crosshair/dot
    const svgPtX = PAD.left + xFn(closest[0]);
    const svgPtY = PAD.top + yFn(val);
    // Екранні % для HTML tooltip
    const pctX = (svgPtX / W) * 100;
    const pctY = (svgPtY / H) * 100;
    tooltip = {
      svgX: svgPtX, svgY: svgPtY,
      pctX, pctY,
      temp: val.toFixed(1) + '°C',
      time: timeStr
    };
  }

  function hideTooltip() { tooltip = null; }
</script>

{#if pts.length > 0}
  <div class="tile-chart">
    <svg bind:this={svgEl} viewBox="0 0 {W} {H}" class="mini-chart"
      on:click={handlePointer}
      on:touchstart|preventDefault={handlePointer}
      on:touchmove|preventDefault={handlePointer}
      on:mousemove={handlePointer}
      on:mouseleave={hideTooltip}
    >
      {#each yLabels as yl}
        <line x1={PAD.left} y1={yl.y} x2={W - PAD.right} y2={yl.y} class="mc-grid" />
        <text x={PAD.left - 6} y={yl.y + 5} class="mc-axis" text-anchor="end">{yl.label}</text>
      {/each}
      {#if spY != null}
        <line x1={PAD.left} y1={spY} x2={W - PAD.right} y2={spY} class="mc-setpoint" />
      {/if}
      {#each pathSegs as seg}
        <path d={seg} fill="none" stroke="#3b82f6" stroke-width="2.5" />
      {/each}
      {#each timeLabels as xl}
        <text x={xl.x} y={H - 4} class="mc-axis" text-anchor="middle">{xl.label}</text>
      {/each}
      {#if tooltip}
        <line x1={tooltip.svgX} y1={PAD.top} x2={tooltip.svgX} y2={H - PAD.bottom} class="mc-cross" />
        <circle cx={tooltip.svgX} cy={tooltip.svgY} r="5" class="mc-dot" />
      {/if}
    </svg>
    {#if tooltip}
      <div class="mc-html-tip" style="left:{tooltip.pctX}%; top:{tooltip.pctY}%">
        <div class="mc-html-val">{tooltip.temp}</div>
        <div class="mc-html-time">{tooltip.time}</div>
      </div>
    {/if}
    <div class="chart-footer">
      {#if agoMin !== null}
        <span class="chart-ago">{agoMin} {$t['chart.min_ago']}</span>
      {/if}
      <button class="chart-link" on:click={() => $navigateTo = 'chart'}>
        {$t['chart.detail']} →
      </button>
    </div>
  </div>
{/if}

<style>
  .tile-chart {
    background: var(--card);
    border-radius: var(--radius-2xl);
    border: 1px solid var(--border);
    padding: var(--sp-4);
    position: relative;
  }
  .mini-chart { width: 100%; display: block; cursor: crosshair; }
  .mini-chart .mc-grid { stroke: var(--border); stroke-width: 0.5; }
  .mini-chart .mc-setpoint { stroke: #f59e0b; stroke-width: 1; stroke-dasharray: 4 2; opacity: 0.7; }
  .mini-chart .mc-axis { font-size: 16px; fill: var(--fg-muted); }
  .mini-chart .mc-cross { stroke: var(--fg-muted); stroke-width: 0.5; stroke-dasharray: 2 2; opacity: 0.5; }
  .mini-chart .mc-dot { fill: #3b82f6; stroke: var(--card); stroke-width: 2; }

  .mc-html-tip {
    position: absolute;
    transform: translate(-50%, -120%);
    background: var(--bg2);
    border: 1px solid var(--border);
    border-radius: 6px;
    padding: 4px 10px;
    text-align: center;
    pointer-events: none;
    white-space: nowrap;
    z-index: 5;
  }
  .mc-html-val { font-size: 14px; font-weight: 700; color: var(--fg); }
  .mc-html-time { font-size: 12px; color: var(--fg-muted); }

  .chart-footer {
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-top: var(--sp-2);
    padding: 0 var(--sp-1);
  }
  .chart-ago {
    font-size: var(--text-xs);
    color: var(--fg-muted);
  }
  .chart-link {
    font-size: var(--text-sm);
    color: var(--accent);
    background: none;
    border: none;
    cursor: pointer;
    font-weight: var(--fw-semibold);
    padding: var(--sp-1) var(--sp-2);
    border-radius: var(--radius-sm);
    transition: background var(--transition-fast);
  }
  .chart-link:hover {
    background: var(--accent-bg);
  }

  @media (max-width: 480px) {
    .mini-chart .mc-axis { font-size: 24px; }
    .tile-chart { padding: var(--sp-3); }
  }
</style>
