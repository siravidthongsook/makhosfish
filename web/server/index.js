'use strict';

const http = require('node:http');
const fs = require('node:fs');
const path = require('node:path');
const { WebSocketServer } = require('ws');
const { Engine } = require('./engine');

const ROOT = path.resolve(__dirname, '..', '..');            // repo root
const PUBLIC_DIR = path.resolve(__dirname, '..', 'public');
const BIN = process.env.MAKHOS_BIN || path.join(ROOT, 'build', 'makhosfish');
const PATTERN = process.env.MAKHOS_PATTERN || path.join(ROOT, 'data', 'patterns', 'thai_e2.mhpw');
const HOST = process.env.HOST || '127.0.0.1';
const PORT = parseInt(process.env.PORT || process.env.MAKHOS_PORT || '8787', 10);

const MIME = {
  '.html': 'text/html; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.svg': 'image/svg+xml',
  '.ico': 'image/x-icon',
};

function serveStatic(req, res) {
  let urlPath = decodeURIComponent((req.url || '/').split('?')[0]);
  if (urlPath === '/') urlPath = '/index.html';
  const filePath = path.join(PUBLIC_DIR, path.normalize(urlPath));
  if (!filePath.startsWith(PUBLIC_DIR + path.sep)) {
    res.writeHead(403).end('forbidden');
    return;
  }
  fs.readFile(filePath, (err, data) => {
    if (err) { res.writeHead(404).end('not found'); return; }
    res.writeHead(200, { 'Content-Type': MIME[path.extname(filePath)] || 'application/octet-stream' });
    res.end(data);
  });
}

// One WebSocket client = one engine session. The session owns the move list and
// rebuilds the engine position from it, but never decides legality itself -- it
// only asks the engine (`state`) and rejects moves outside the engine's list.
class Session {
  constructor(ws) {
    this.ws = ws;
    this.engine = new Engine({ bin: BIN, pattern: PATTERN });
    this.moves = [];
    this.startFen = null;
    this.side = 'W';
    this.opts = { RuleSet: 'thai', Eval: 'e2' };
    this.last = null;
    this.dirty = false; // engine restarted -> position must be rebuilt from `moves`

    this.engine.on('engine-message', (m) => this.send({ type: 'error', message: m }));
    this.engine.on('stderr', (m) => this.send({ type: 'error', message: 'stderr: ' + m.trim() }));
    this.engine.on('error', (e) => this.send({ type: 'error', message: String(e.message || e) }));
    this.engine.on('exit', () => { this.last = null; this.dirty = true; });
    this.engine.start();

    ws.on('message', (data) => this.onMessage(data));
    ws.on('close', () => this.engine.dispose());
  }

  send(obj) {
    if (this.ws.readyState === 1) this.ws.send(JSON.stringify(obj));
  }

  async onMessage(data) {
    let msg;
    try {
      msg = JSON.parse(data.toString());
    } catch {
      this.send({ type: 'error', message: 'invalid JSON' });
      return;
    }
    try {
      switch (msg.type) {
        case 'newgame': return await this.onNewGame(msg);
        case 'state': return await this.pushState();
        case 'move': return await this.onMove(msg);
        case 'undo': return await this.onUndo(msg);
        case 'go': return await this.onGo(msg);
        case 'stop': this.engine.stop(); return;
        default: this.send({ type: 'error', message: 'unknown message type: ' + msg.type });
      }    } catch (e) {
      this.send({ type: 'error', message: String(e.message || e) });
    }
  }

  async onNewGame(msg) {
    this.moves = [];
    this.startFen = msg.fen || null;
    if (msg.side === 'W' || msg.side === 'B') this.side = msg.side;
    this.opts = {
      RuleSet: msg.ruleset || 'thai',
      Eval: msg.eval || 'e2',
    };
    await this.engine.newGame(this.opts);
    this.dirty = false;
    await this.setPosition();
    await this.pushState();
  }

  async onMove(msg) {
    await this.ensureSynced();
    const m = typeof msg.m === 'string' ? msg.m.trim() : '';
    if (!this.last) this.last = await this.engine.state();
    if (!this.last.legal.includes(m)) {
      this.send({ type: 'error', message: 'illegal move: ' + m, legal: this.last.legal });
      return;
    }
    this.moves.push(m);
    await this.setPosition();
    await this.pushState();
  }

  async onUndo(msg) {
    await this.ensureSynced();
    const n = Math.max(0, Math.min(this.moves.length, msg.plies || 1));
    this.moves.splice(this.moves.length - n, n);
    await this.setPosition();
    await this.pushState();
  }

  async onGo(msg) {
    await this.ensureSynced();
    if (!this.last) this.last = await this.engine.state();
    if (!this.last.legal.length) { await this.pushState(); return; }

    const limits = {};
    if (msg.depth != null) limits.depth = msg.depth;
    else if (msg.movetime != null) limits.movetime = msg.movetime;
    else if (msg.nodes != null) limits.nodes = msg.nodes;

    this.send({ type: 'thinking', value: true });
    try {
      const res = await this.engine.go(limits, (info) => this.send({ type: 'info', ...info }));
      this.send({ type: 'bestmove', move: res.bestmove });
      if (res.bestmove && res.bestmove !== '(none)') {
        this.moves.push(res.bestmove);
        await this.setPosition();
      }
      await this.pushState();
    } finally {
      this.send({ type: 'thinking', value: false });
    }
  }

  // After a crash the child restarts at startpos; rebuild the game from the
  // move list before trusting any cached position/legal list.
  async ensureSynced() {
    if (!this.dirty) return;
    await this.setPosition();
    this.last = await this.engine.state();
    this.dirty = false;
  }

  async setPosition() {
    const p = this.startFen
      ? { fen: this.startFen, moves: this.moves }
      : { startpos: true, moves: this.moves };
    await this.engine.setPosition(p);
  }

  async pushState() {
    const st = await this.engine.state();
    this.last = st;
    this.send({
      type: 'state',
      fen: st.fen,
      legal: st.legal,
      stm: st.stm,
      moves: this.moves,
      side: this.side,
      opts: this.opts,
    });
  }
}

const server = http.createServer(serveStatic);
const wss = new WebSocketServer({ noServer: true });

server.on('upgrade', (req, socket, head) => {
  const { pathname } = new URL(req.url, `http://${HOST}`);
  if (pathname !== '/ws') { socket.destroy(); return; }
  wss.handleUpgrade(req, socket, head, (ws) => wss.emit('connection', ws, req));
});

wss.on('connection', (ws) => {
  ws.on('error', () => {});
  new Session(ws);
});

server.listen(PORT, HOST, () => {
  console.log(`MakHosFish web board: http://${HOST}:${PORT}`);
  console.log(`  engine : ${BIN}`);
  console.log(`  pattern: ${PATTERN}`);
});

module.exports = { server };
