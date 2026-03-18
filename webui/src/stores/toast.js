import { writable } from 'svelte/store';

export const toasts = writable([]);

const MAX_TOASTS = 3;
let nextId = 0;

function addToast(msg, type, duration) {
  const id = ++nextId;
  toasts.update(t => {
    const list = [...t, { id, msg, type }];
    // Max 3 тости — старіші видаляються
    return list.length > MAX_TOASTS ? list.slice(list.length - MAX_TOASTS) : list;
  });
  setTimeout(() => {
    toasts.update(t => t.filter(x => x.id !== id));
  }, duration);
}

export function dismissToast(id) {
  toasts.update(t => t.filter(x => x.id !== id));
}

export function toastSuccess(msg) { addToast(msg, 'success', 3000); }
export function toastError(msg) { addToast(msg, 'error', 8000); }
export function toastWarn(msg) { addToast(msg, 'warn', 5000); }
