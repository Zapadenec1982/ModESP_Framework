import { writable, derived } from 'svelte/store';
import { createWebSocket } from '../lib/websocket.js';

/** All SharedState keys as reactive store */
export const state = writable({});

/** WebSocket connection status */
export const wsConnected = writable(false);

let wsClient = null;

export function initWebSocket() {
  if (wsClient) return;
  wsClient = createWebSocket((data) => {
    if ('_ws_connected' in data) {
      wsConnected.set(data._ws_connected);
      return;
    }
    state.update(s => ({ ...s, ...data }));
  });
}

/** Optimistic update: set value locally before server confirms */
export function setStateKey(key, value) {
  state.update(s => ({ ...s, [key]: value }));
}

/** Get a derived store for a single state key */
export function stateKey(key) {
  return derived(state, $s => $s[key]);
}
