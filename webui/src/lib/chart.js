/**
 * Shared chart math utilities — used by ChartWidget and MiniChart.
 */

/** SVG polyline path (пряме з'єднання точок, без overshoots) */
export function polylinePath(points) {
  if (points.length < 2) return '';
  let d = `M${points[0].x},${points[0].y}`;
  for (let i = 1; i < points.length; i++) {
    d += ` L${points[i].x},${points[i].y}`;
  }
  return d;
}

/** Build SVG path segments for one channel (handles null gaps) */
export function buildSegments(pts, chIdx, pad, xFn, yFn) {
  const segments = [];
  let seg = [];
  for (const p of pts) {
    const raw = p[chIdx];
    if (raw == null) {
      if (seg.length > 0) { segments.push(polylinePath(seg)); seg = []; }
      continue;
    }
    seg.push({ x: +(pad.left + xFn(p[0])).toFixed(1), y: +(pad.top + yFn(raw / 10)).toFixed(1) });
  }
  if (seg.length > 0) segments.push(polylinePath(seg));
  return segments;
}

/** Find min/max temperature from data points across given channel indices */
export function tempRange(pts, channelIndices, defaultRange) {
  let mn = Infinity, mx = -Infinity;
  for (const p of pts) {
    for (const idx of channelIndices) {
      const raw = p[idx];
      if (raw == null) continue;
      const v = raw / 10;
      if (v < mn) mn = v;
      if (v > mx) mx = v;
    }
  }
  if (mn === Infinity) return defaultRange || [-25, -15];
  const margin = Math.max((mx - mn) * 0.1, 1);
  return [Math.floor(mn - margin), Math.ceil(mx + margin)];
}

/** Generate time axis labels (N evenly-spaced) */
export function computeTimeLabels(tMin, tMax, count, padLeft, xFn) {
  if (tMin === tMax) return [];
  const labels = [];
  for (let i = 0; i <= count; i++) {
    const ts = tMin + (tMax - tMin) * i / count;
    const d = new Date(ts * 1000);
    labels.push({
      x: padLeft + xFn(ts),
      label: `${String(d.getHours()).padStart(2,'0')}:${String(d.getMinutes()).padStart(2,'0')}`
    });
  }
  return labels;
}

/** Generate temperature axis labels */
export function computeTempLabels(vMin, vMax, count, padTop, yFn) {
  const labels = [];
  const step = Math.max(1, Math.round((vMax - vMin) / count));
  for (let v = Math.ceil(vMin); v <= Math.floor(vMax); v += step) {
    labels.push({ y: padTop + yFn(v), label: `${v}°` });
  }
  return labels;
}
