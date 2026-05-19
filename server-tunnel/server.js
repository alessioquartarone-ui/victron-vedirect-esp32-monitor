'use strict';

/*
  SolarLink / Victron ESP32 Remote WebUI Tunnel Server
  Version: 0.1.0

  Purpose:
  - ESP32 polls this server from behind NAT/4G/router.
  - Browser asks this server for a remote path.
  - Server queues request for the ESP32.
  - ESP32 returns the response.
  - Browser receives the ESP32 WebUI/JSON response.

  First POC:
  - Polling tunnel, not WebSocket yet.
  - Token-based device auth.
  - One in-memory request queue.
  - No database required.
  - Good for Oracle Free / small VPS testing.
*/

const express = require('express');
const helmet = require('helmet');
const morgan = require('morgan');
const { v4: uuidv4 } = require('uuid');

const app = express();

const PORT = Number(process.env.PORT || 8080);
const PUBLIC_BASE_URL = process.env.PUBLIC_BASE_URL || '';
const ADMIN_TOKEN = process.env.ADMIN_TOKEN || 'change-this-admin-token';
const DEFAULT_DEVICE_TOKEN = process.env.DEFAULT_DEVICE_TOKEN || 'change-this-device-token';
const REQUEST_TIMEOUT_MS = Number(process.env.REQUEST_TIMEOUT_MS || 30000);
const MAX_BODY_BYTES = Number(process.env.MAX_BODY_BYTES || 512000);

app.use(helmet({
  contentSecurityPolicy: false
}));

app.use(morgan('combined'));

app.use(express.json({
  limit: `${MAX_BODY_BYTES}b`
}));

app.use(express.urlencoded({
  extended: true,
  limit: `${MAX_BODY_BYTES}b`
}));

// ======================================================
// In-memory state
// ======================================================

const devices = new Map();

/*
  devices map shape:

  deviceId -> {
    deviceId,
    token,
    lastSeen,
    firmware,
    online,
    pending: [],
    responses: Map(requestId -> responseObject),
    stats: {
      polls,
      requests,
      responses,
      authErrors,
      timeouts
    }
  }
*/

function nowIso() {
  return new Date().toISOString();
}

function getOrCreateDevice(deviceId) {
  if (!devices.has(deviceId)) {
    devices.set(deviceId, {
      deviceId,
      token: DEFAULT_DEVICE_TOKEN,
      lastSeen: null,
      firmware: '',
      online: false,
      pending: [],
      responses: new Map(),
      stats: {
        polls: 0,
        requests: 0,
        responses: 0,
        authErrors: 0,
        timeouts: 0
      }
    });
  }

  return devices.get(deviceId);
}

function normalizePath(path) {
  if (!path || typeof path !== 'string') return '/';
  if (!path.startsWith('/')) return `/${path}`;
  return path;
}

function isDeviceOnline(device) {
  if (!device.lastSeen) return false;
  return Date.now() - device.lastSeen.getTime() < 45000;
}

function htmlEscape(value) {
  return String(value ?? '')
    .replaceAll('&', '&amp;')
    .replaceAll('<', '&lt;')
    .replaceAll('>', '&gt;')
    .replaceAll('"', '&quot;')
    .replaceAll("'", '&#39;');
}

function getDeviceAuth(req) {
  const deviceId = String(req.header('X-Device-Id') || req.query.deviceId || req.body.deviceId || '').trim();
  const token = String(req.header('X-Device-Token') || req.query.token || req.body.token || '').trim();

  return { deviceId, token };
}

function requireDeviceAuth(req, res, next) {
  const { deviceId, token } = getDeviceAuth(req);

  if (!deviceId) {
    return res.status(401).json({
      error: 'missing_device_id'
    });
  }

  const device = getOrCreateDevice(deviceId);

  if (!token || token !== device.token) {
    device.stats.authErrors++;
    return res.status(401).json({
      error: 'invalid_device_token'
    });
  }

  req.device = device;
  next();
}

function requireAdmin(req, res, next) {
  const token = String(req.header('X-Admin-Token') || req.query.adminToken || '').trim();

  if (!token || token !== ADMIN_TOKEN) {
    return res.status(401).send('Unauthorized');
  }

  next();
}

