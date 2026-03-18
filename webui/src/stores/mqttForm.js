import { writable } from 'svelte/store';

export const mqttBroker = writable('');
export const mqttUser = writable('');
export const mqttPass = writable('');
export const mqttPrefix = writable('');
