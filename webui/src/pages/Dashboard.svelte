<script>
  import { state } from "../stores/state.js";
  import { stateMeta } from "../stores/ui.js";
  import { t } from "../stores/i18n.js";
  import SliderWidget from "../components/widgets/SliderWidget.svelte";
  import MiniChart from "../components/MiniChart.svelte";

  $: displayTemp = $state["thermostat.display_temp"];
  $: temperature = $state["thermostat.temperature"];
  $: setpoint = $state["thermostat.setpoint"];
  $: compressor = $state["equipment.compressor"];
  $: evapFan = $state["equipment.evap_fan"];
  $: defrostRelay = $state["equipment.defrost_relay"];
  $: thermoState = $state["thermostat.state"];
  $: defrostActive = $state["defrost.active"];
  $: defrostPhase = $state["defrost.phase"];
  $: defrostType = $state["defrost.type"]; // 0=natural, 1=electric, 2=hot gas
  $: alarmActive = $state["protection.alarm_active"];
  $: alarmCode = $state["protection.alarm_code"];
  $: nightActive = $state["thermostat.night_active"];
  $: condFan = $state["equipment.cond_fan"];
  $: hasEvapTemp = $state["equipment.has_evap_temp"];
  $: hasCondTemp = $state["equipment.has_cond_temp"];
  $: hasCondFan = $state["equipment.has_cond_fan"];
  $: showDefrostSymbol = typeof displayTemp === "number" && displayTemp <= -900;

  $: sp = typeof setpoint === "number" ? setpoint : -18;
  $: shownTemp = showDefrostSymbol
    ? null
    : typeof displayTemp === "number"
      ? displayTemp
      : temperature;
  $: tempColor = getTemperatureColor(shownTemp, sp);
  $: tempGlow = getTemperatureGlow(shownTemp, sp);
  $: spMeta = $stateMeta["thermostat.setpoint"] || {
    min: -35,
    max: 0,
    step: 0.5,
  };

  function getTemperatureColor(temp, sp) {
    if (typeof temp !== "number") return "var(--fg-muted)";
    if (temp < sp - 2) return "#3b82f6";
    if (temp <= sp + 2) return "var(--status-ok)";
    if (temp <= sp + 5) return "#f59e0b";
    return "#ef4444";
  }

  function getTemperatureGlow(temp, sp) {
    if (typeof temp !== "number") return "rgba(100,100,100,0.06)";
    if (temp < sp - 2) return "rgba(59,130,246,0.18)";
    if (temp <= sp + 2) return "rgba(34,197,94,0.15)";
    if (temp <= sp + 5) return "rgba(245,158,11,0.20)";
    return "rgba(239,68,68,0.25)";
  }

  const stateKeys = {
    idle: "state.idle",
    cooling: "state.cooling",
    safe_mode: "state.safety_run",
    startup: "state.startup",
  };

  function getTempClass(temp, sp) {
    if (typeof temp !== "number") return "ok";
    return temp > sp + 5 ? "alarm" : "ok";
  }

  // Helper to format time strings from seconds
  function formatTime(secs) {
    if (!secs) return "00:00";
    const h = Math.floor(secs / 3600);
    const m = Math.floor((secs % 3600) / 60);
    return h > 0
      ? `${h.toString().padStart(2, "0")}:${m.toString().padStart(2, "0")}`
      : `${m.toString().padStart(2, "0")}:${(secs % 60).toString().padStart(2, "0")}`;
  }
</script>

