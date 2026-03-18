import { writable } from 'svelte/store';

const KEY = 'modesp-theme';

function getInitial() {
  try {
    const s = localStorage.getItem(KEY);
    if (s === 'light' || s === 'dark') return s;
  } catch (e) {}
  if (window.matchMedia('(prefers-color-scheme: light)').matches) return 'light';
  return 'dark';
}

export const theme = writable(getInitial());

theme.subscribe(v => {
  document.documentElement.setAttribute('data-theme', v);
  try { localStorage.setItem(KEY, v); } catch (e) {}
});

export function toggleTheme() {
  theme.update(t => t === 'dark' ? 'light' : 'dark');
}
