// Minimal proxy: browser -> this server -> Ollama /api/generate
// Run: node ai-eval-server.js
// Then open the HTML in your browser while this is running.

const http = require('http');
const fs = require('fs');
const path = require('path');
const { spawn } = require('child_process');

const OLLAMA_URL = process.env.OLLAMA_URL || 'http://192.168.0.135:11434';
const DEFAULT_MODEL = process.env.EVAL_MODEL || 'minimax-m3:cloud';
const PORT = Number(process.env.PORT || 5179);
const STATIC_DIR = __dirname; // serve the HTML from the same folder as this script

// Start (or no-op) the Ollama daemon — fall back to letting the user start it manually if the binary is missing/broken.
try {
  const ollama = spawn('ollama', ['serve'], { detached: true, stdio: 'ignore' });
  ollama.unref();
} catch (_) { /* ignore */ }

const CORS = {
  'Access-Control-Allow-Origin': '*',
  'Access-Control-Allow-Methods': 'GET, POST, OPTIONS',
  'Access-Control-Allow-Headers': 'Content-Type',
};

function readBody(req) {
  return new Promise((resolve, reject) => {
    let raw = '';
    req.on('data', (c) => (raw += c));
    req.on('end', () => resolve(raw));
    req.on('error', reject);
  });
}

function proxyToOllama(path, body) {
  return new Promise((resolve, reject) => {
    const url = new URL(path, OLLAMA_URL);
    const opts = {
      method: 'POST',
      hostname: url.hostname,
      port: url.port || 80,
      path: url.pathname + url.search,
      headers: { 'Content-Type': 'application/json' },
    };
    const req = http.request(opts, (res) => {
      let raw = '';
      res.on('data', (c) => (raw += c));
      res.on('end', () => resolve({ status: res.statusCode || 200, body: raw }));
    });
    req.on('error', reject);
    if (body) req.write(body);
    req.end();
  });
}

const server = http.createServer(async (req, res) => {
  if (req.method === 'OPTIONS') {
    Object.entries(CORS).forEach(([k, v]) => res.setHeader(k, v));
    res.writeHead(204).end();
    return;
  }

  // GET /  -> serve ai-interview-prep.html so the page and proxy share an origin
  if (req.method === 'GET' && (req.url === '/' || req.url === '/index.html')) {
    Object.entries(CORS).forEach(([k, v]) => res.setHeader(k, v));
    res.setHeader('Content-Type', 'text/plain');
    res.writeHead(200);
    res.end(
      'ai-eval-server is running.\n' +
      'Open the interview prep page at:  http://127.0.0.1:' + PORT + '/ai-interview-prep.html\n' +
      'POST JSON to /api/evaluate with { "model": "...", "prompt": "..." }.\n' +
      'Default model: ' + DEFAULT_MODEL + '\n'
    );
    return;
  }

  // GET /<file>  -> serve files from the same directory (so the HTML page is reachable over HTTP)
  if (req.method === 'GET' && req.url !== '/api/evaluate') {
    try {
      const urlPath = decodeURIComponent(req.url.split('?')[0]);
      const safe = path.normalize(urlPath).replace(/^(\.\.[\/\\])+/, '');
      const filePath = path.join(STATIC_DIR, safe);
      if (!filePath.startsWith(STATIC_DIR)) {
        res.writeHead(403); return res.end('forbidden');
      }
      const data = fs.readFileSync(filePath);
      const ext = path.extname(filePath).toLowerCase();
      const type = ext === '.html' ? 'text/html; charset=utf-8'
        : ext === '.js' ? 'application/javascript; charset=utf-8'
          : ext === '.css' ? 'text/css; charset=utf-8'
            : ext === '.json' ? 'application/json; charset=utf-8'
              : 'application/octet-stream';
      Object.entries(CORS).forEach(([k, v]) => res.setHeader(k, v));
      res.setHeader('Content-Type', type);
      res.writeHead(200);
      return res.end(data);
    } catch (_) {
      // fall through to 404
    }
  }

  if (req.method === 'POST' && req.url === '/api/evaluate') {
    let raw;
    try { raw = await readBody(req); } catch (e) {
      Object.entries(CORS).forEach(([k, v]) => res.setHeader(k, v));
      res.writeHead(400).end(JSON.stringify({ error: 'bad request body' }));
      return;
    }

    let payload;
    try { payload = JSON.parse(raw); } catch (e) {
      Object.entries(CORS).forEach(([k, v]) => res.setHeader(k, v));
      res.writeHead(400).end(JSON.stringify({ error: 'invalid JSON' }));
      return;
    }

    const model = payload.model || DEFAULT_MODEL;
    const prompt = payload.prompt || '';

    const ollamaBody = JSON.stringify({
      model,
      prompt,
      stream: false,
      options: { temperature: 0.2 },
    });

    try {
      const r = await proxyToOllama('/api/generate', ollamaBody);
      Object.entries(CORS).forEach(([k, v]) => res.setHeader(k, v));
      res.setHeader('Content-Type', 'application/json');
      // Forward Ollama's response body. It is already JSON.
      res.writeHead(r.status).end(r.body);
    } catch (e) {
      Object.entries(CORS).forEach(([k, v]) => res.setHeader(k, v));
      res.writeHead(502).end(JSON.stringify({ error: 'ollama unreachable: ' + (e.message || String(e)) }));
    }
    return;
  }

  Object.entries(CORS).forEach(([k, v]) => res.setHeader(k, v));
  res.writeHead(404).end(JSON.stringify({ error: 'not found' }));
});

server.listen(PORT, '127.0.0.1', () => {
  console.log('ai-eval-server listening on http://127.0.0.1:' + PORT);
  console.log('Open the page at:   http://127.0.0.1:' + PORT + '/ai-interview-prep.html');
  console.log('Proxying to ' + OLLAMA_URL + ' with default model ' + DEFAULT_MODEL);
});
