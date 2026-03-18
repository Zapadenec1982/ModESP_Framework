/**
 * Auto-reconnecting WebSocket client.
 * Calls onMessage(data) with parsed JSON on each message.
 * Sends _ws_connected meta-events for connection status.
 */
export function createWebSocket(onMessage) {
  let ws = null;
  let retry = 1000;
  let stopped = false;

  function connect() {
    if (stopped) return;
    if (ws && ws.readyState < 2) return;

    const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
    ws = new WebSocket(`${proto}//${location.host}/ws`);

    ws.onopen = () => {
      retry = 1000;
      onMessage({ _ws_connected: true });
    };

    ws.onmessage = (e) => {
      try {
        onMessage(JSON.parse(e.data));
      } catch (err) {
        console.warn('WS parse error', err);
      }
    };

    ws.onclose = () => {
      onMessage({ _ws_connected: false });
      if (!stopped) {
        setTimeout(connect, retry);
        retry = Math.min(retry * 2, 30000);
      }
    };

    ws.onerror = () => {};
  }

  connect();

  return {
    stop() {
      stopped = true;
      if (ws) ws.close();
    }
  };
}
