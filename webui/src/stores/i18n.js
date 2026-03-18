import { writable, derived } from 'svelte/store';
import uk from '../i18n/uk.js';
import en from '../i18n/en.js';
import de from '../i18n/de.js';
import pl from '../i18n/pl.js';

const KEY = 'modesp-lang';
const LANGS = ['uk', 'en', 'de', 'pl'];
const dicts = { uk, en, de, pl };

function getInitial() {
  try {
    const s = localStorage.getItem(KEY);
    if (LANGS.includes(s)) return s;
  } catch (e) {}
  const nav = navigator.language || '';
  if (nav.startsWith('uk') || nav.startsWith('ru')) return 'uk';
  if (nav.startsWith('de')) return 'de';
  if (nav.startsWith('pl')) return 'pl';
  return 'en';
}

export const language = writable(getInitial());
export const supportedLangs = LANGS;

language.subscribe(v => {
  document.documentElement.lang = v;
  try { localStorage.setItem(KEY, v); } catch (e) {}
});

/** Chrome UI strings (buttons, toasts, status labels) */
export const t = derived(language, $lang => dicts[$lang] || dicts.uk);

/** Cycle through supported languages (UK → EN → DE → ...) */
export function cycleLanguage() {
  language.update(l => {
    const idx = LANGS.indexOf(l);
    return LANGS[(idx + 1) % LANGS.length];
  });
}

/** @deprecated Use cycleLanguage() instead */
export function toggleLanguage() {
  cycleLanguage();
}