<div class="page" id="pg-dash">
  <!-- 1. Main Temp Card (Bento Box) -->
  <div class="card">
    <div
      class="temp-card {alarmActive || getTempClass(shownTemp, sp) === 'alarm'
        ? 'alarm'
        : 'ok'}"
      id="tempCard"
      style="--temp-glow: {tempGlow}"
    >
      <div class="temp-ey">{$t['dash.chamber']}</div>
      <div class="temp-hero">
        <div class="temp-dg">
          <span
            class="temp-n"
            id="tempN"
            style="color: {tempColor}"
          >
            {#if showDefrostSymbol}
              -d-
            {:else if typeof shownTemp === "number"}
              {shownTemp.toFixed(1)}
            {:else}
              —
            {/if}
          </span>
          <span class="temp-deg">{showDefrostSymbol ? "" : "°C"}</span>
        </div>
        <div class="hero-badges">
          <div
            class="badge {defrostActive
              ? 'orange'
              : compressor
                ? 'cyan'
                : 'neutral'}"
          >
            <span class="bdot"></span>
            {defrostActive
              ? $t['dash.mode_defrost']
              : compressor
                ? $t['dash.mode_cooling']
                : $t['dash.mode_idle']}
          </div>
          {#if nightActive}
            <div class="badge night">
              <svg width="12" height="12" viewBox="0 0 24 24" fill="none"
                stroke="currentColor" stroke-width="2">
                <path d="M21 12.79A9 9 0 1 1 11.21 3 7 7 0 0 0 21 12.79z"/>
              </svg>
              {$t['state.night']}
            </div>
          {/if}
        </div>
      </div>
      <div class="sp-area">
        <div class="sp-row">
          <span class="sp-lbl">{$t['dash.sp_label']}</span>
          <span class="sp-val" id="spD">{sp.toFixed(1)}<small>°C</small></span>
        </div>
        <div class="slider-wrap">
          <SliderWidget
            config={{
              key: "thermostat.setpoint",
              description: "",
              unit: "",
              min: spMeta.min,
              max: spMeta.max,
              step: spMeta.step,
              hideHeader: true,
              hideLabels: true,
            }}
            value={setpoint}
          />
        </div>
        <div class="rng-lbl">
          <span>{spMeta.min}</span><span>{spMeta.max}</span>
        </div>
      </div>
    </div>
  </div>

  <!-- 2. Metrics Row -->
  <div class="met-row">
    {#if hasEvapTemp}
      <div class="met">
        <div class="met-l">{$t['dash.evap_temp']}</div>
        <div class="met-v">
          {typeof $state["equipment.evap_temp"] === "number"
            ? $state["equipment.evap_temp"].toFixed(1)
            : "—"}<span class="met-u">°C</span>
        </div>
      </div>
    {/if}
    {#if hasCondTemp}
      <div class="met">
        <div class="met-l">{$t['dash.cond_short']}</div>
        <div class="met-v">
          {typeof $state["equipment.cond_temp"] === "number"
            ? $state["equipment.cond_temp"].toFixed(1)
            : "—"}<span class="met-u">°C</span>
        </div>
      </div>
    {/if}
  </div>

  <!-- 3. Protection Summary -->
  <div class="prot-row" class:alarm={alarmActive}>
    <svg class="prot-icon" width="18" height="18" viewBox="0 0 24 24" fill="none"
      stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
      <path d="M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z"/>
    </svg>
    <span class="prot-lbl">{$t['dash.protection']}</span>
    <div class="badge {alarmActive ? 'red' : 'green'} prot-badge">
      <span class="bdot"></span>
      {alarmActive ? $t['dash.prot_alarm'] : $t['dash.prot_ok']}
    </div>
    {#if alarmActive && alarmCode}
      <span class="prot-code">{alarmCode}</span>
    {/if}
  </div>

  <!-- 4. Equipment Grid -->
  <div class="eq-grid">
    <div class="eq-cell">
      <span class="eq-lbl">{$t['dash.compressor']}</span>
      <div class="eq-bot">
        <div class="badge {compressor ? 'blue' : 'neutral'}">
          <span class="bdot"></span>{compressor ? $t['eq.on'] : $t['eq.off']}
        </div>
        {#if compressor}
          <span class="eq-time"
            >{formatTime($state["equipment.comp_run_time"])}</span
          >
        {/if}
      </div>
      <div class="eq-progress {compressor ? 'active blue' : ''}"></div>
    </div>
    <div class="eq-cell">
      <span class="eq-lbl">{$t['dash.defrost_label']}</span>
      <div class="eq-bot">
        <div class="badge {defrostActive ? 'orange' : 'neutral'}">
          <span class="bdot"></span>{defrostActive ? $t['eq.on'] : $t['eq.idle']}
        </div>
        {#if !defrostActive && $state["defrost.time_to_next"]}
          <span class="eq-time"
            >{$t['dash.in_time']} {formatTime($state["defrost.time_to_next"] * 60)}</span
          >
        {/if}
      </div>
      <div class="eq-progress {defrostActive ? 'active orange' : ''}"></div>
    </div>
    <div class="eq-cell">
      <span class="eq-lbl">{$t['dash.evap_fan_label']}</span>
      <div class="eq-bot">
        <div class="badge {evapFan ? 'frost' : 'neutral'}">
          <span class="bdot"></span>{evapFan ? $t['eq.on'] : $t['eq.off']}
        </div>
      </div>
      <div class="eq-progress {evapFan ? 'active frost' : ''}"></div>
    </div>
    {#if hasCondFan}
      <div class="eq-cell">
        <span class="eq-lbl">{$t['dash.cond_fan_label']}</span>
        <div class="eq-bot">
          <div class="badge {condFan ? 'green' : 'neutral'}">
            <span class="bdot"></span>{condFan ? $t['eq.on'] : $t['eq.off']}
          </div>
        </div>
        <div class="eq-progress {condFan ? 'active green' : ''}"></div>
      </div>
    {/if}
  </div>

  <!-- 5. MiniChart -->
  <MiniChart />
</div>

<style>
  /* ===== SHARED CARDS ===== */
  .page {
    display: flex;
    flex-direction: column;
    gap: 14px;
    width: 100%;
  }
  .card {
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    overflow: hidden;
  }
  .badge {
    display: inline-flex;
    align-items: center;
    gap: 7px;
    padding: 6px 12px;
    border-radius: var(--radius-xs);
    font-size: 11px;
    font-weight: 600;
    letter-spacing: 0.4px;
    white-space: nowrap;
  }
  .bdot {
    width: 6px;
    height: 6px;
    border-radius: 50%;
    flex-shrink: 0;
  }
  .badge.cyan {
    background: var(--info-dim);
    color: var(--info);
  }
  .badge.cyan .bdot {
    background: var(--info);
    box-shadow: 0 0 6px var(--info-glow);
  }
  .badge.blue {
    background: var(--accent-dim);
    color: var(--accent);
  }
  .badge.blue .bdot {
    background: var(--accent);
    box-shadow: 0 0 6px var(--accent-glow);
  }
  .badge.green {
    background: var(--ok-dim);
    color: var(--ok);
  }
  .badge.green .bdot {
    background: var(--ok);
    box-shadow: 0 0 6px var(--ok-glow);
  }
  .badge.red {
    background: var(--danger-dim);
    color: var(--danger);
  }
  .badge.red .bdot {
    background: var(--danger);
    box-shadow: 0 0 4px var(--danger-glow);
  }
  .badge.orange {
    background: var(--warn-dim);
    color: var(--warn);
  }
  .badge.orange .bdot {
    background: var(--warn);
    box-shadow: 0 0 6px var(--warn-glow);
  }
  .badge.frost {
    background: var(--frost-dim);
    color: var(--frost);
  }
  .badge.frost .bdot {
    background: var(--frost);
    box-shadow: 0 0 6px var(--frost-glow);
  }
  .badge.neutral {
    background: var(--surface-2);
    color: var(--text-3);
    border: 1px solid var(--border);
  }
  .badge.neutral .bdot {
    background: var(--text-4);
  }

  /* ===== TEMP CARD ===== */
  .temp-card {
    position: relative;
    padding: 32px 28px 24px;
    background: linear-gradient(180deg, var(--surface), var(--surface-2));
    overflow: hidden;
  }
  .temp-card::before {
    content: "";
    position: absolute;
    width: 200px;
    height: 200px;
    border-radius: 50%;
    background: var(--temp-glow, transparent);
    bottom: -40px;
    right: -40px;
    filter: blur(60px);
    pointer-events: none;
    animation: heroBreath 4s ease-in-out infinite;
  }
  @keyframes heroBreath {
    0%, 100% { transform: scale(1); opacity: 0.8; }
    50% { transform: scale(1.15); opacity: 0.5; }
  }

  .temp-ey {
    font-size: 11px;
    font-weight: 600;
    color: var(--text-3);
    text-transform: uppercase;
    letter-spacing: 2px;
    margin-bottom: 12px;
    position: relative;
    z-index: 1;
  }
  .temp-hero {
    display: flex;
    align-items: flex-start;
    justify-content: space-between;
    margin-bottom: 24px;
    position: relative;
    z-index: 1;
  }
  .temp-dg {
    display: flex;
    align-items: baseline;
    gap: 4px;
  }
  .temp-n {
    font-size: var(--text-hero-lg);
    font-weight: 300;
    line-height: 1;
    letter-spacing: -3px;
    font-variant-numeric: tabular-nums;
    transition: color 0.5s ease;
    text-shadow: 0 0 30px currentColor;
  }

  .temp-deg {
    font-size: clamp(18px, 4vw, 24px);
    font-weight: 300;
    color: var(--text-4);
    margin-left: 4px;
    align-self: flex-start;
    margin-top: 10px;
  }

  .sp-area {
    margin-top: 20px;
    padding-top: 20px;
    border-top: 1px solid var(--border);
  }
  .sp-row {
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: 14px;
  }
  .sp-lbl {
    font-size: 13px;
    color: var(--text-2);
    font-weight: 500;
  }
  .sp-val {
    font-size: 28px;
    font-weight: 600;
    color: var(--ok);
    letter-spacing: -1px;
    font-variant-numeric: tabular-nums;
    text-shadow: 0 0 24px var(--ok-glow);
  }
  .sp-val small {
    font-size: 14px;
    font-weight: 400;
    color: var(--text-3);
    text-shadow: none;
  }

  .rng-lbl {
    display: flex;
    justify-content: space-between;
    margin-top: 2px;
  }
  .rng-lbl span {
    font-size: 9px;
    color: var(--text-4);
    font-variant-numeric: tabular-nums;
  }

  .slider-wrap {
    width: 100%;
    margin-top: -12px;
    margin-bottom: -16px;
  }

  /* ===== METRICS ROW ===== */
  .met-row {
    display: grid;
    grid-template-columns: 1fr 1fr 1fr;
    gap: 14px;
  }
  .met {
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    padding: 16px 14px;
    position: relative;
    overflow: hidden;
    transition:
      transform var(--transition-fast),
      border-color var(--transition-fast);
  }
  .met:hover {
    transform: translateY(-2px);
    border-color: var(--border-accent);
  }
  .met::before {
    content: "";
    position: absolute;
    left: 0;
    top: 0;
    bottom: 0;
    width: 3px;
    background: var(--border-accent);
  }
  .met:nth-child(1)::before {
    background: var(--frost);
    box-shadow: 0 0 8px var(--frost-glow);
  }
  .met:nth-child(2)::before {
    background: var(--warn);
    box-shadow: 0 0 8px var(--warn-glow);
  }
  .met:nth-child(3)::before {
    background: var(--accent);
    box-shadow: 0 0 8px var(--accent-glow);
  }

  .met::after {
    content: "";
    position: absolute;
    top: 0;
    left: 0;
    right: 0;
    height: 1px;
    background: linear-gradient(
      90deg,
      transparent,
      var(--border-accent),
      transparent
    );
  }
  .met-l {
    font-size: 9px;
    font-weight: 600;
    color: var(--text-3);
    text-transform: uppercase;
    letter-spacing: 1.2px;
    margin-bottom: 8px;
  }
  .met-v {
    font-size: 22px;
    font-weight: 700;
    letter-spacing: -1px;
    font-variant-numeric: tabular-nums;
    color: var(--text-1);
  }
  .met-u {
    font-size: 12px;
    font-weight: 300;
    color: var(--text-4);
  }

  /* ===== EQUIPMENT GRID ===== */
  .eq-grid {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 12px;
  }
  .eq-cell {
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    padding: 16px;
    display: flex;
    flex-direction: column;
    gap: 12px;
    position: relative;
    overflow: hidden;
    transition:
      transform var(--transition-fast),
      border-color var(--transition-fast);
  }
  .eq-cell:hover {
    transform: translateY(-2px);
    border-color: var(--border-accent);
  }
  .eq-lbl {
    font-size: 12px;
    color: var(--text-2);
    font-weight: 500;
  }
  .eq-bot {
    display: flex;
    align-items: center;
    justify-content: space-between;
  }
  .eq-time {
    font-size: 11px;
    color: var(--text-3);
    font-variant-numeric: tabular-nums;
  }

  .eq-progress {
    position: absolute;
    bottom: 0;
    left: 0;
    right: 0;
    height: 2px;
    background: var(--surface-3);
    overflow: hidden;
  }
  .eq-progress::after {
    content: "";
    position: absolute;
    top: 0;
    left: 0;
    width: 50%;
    height: 100%;
    background: var(--text-4);
    transition: background 0.3s;
    transform: translateX(-100%);
    will-change: transform;
  }
  .eq-progress.active::after {
    animation: eqScan 2s ease-in-out infinite;
  }
  .eq-progress.blue.active::after {
    background: var(--accent);
    box-shadow: 0 0 8px var(--accent-glow);
  }
  .eq-progress.orange.active::after {
    background: var(--warn);
    box-shadow: 0 0 8px var(--warn-glow);
  }
  .eq-progress.frost.active::after {
    background: var(--frost);
    box-shadow: 0 0 8px var(--frost-glow);
  }
  .eq-progress.green.active::after {
    background: var(--ok);
    box-shadow: 0 0 8px var(--ok-glow);
  }

  @keyframes eqScan {
    0% {
      transform: translateX(-100%);
    }
    100% {
      transform: translateX(200%);
    }
  }

  /* ===== HERO BADGES ===== */
  .hero-badges {
    display: flex;
    flex-direction: column;
    align-items: flex-end;
    gap: 6px;
  }
  .badge.night {
    background: var(--warn-dim);
    color: var(--warn);
    gap: 5px;
  }

  /* ===== PROTECTION SUMMARY ===== */
  .prot-row {
    display: flex;
    align-items: center;
    gap: 10px;
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    padding: 14px 16px;
    transition: border-color 0.3s;
  }
  .prot-row.alarm {
    border-color: var(--danger-border);
    background: var(--danger-dim);
  }
  .prot-icon {
    flex-shrink: 0;
    color: var(--ok);
  }
  .prot-row.alarm .prot-icon {
    color: var(--danger);
  }
  .prot-lbl {
    font-size: 13px;
    font-weight: 600;
    color: var(--text-1);
    flex: 1;
  }
  .prot-badge {
    flex-shrink: 0;
  }
  .prot-code {
    font-size: 12px;
    font-weight: 700;
    color: var(--danger);
    font-family: var(--font-mono);
    letter-spacing: 0.5px;
  }
</style>
