/**
 * ModESP Dev Server — mock API + WebSocket + rollup watch
 * Usage: node dev-server.js
 */
import { createServer } from 'http';
import { readFileSync, existsSync } from 'fs';
import { join, extname } from 'path';
import { fileURLToPath } from 'url';
import { spawn } from 'child_process';
import { WebSocketServer } from 'ws';

const __dirname = fileURLToPath(new URL('.', import.meta.url));
const PORT = 5000;

// ── Mock State ──────────────────────────────────────────
const mockState = {
  // System
  'system.uptime': 3600,
  'system.heap_free': 150000,
  'system.heap_largest': 110000,
  'system.wifi_rssi': -45,

  // Equipment (framework — generic sensor + actuator)
  'equipment.air_temp': 21.5,
  'equipment.actuator_1': false,
  'equipment.air_temp_ok': true,
  'equipment.filter_coeff': 4,
  'equipment.has_air_temp': true,
  'equipment.has_actuator_1': true,

  // Simple Thermostat (demo module)
  'simple_thermo.temperature': 21.5,
  'simple_thermo.setpoint': 22.0,
  'simple_thermo.differential': 1.0,
  'simple_thermo.state': 'idle',
  'simple_thermo.output': false,

  // DataLogger
  'datalogger.enabled': true,
  'datalogger.retention_hours': 48,
  'datalogger.sample_interval': 60,
  'datalogger.records_count': 120,
  'datalogger.events_count': 5,
  'datalogger.flash_used': 2,
};

// ── MIME types ───────────────────────────────────────────
const MIME = {
  '.html': 'text/html',
  '.js':   'text/javascript',
  '.css':  'text/css',
  '.json': 'application/json',
  '.svg':  'image/svg+xml',
  '.png':  'image/png',
  '.ico':  'image/x-icon',
};

// ── Start rollup in watch mode ──────────────────────────
console.log('Starting rollup in watch mode...');
const rollup = spawn('npx', ['rollup', '-c', '-w'], {
  stdio: 'inherit',
  shell: true,
  cwd: __dirname,
});