function cleanupOldResponses() {
  const limitMs = 120000;
  const now = Date.now();

  for (const device of devices.values()) {
    for (const [requestId, response] of device.responses.entries()) {
      if (now - response.createdAt > limitMs) {
        device.responses.delete(requestId);
      }
    }

    device.pending = device.pending.filter((request) => {
      const keep = now - request.createdAt < limitMs;
      if (!keep) device.stats.timeouts++;
      return keep;
    });
  }
}

setInterval(cleanupOldResponses, 30000);

// ======================================================
// Device API
// ======================================================

app.get('/api/device/poll', requireDeviceAuth, (req, res) => {
  const device = req.device;

  device.lastSeen = new Date();
  device.online = true;
  device.firmware = String(req.query.fw || device.firmware || '');
  device.stats.polls++;

  if (device.pending.length === 0) {
    return res.status(204).send('');
  }

  const request = device.pending.shift();

  return res.status(200).json({
    hasRequest: true,
    requestId: request.requestId,
    method: request.method,
    path: request.path,
    body: request.body || ''
  });
});

app.post('/api/device/response', requireDeviceAuth, (req, res) => {
  const device = req.device;

  const requestId = String(req.body.requestId || '').trim();

  if (!requestId) {
    return res.status(400).json({
      error: 'missing_request_id'
    });
  }

  const statusCode = Number(req.body.statusCode || 200);
  const contentType = String(req.body.contentType || 'text/plain');
  const body = String(req.body.body || '');

  device.responses.set(requestId, {
    requestId,
    statusCode,
    contentType,
    body,
    createdAt: Date.now()
  });

  device.lastSeen = new Date();
  device.online = true;
  device.stats.responses++;

  return res.status(200).json({
    ok: true,
    requestId
  });
});

// ======================================================
// Browser remote proxy
// ======================================================

function queueRemoteRequest(device, method, path, body) {
  const requestId = uuidv4();

  const request = {
    requestId,
    method,
    path,
    body: body || '',
    createdAt: Date.now()
  };

  device.pending.push(request);
  device.stats.requests++;

  return requestId;
}

function waitForResponse(device, requestId, timeoutMs) {
  return new Promise((resolve) => {
    const startedAt = Date.now();

    const timer = setInterval(() => {
      const response = device.responses.get(requestId);

      if (response) {
        clearInterval(timer);
        device.responses.delete(requestId);
        resolve(response);
        return;
      }

      if (Date.now() - startedAt > timeoutMs) {
        clearInterval(timer);
        device.stats.timeouts++;
        resolve(null);
      }
    }, 200);
  });
}

async function handleRemoteDeviceRequest(req, res) {
  const deviceId = String(req.params.deviceId || '').trim();
  const remotePath = normalizePath(req.params[0] || '/');

  const device = getOrCreateDevice(deviceId);

  if (!isDeviceOnline(device)) {
    return res.status(503).send(renderErrorPage(
      'Device offline',
      `Device ${htmlEscape(deviceId)} is not currently connected to the tunnel server.`
    ));
  }

  const method = req.method.toUpperCase();

  let body = '';
  if (method === 'POST') {
    body = JSON.stringify(req.body || {});
  }

  const requestId = queueRemoteRequest(device, method, remotePath, body);
  const response = await waitForResponse(device, requestId, REQUEST_TIMEOUT_MS);

  if (!response) {
    return res.status(504).send(renderErrorPage(
      'Tunnel timeout',
      `No response from device ${htmlEscape(deviceId)} for ${htmlEscape(remotePath)}.`
    ));
  }

  res.status(response.statusCode || 200);
  res.type(response.contentType || 'text/plain');
  return res.send(response.body || '');
}

app.get('/device/:deviceId', (req, res) => {
  req.params[0] = '/';
  return handleRemoteDeviceRequest(req, res);
});

app.get('/device/:deviceId/', (req, res) => {
  req.params[0] = '/';
  return handleRemoteDeviceRequest(req, res);
});

app.get('/device/:deviceId/*', handleRemoteDeviceRequest);
app.post('/device/:deviceId/*', handleRemoteDeviceRequest);

// ======================================================
// Admin / status pages
// ======================================================

function renderErrorPage(title, message) {
  return `
<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>${htmlEscape(title)}</title>
  <style>
    body{margin:0;background:#020617;color:#e5e7eb;font-family:Arial,Helvetica,sans-serif;padding:24px}
    .box{max-width:760px;background:#111827;border:1px solid #334155;border-radius:16px;padding:18px}
    a{color:#38bdf8}
  </style>
</head>
<body>
  <div class="box">
    <h2>${htmlEscape(title)}</h2>
    <p>${htmlEscape(message)}</p>
    <p><a href="/">Server home</a></p>
  </div>
</body>
</html>`;
}

