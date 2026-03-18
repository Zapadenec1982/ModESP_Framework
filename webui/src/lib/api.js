import { writable } from 'svelte/store';

const BASE = '';
const AUTH_KEY = 'modesp_auth';

/** Store: true when a 401 response needs login */
export const needsLogin = writable(false);

/** Зберегти credentials (base64 "user:pass") */
export function setAuth(user, pass) {
  const creds = btoa(user + ':' + pass);
  sessionStorage.setItem(AUTH_KEY, creds);
}

/** Очистити credentials */
export function clearAuth() {
  sessionStorage.removeItem(AUTH_KEY);
}

/** Auth header для protected requests */
function authHeaders() {
  const creds = sessionStorage.getItem(AUTH_KEY);
  return creds ? { 'Authorization': `Basic ${creds}` } : {};
}

export async function apiGet(url) {
  const r = await fetch(BASE + url);
  if (!r.ok) {
    const text = await r.text().catch(() => '');
    throw new Error(text || `GET ${url}: ${r.status}`);
  }
  return r.json();
}

export async function apiPost(url, data) {
  const r = await fetch(BASE + url, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json', ...authHeaders() },
    body: JSON.stringify(data)
  });
  if (r.status === 401) {
    needsLogin.set(true);
    throw new Error('Unauthorized');
  }
  if (!r.ok) {
    const text = await r.text().catch(() => '');
    throw new Error(text || httpStatusMessage(r.status, url));
  }
  return r.json();
}

export async function apiUpload(url, file, onProgress) {
  return new Promise((resolve, reject) => {
    const xhr = new XMLHttpRequest();
    xhr.open('POST', BASE + url);
    const creds = sessionStorage.getItem(AUTH_KEY);
    if (creds) xhr.setRequestHeader('Authorization', `Basic ${creds}`);
    xhr.upload.onprogress = (e) => {
      if (e.lengthComputable && onProgress) {
        onProgress(Math.round(e.loaded / e.total * 100), e.loaded);
      }
    };
    xhr.onload = () => {
      if (xhr.status === 200) resolve(JSON.parse(xhr.responseText || '{}'));
      else if (xhr.status === 401) {
        needsLogin.set(true);
        reject(new Error('Unauthorized'));
      }
      else reject(new Error(xhr.responseText || httpStatusMessage(xhr.status, url)));
    };
    xhr.onerror = () => reject(new Error('Connection error'));
    xhr.setRequestHeader('Content-Type', 'application/octet-stream');
    xhr.send(file);
  });
}

function httpStatusMessage(status, url) {
  const map = {
    400: 'Bad request',
    401: 'Unauthorized',
    404: 'Not found',
    413: 'Too large',
    422: 'Invalid value',
    500: 'Server error',
  };
  return map[status] || `${url}: ${status}`;
}