// ── HTTP Server ─────────────────────────────────────────
const server = createServer((req, res) => {
  const url = new URL(req.url, `http://localhost:${PORT}`);
  res.setHeader('Access-Control-Allow-Origin', '*');
  res.setHeader('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
  res.setHeader('Access-Control-Allow-Headers', 'Content-Type, Authorization');

  if (req.method === 'OPTIONS') {
    res.writeHead(204);
    res.end();
    return;
  }

  // ── API Routes ──────────────────────────────────────
  if (url.pathname === '/api/ui') {
    try {
      const ui = readFileSync(join(__dirname, '..', 'data', 'ui.json'), 'utf8');
      res.writeHead(200, { 'Content-Type': 'application/json' });
      res.end(ui);
    } catch (e) {
      res.writeHead(500);
      res.end(JSON.stringify({ error: 'ui.json not found: ' + e.message }));
    }
    return;
  }

  if (url.pathname === '/api/state') {
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify(mockState));
    return;
  }

  if (url.pathname === '/api/settings' && req.method === 'POST') {
    let body = '';
    req.on('data', c => body += c);
    req.on('end', () => {
      try {
        const data = JSON.parse(body);
        Object.assign(mockState, data);
        // Broadcast changed keys to all WS clients
        broadcastWs(data);
      } catch {}
      res.writeHead(200, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify({ ok: true }));
    });
    return;
  }

  if (url.pathname === '/api/log') {
    // Mock chart data matching the REAL generated channels/events (template
    // project): channels air_temp/temperature, events 30/31 (heat ON/OFF BOTH)
    // + system power-on (10). Keeps the dev-server useful for verifying the
    // generator's chart-widget config — no stale refrigeration keys.
    const now = Math.floor(Date.now() / 1000);
    const temp = [];
    const channels = ['air_temp', 'temperature'];
    for (let i = 120; i >= 0; i--) {
      const ts = now - i * 60;
      const airTemp = 21 + Math.sin(i / 20) * 2 + (Math.random() - 0.5) * 0.3;
      const thermo = 22 + Math.sin(i / 25) * 1.2;
      temp.push([ts, +airTemp.toFixed(1), +thermo.toFixed(1)]);
    }
    const events = [
      [now - 7200, 10],  // power on (system)
      [now - 5400, 30],  // heat ON  (BOTH rising)
      [now - 3600, 31],  // heat OFF (BOTH falling = id+1)
      [now - 1800, 30],  // heat ON
    ];
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({ channels, temp, events }));
    return;
  }

  if (url.pathname === '/api/log/summary') {
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({
      hours: 48,
      temp_count: 2880,
      event_count: 156,
      flash_kb: 52,
    }));
    return;
  }

  if (url.pathname === '/api/board') {
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({ board: 'kc868_a6', version: '1.0.0' }));
    return;
  }

  if (url.pathname === '/api/modules') {
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify([
      { name: 'equipment', status: 'running', priority: 0 },
      { name: 'simple_thermo', status: 'running', priority: 2 },
      { name: 'datalogger', status: 'running', priority: 3 },
    ]));
    return;
  }

  if (url.pathname === '/api/bindings') {
    try {
      const b = readFileSync(join(__dirname, '..', 'data', 'bindings.json'), 'utf8');
      res.writeHead(200, { 'Content-Type': 'application/json' });
      res.end(b);
    } catch {
      res.writeHead(200, { 'Content-Type': 'application/json' });
      res.end('[]');
    }
    return;
  }

  if (url.pathname === '/api/onewire/scan') {
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({
      devices: [
        { address: '28FF1234560000A1', temperature: 21.4, role: 'air_temp' },
        { address: '28FF5678900000B2', temperature: 22.1, role: null },
      ]
    }));
    return;
  }

  if (url.pathname.startsWith('/api/')) {
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({ ok: true }));
    return;
  }

  // ── Static files: dist/ first, then public/ ─────────
  const urlPath = url.pathname === '/' ? '/index.html' : url.pathname;
  const distPath = join(__dirname, 'dist', urlPath);
  const publicPath = join(__dirname, 'public', urlPath);
  let filePath;

  if (existsSync(distPath)) filePath = distPath;
  else if (existsSync(publicPath)) filePath = publicPath;
  else filePath = join(__dirname, 'public', 'index.html'); // SPA fallback

  const ext = extname(filePath);
  const mime = MIME[ext] || 'application/octet-stream';

  try {
    const content = readFileSync(filePath);
    res.writeHead(200, { 'Content-Type': mime });
    res.end(content);
  } catch {
    res.writeHead(404);
    res.end('Not found');
  }
});

// ── WebSocket Server (mock real-time updates) ───────────
const wss = new WebSocketServer({ server, path: '/ws' });
const wsClients = new Set();

function broadcastWs(data) {
  const msg = JSON.stringify(data);
  for (const ws of wsClients) {
    if (ws.readyState === 1) ws.send(msg);
  }
}

wss.on('connection', (ws) => {
  wsClients.add(ws);
  console.log(`  WS client connected (${wsClients.size} total)`);

  // Send full state on connect
  ws.send(JSON.stringify(mockState));

  // Simulate periodic drift on the real template keys (generic, no
  // refrigeration-specific state).
  const interval = setInterval(() => {
    const drift = (Math.random() - 0.5) * 0.15;
    mockState['equipment.air_temp'] = +(mockState['equipment.air_temp'] + drift).toFixed(2);
    mockState['simple_thermo.temperature'] = mockState['equipment.air_temp'];
    mockState['system.uptime'] += 2;

    const delta = {
      'equipment.air_temp': mockState['equipment.air_temp'],
      'simple_thermo.temperature': mockState['simple_thermo.temperature'],
      'system.uptime': mockState['system.uptime'],
    };
    if (ws.readyState === 1) ws.send(JSON.stringify(delta));
  }, 2000);

  ws.on('close', () => {
    wsClients.delete(ws);
    clearInterval(interval);
    console.log(`  WS client disconnected (${wsClients.size} total)`);
  });
});

// ── Start ────────────────────────────────────────────────
server.listen(PORT, () => {
  console.log(`\n  ╔═══════════════════════════════════╗`);
  console.log(`  ║  ModESP Dev Server                ║`);
  console.log(`  ║  http://localhost:${PORT}            ║`);
  console.log(`  ╚═══════════════════════════════════╝\n`);
});

// Cleanup on exit
process.on('SIGINT', () => {
  rollup.kill();
  process.exit();
});