function renderHomePage() {
  const rows = [];

  for (const device of devices.values()) {
    rows.push(`
      <tr>
        <td>${htmlEscape(device.deviceId)}</td>
        <td>${isDeviceOnline(device) ? 'online' : 'offline'}</td>
        <td>${device.lastSeen ? htmlEscape(device.lastSeen.toISOString()) : '-'}</td>
        <td>${htmlEscape(device.firmware || '-')}</td>
        <td>${device.pending.length}</td>
        <td><a href="/device/${encodeURIComponent(device.deviceId)}/">open</a></td>
      </tr>
    `);
  }

  return `
<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>SolarLink Remote WebUI Tunnel</title>
  <style>
    body{margin:0;background:#020617;color:#e5e7eb;font-family:Arial,Helvetica,sans-serif}
    header{padding:20px;background:#0f172a;border-bottom:1px solid #334155}
    main{max-width:1100px;margin:0 auto;padding:18px}
    .card{background:#111827;border:1px solid #334155;border-radius:16px;padding:16px;margin:14px 0}
    table{width:100%;border-collapse:collapse}
    th,td{border-bottom:1px solid #334155;padding:10px;text-align:left;font-size:14px}
    th{color:#94a3b8}
    a{color:#38bdf8;text-decoration:none;font-weight:700}
    code{background:#020617;border:1px solid #334155;border-radius:8px;padding:2px 6px}
  </style>
</head>
<body>
  <header>
    <h2 style="margin:0">SolarLink Remote WebUI Tunnel</h2>
    <div style="color:#94a3b8;margin-top:4px">POC server · ${htmlEscape(nowIso())}</div>
  </header>

  <main>
    <div class="card">
      <h3>Server status</h3>
      <p>Running on port <code>${PORT}</code></p>
      <p>Public base URL: <code>${htmlEscape(PUBLIC_BASE_URL || '(not set)')}</code></p>
      <p>Devices in memory: <code>${devices.size}</code></p>
    </div>

    <div class="card">
      <h3>Devices</h3>
      <table>
        <thead>
          <tr>
            <th>Device ID</th>
            <th>Status</th>
            <th>Last seen</th>
            <th>Firmware</th>
            <th>Pending</th>
            <th>Open</th>
          </tr>
        </thead>
        <tbody>
          ${rows.length ? rows.join('\n') : '<tr><td colspan="6">No devices connected yet.</td></tr>'}
        </tbody>
      </table>
    </div>

    <div class="card">
      <h3>ESP32 wizard values</h3>
      <p>Tunnel server URL:</p>
      <p><code>${htmlEscape(PUBLIC_BASE_URL || `http://SERVER-IP:${PORT}`)}</code></p>
      <p>Default device token:</p>
      <p><code>${htmlEscape(DEFAULT_DEVICE_TOKEN)}</code></p>
    </div>
  </main>
</body>
</html>`;
}

app.get('/', (req, res) => {
  res.type('html').send(renderHomePage());
});

app.get('/api/admin/devices', requireAdmin, (req, res) => {
  const out = [];

  for (const device of devices.values()) {
    out.push({
      deviceId: device.deviceId,
      online: isDeviceOnline(device),
      lastSeen: device.lastSeen,
      firmware: device.firmware,
      pending: device.pending.length,
      stats: device.stats
    });
  }

  res.json({
    ok: true,
    devices: out
  });
});

app.post('/api/admin/device/:deviceId/token', requireAdmin, (req, res) => {
  const deviceId = String(req.params.deviceId || '').trim();
  const token = String(req.body.token || '').trim();

  if (!deviceId || !token) {
    return res.status(400).json({
      error: 'missing_device_id_or_token'
    });
  }

  const device = getOrCreateDevice(deviceId);
  device.token = token;

  res.json({
    ok: true,
    deviceId
  });
});

app.get('/health', (req, res) => {
  res.json({
    ok: true,
    time: nowIso(),
    devices: devices.size
  });
});

// ======================================================
// Start
// ======================================================

app.listen(PORT, '0.0.0.0', () => {
  console.log(`SolarLink tunnel server listening on port ${PORT}`);
  console.log(`Default device token: ${DEFAULT_DEVICE_TOKEN}`);
  console.log(`Admin token: ${ADMIN_TOKEN}`);
});
