/**
 * Перевірити visible_when умову.
 * @param {object} vw  — visible_when з ui.json ({key, eq|neq|in})
 * @param {object} st  — поточний $state
 * @returns {boolean}
 */
export function isVisible(vw, st) {
  if (!vw) return true;
  const val = st[vw.key];
  if ('eq' in vw)  return val === vw.eq;
  if ('neq' in vw) return val !== vw.neq;
  if ('in' in vw)  return Array.isArray(vw.in) && vw.in.includes(val);
  return true;
}
