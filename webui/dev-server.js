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
  'system.uptime': 86400,
  'system.heap_free': 142000,
  'system.heap_largest': 98000,
  'system.wifi_rssi': -52,

  // Thermostat
  'thermostat.temperature': -17.4,
  'thermostat.display_temp': -17.4,
  'thermostat.setpoint': -18.0,
  'thermostat.effective_setpoint': -18.0,
  'thermostat.differential': 2.0,
  'thermostat.state': 'cooling',
  'thermostat.night_active': false,
  'thermostat.night_mode': 0,
  'thermostat.night_setback': 2.0,
  'thermostat.night_start': 22,
  'thermostat.night_end': 6,
  'thermostat.display_defrost': 0,
  'thermostat.min_on_time': 3,
  'thermostat.min_off_time': 5,
  'thermostat.startup_delay': 2,
  'thermostat.evap_fan_mode': 1,
  'thermostat.evap_fan_temp': -5.0,
  'thermostat.evap_fan_hyst': 2.0,
  'thermostat.cond_fan_delay': 30,
  'thermostat.safety_run_on': 15,
  'thermostat.safety_run_off': 15,

  // Equipment
  'equipment.air_temp': -17.4,
  'equipment.evap_temp': -24.3,
  'equipment.cond_temp': 38.7,
  'equipment.compressor': true,
  'equipment.evap_fan': true,
  'equipment.cond_fan': true,
  'equipment.defrost_relay': false,
  'equipment.door_contact': false,
  'equipment.night_input': false,
  'equipment.sensor1_ok': true,
  'equipment.sensor2_ok': true,
  'equipment.filter_coeff': 4,
  'equipment.comp_run_time': 1845,
  'equipment.has_evap_temp': true,
  'equipment.has_cond_temp': true,
  'equipment.has_cond_fan': true,
  'equipment.has_door_contact': true,
  'equipment.has_defrost_relay': true,
  'equipment.has_night_input': false,

  // Defrost
  'defrost.state': 'idle',
  'defrost.phase': 'idle',
  'defrost.active': false,
  'defrost.type': 2,
  'defrost.interval': 6,
  'defrost.counter_mode': 1,
  'defrost.interval_timer': 142,
  'defrost.time_to_next': 218,
  'defrost.end_temp': 12.0,
  'defrost.max_duration': 30,
  'defrost.drip_time': 2,
  'defrost.fad_duration': 5,
  'defrost.fad_temp': -2.0,
  'defrost.demand_temp': -30.0,
  'defrost.stabilize_time': 60,
  'defrost.equalize_time': 30,
  'defrost.defrost_count': 3,

  // Protection
  'protection.alarm_active': false,
  'protection.alarm_code': 'none',
  'protection.high_temp_alarm': false,
  'protection.low_temp_alarm': false,
  'protection.sensor1_alarm': false,
  'protection.sensor2_alarm': false,
  'protection.door_alarm': false,
  'protection.short_cycle_alarm': false,
  'protection.rapid_cycle_alarm': false,
  'protection.continuous_run_alarm': false,
  'protection.pulldown_alarm': false,
  'protection.rate_alarm': false,
  'protection.reset_alarms': false,
  'protection.manual_reset': false,
  'protection.high_limit': 12.0,
  'protection.low_limit': -35.0,
  'protection.high_alarm_delay': 30,
  'protection.low_alarm_delay': 30,
  'protection.door_delay': 15,
  'protection.post_defrost_delay': 30,
  'protection.min_compressor_run': 120,
  'protection.max_starts_hour': 8,
  'protection.max_continuous_run': 120,
  'protection.pulldown_timeout': 60,
  'protection.pulldown_min_drop': 2.0,
  'protection.max_rise_rate': 2.0,
  'protection.rate_duration': 15,
  'protection.compressor_starts_1h': 3,
  'protection.compressor_duty': 62.5,
  'protection.compressor_run_time': 1845,
  'protection.compressor_hours': 4271.5,
  'protection.last_cycle_run': 1845,
  'protection.last_cycle_off': 320,
  'protection.lockout': false,

  // DataLogger
  'datalogger.enabled': true,
  'datalogger.retention_hours': 48,
  'datalogger.sample_interval': 60,
  'datalogger.log_evap': true,
  'datalogger.log_cond': false,
  'datalogger.log_setpoint': true,
  'datalogger.log_humidity': false,
  'datalogger.records_count': 2880,
  'datalogger.events_count': 156,
  'datalogger.flash_used': 52,
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
