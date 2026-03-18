/**
 * Moving average — згладжує квантовані дані (0.1°C сходинки) перед рендером.
 * Кожен канал згладжується незалежно, null пропускаються.
 * @param {Array} points - [[timestamp, v1, v2, ...], ...]
 * @param {number} window - розмір вікна (непарне число, default 5)
 * @returns {Array} smoothed points (timestamps unchanged)
 */
export function movingAverage(points, window = 5) {
  if (points.length <= window) return points;
  const half = Math.floor(window / 2);
  const cols = points[0]?.length || 0;
  return points.map((p, i) => {
    const result = [p[0]];
    for (let col = 1; col < cols; col++) {
      if (p[col] == null) { result.push(null); continue; }
      let sum = 0, count = 0;
      for (let j = Math.max(0, i - half); j < Math.min(points.length, i + half + 1); j++) {
        if (points[j][col] != null) { sum += points[j][col]; count++; }
      }
      result.push(count > 0 ? Math.round(sum / count) : null);
    }
    return result;
  });
}

/**
 * Average bucket downsampling — плавна крива для згладжування.
 * Кожен бакет → одна точка з середнім значенням усіх каналів.
 * @param {Array} points - [[timestamp, v1, v2, ...], ...]
 * @param {number} maxPoints - максимум точок у результаті
 * @param {number} channelIdx - основний канал (для null-перевірки)
 * @returns {Array} downsampled points
 */
export function downsampleAvg(points, maxPoints, channelIdx = 1) {
  if (points.length <= maxPoints) return points;
  const bucket = Math.ceil(points.length / maxPoints);
  const result = [];
  for (let i = 0; i < points.length; i += bucket) {
    const slice = points.slice(i, i + bucket);
    const valid = slice.filter(p => p[channelIdx] != null);
    if (valid.length === 0) { result.push(slice[0]); continue; }
    // Середнє по timestamp та всім каналам
    const avg = new Array(valid[0].length).fill(0);
    for (const p of valid) for (let j = 0; j < p.length; j++) avg[j] += (p[j] || 0);
    for (let j = 0; j < avg.length; j++) avg[j] = Math.round(avg[j] / valid.length);
    result.push(avg);
  }
  return result;
}

/**
 * Min/Max bucket downsampling — зберігає форму кривої та піки.
 * @param {Array} points - [[timestamp, v1, v2, ...], ...]
 * @param {number} maxPoints - максимум точок у результаті
 * @param {number} channelIdx - індекс каналу для порівняння (default 1 = air)
 * @returns {Array} downsampled points
 */
export function downsample(points, maxPoints, channelIdx = 1) {
  if (points.length <= maxPoints) return points;
  const bucket = Math.ceil(points.length / maxPoints);
  const result = [];
  for (let i = 0; i < points.length; i += bucket) {
    const slice = points.slice(i, i + bucket);
    // Фільтруємо точки де канал має дані (не null)
    const valid = slice.filter(p => p[channelIdx] != null);
    if (valid.length === 0) {
      // Якщо всі null — беремо першу точку з бакету
      result.push(slice[0]);
      continue;
    }
    const min = valid.reduce((a, b) => a[channelIdx] < b[channelIdx] ? a : b);
    const max = valid.reduce((a, b) => a[channelIdx] > b[channelIdx] ? a : b);
    if (min[0] < max[0]) { result.push(min, max); }
    else { result.push(max, min); }
  }
  return result;
}
