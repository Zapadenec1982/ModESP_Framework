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
    // Mock empty chart data
    const now = Math.floor(Date.now() / 1000);
    const temp = [];
    const channels = ['air', 'evap', 'setpoint'];
    // Generate 2h of fake data
    for (let i = 120; i >= 0; i--) {
      const ts = now - i * 60;
      const air = -17 + Math.sin(i / 20) * 2 + (Math.random() - 0.5) * 0.3;
      const evap = -24 + Math.sin(i / 20) * 1.5;
      const sp = -18;
      temp.push([ts, +air.toFixed(1), +evap.toFixed(1), sp]);
    }
    const events = [
      [now - 7200, 10],
      [now - 5400, 1],
      [now - 3600, 2],
      [now - 1800, 1],
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
      { name: 'protection', status: 'running', priority: 1 },
      { name: 'thermostat', status: 'running', priority: 2 },
      { name: 'defrost', status: 'running', priority: 2 },
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
        { address: '28FF1234560000A1', temperature: -17.4, role: 'air_temp' },
        { address: '28FF5678900000B2', temperature: -24.3, role: 'evap_temp' },
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

  // Simulate periodic temperature drift
  const interval = setInterval(() => {
    const drift = (Math.random() - 0.5) * 0.15;
    mockState['thermostat.temperature'] = +(mockState['thermostat.temperature'] + drift).toFixed(2);
    mockState['thermostat.display_temp'] = mockState['thermostat.temperature'];
    mockState['equipment.air_temp'] = mockState['thermostat.temperature'];

    // Evap temp slight drift
    mockState['equipment.evap_temp'] = +(-24 + Math.sin(Date.now() / 30000) * 1.5).toFixed(1);
    // Cond temp slight drift
    mockState['equipment.cond_temp'] = +(38 + Math.sin(Date.now() / 25000) * 2).toFixed(1);

    // Uptime increment
    mockState['system.uptime'] += 2;
    // Compressor run time increment (if compressor ON)
    if (mockState['equipment.compressor']) {
      mockState['equipment.comp_run_time'] += 2;
      mockState['protection.compressor_run_time'] += 2;
    }

    const delta = {
      'thermostat.temperature': mockState['thermostat.temperature'],
      'thermostat.display_temp': mockState['thermostat.display_temp'],
      'equipment.air_temp': mockState['equipment.air_temp'],
      'equipment.evap_temp': mockState['equipment.evap_temp'],
      'equipment.cond_temp': mockState['equipment.cond_temp'],
      'system.uptime': mockState['system.uptime'],
      'equipment.comp_run_time': mockState['equipment.comp_run_time'],
      'protection.compressor_run_time': mockState['protection.compressor_run_time'],
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
