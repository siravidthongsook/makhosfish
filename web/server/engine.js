// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Siravid Thongsook and MakHosFish contributors

'use strict';

const { spawn } = require('node:child_process');
const readline = require('node:readline');
const { EventEmitter } = require('node:events');

const DEFAULT_TIMEOUT_MS = 30000;
const SEARCH_TIMEOUT_MS = 10 * 60 * 1000; // a search is bounded by depth/movetime, but never hang forever

// Parse a TCI `info` line into a structured object. `multipv` is the 1-indexed
// engine line number (defaults to 1 for older binaries that omit the token).
function parseInfo(line) {
  const t = line.split(/\s+/);
  const o = { depth: 0, seldepth: 0, multipv: 1, score: 0, nodes: 0, time: 0, pv: [] };
  for (let i = 1; i < t.length; i++) {
    switch (t[i]) {
      case 'depth': o.depth = +t[++i]; break;
      case 'seldepth': o.seldepth = +t[++i]; break;
      case 'multipv': o.multipv = +t[++i]; break;
      case 'score': i += 2; o.score = +t[i]; break;
      case 'nodes': o.nodes = +t[++i]; break;
      case 'time': o.time = +t[++i]; break;
      case 'pv': o.pv = t.slice(i + 1); i = t.length; break;
      default: break;
    }
  }
  if (!Number.isFinite(o.multipv) || o.multipv < 1) o.multipv = 1;
  return o;
}

// One `makhosfish tci` child. Commands are serialized through a promise queue so
// `position`/`state` never race a running search; `stop()` deliberately bypasses
// the queue because it is what *ends* the search. Lines are framed with readline.
class Engine extends EventEmitter {
  constructor({ bin, pattern } = {}) {
    super();
    this.bin = bin;
    this.pattern = pattern;
    this._opts = {};          // remembered setoptions, re-sent after a restart
    this.proc = null;
    this._rl = null;
    this._waiter = null;
    this._queue = Promise.resolve();
  }

  start() {
    if (this._alive()) return;
    const args = [];
    if (this.pattern) args.push('--pattern', this.pattern);
    args.push('tci');
    this.proc = spawn(this.bin, args, { stdio: ['pipe', 'pipe', 'pipe'] });
    this._rl = readline.createInterface({ input: this.proc.stdout });
    this._rl.on('line', (l) => this._onLine(l));
    this.proc.on('exit', (code, signal) => {
      this._failWaiter(new Error(`engine exited (code ${code}, signal ${signal})`));
      this.emit('exit', { code, signal });
    });
    this.proc.on('error', (err) => {
      this._failWaiter(err);
      this.emit('error', err);
    });
    this.proc.stderr.on('data', (d) => this.emit('stderr', d.toString()));
    this.send('tci');
    this._applyOpts(); // re-establish RuleSet/Eval/... after a crash restart
  }

  _alive() {
    return !!this.proc && this.proc.exitCode === null && !this.proc.killed;
  }

  send(line) {
    if (!this._alive()) throw new Error('engine not running');
    this.proc.stdin.write(line + '\n');
  }

  // `stop` ends an in-flight search, so it must not wait behind it in the queue.
  stop() {
    if (this._alive()) this.send('stop');
  }

  dispose() {
    if (!this.proc) return;
    this._failWaiter(new Error('engine disposed'));
    try { this.send('quit'); } catch { /* already gone */ }
    const p = this.proc;
    setTimeout(() => { if (p.exitCode === null) p.kill('SIGKILL'); }, 200);
  }

  // --- command plumbing ---------------------------------------------------

  _run(task) {
    const p = this._queue.then(task);
    this._queue = p.then(() => {}, () => {}); // keep the chain alive on rejection
    return p;
  }

  _applyOpts() {
    for (const [name, value] of Object.entries(this._opts))
      this.send(`setoption name ${name} value ${value}`);
  }

  configure(opts) {
    this._opts = { ...this._opts, ...opts };
    if (this._alive()) this._applyOpts();
  }

  // Register a line waiter *before* sending, so a fast reply is never missed.
  _command(line, until, onLine, timeout = DEFAULT_TIMEOUT_MS) {
    if (!this._alive()) this.start();
    const wait = this._wait(until, onLine, timeout);
    this.send(line);
    return wait;
  }

  _wait(until, onLine, timeoutMs) {
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this._waiter = null;
        reject(new Error('engine timeout'));
      }, timeoutMs);
      this._waiter = {
        lines: [],
        done: (v) => { clearTimeout(timer); resolve(v); },
        fail: (e) => { clearTimeout(timer); reject(e); },
      };
      this._until = until;
      this._onLineCb = onLine;
    });
  }

  _failWaiter(err) {
    const w = this._waiter;
    if (!w) return;
    this._waiter = null;
    w.fail(err);
  }

  _onLine(line) {
    if (line.startsWith('info string ')) this.emit('engine-message', line.slice(12));
    const w = this._waiter;
    if (!w) return;
    w.lines.push(line);
    if (this._onLineCb) this._onLineCb(line);
    if (this._until(w.lines, line)) {
      this._waiter = null;
      w.done(w.lines);
    }
  }

  // --- TCI operations -----------------------------------------------------

  sync() {
    return this._run(() => this._command('isready', (lines) => lines.includes('readyok')));
  }

  state() {
    return this._run(() =>
      this._command('state', (lines) => lines.some((l) => l.startsWith('stm ')))
    ).then((lines) => {
      const out = { fen: '', legal: [], stm: '' };
      for (const l of lines) {
        if (l.startsWith('fen ')) out.fen = l.slice(4).trim();
        else if (l === 'moves' || l.startsWith('moves ')) out.legal = l.slice(5).trim().split(/\s+/).filter(Boolean);
        else if (l.startsWith('stm ')) out.stm = l.slice(4).trim();
      }
      return out;
    });
  }

  setPosition({ startpos = true, fen = null, moves = [] } = {}) {
    return this._run(async () => {
      let line = 'position ' + (fen ? 'fen ' + fen : 'startpos');
      if (moves.length) line += ' moves ' + moves.join(' ');
      this.send(line);
      await this._command('isready', (lines) => lines.includes('readyok'));
    });
  }

  newGame(opts = {}) {
    return this._run(async () => {
      this.configure(opts);
      this.send('tcinewgame');
      await this._command('isready', (lines) => lines.includes('readyok'));
    });
  }

  go(limits = {}, onInfo) {
    return this._run(() => {
      const parts = ['go'];
      if (limits.depth != null) parts.push('depth', String(limits.depth));
      else if (limits.movetime != null) parts.push('movetime', String(limits.movetime));
      else if (limits.nodes != null) parts.push('nodes', String(limits.nodes));
      else parts.push('depth', '8');
      if (limits.multipv != null) parts.push('multipv', String(limits.multipv));
      const line = parts.join(' ');
      return this._command(
        line,
        (lines, l) => l.startsWith('bestmove'),
        (l) => { if (onInfo && l.startsWith('info ')) onInfo(parseInfo(l)); },
        SEARCH_TIMEOUT_MS
      );
    }).then((lines) => {
      const bm = lines.find((l) => l.startsWith('bestmove '));
      return { bestmove: bm ? bm.slice(9).trim() : '(none)' };
    });
  }
}

module.exports = { Engine, parseInfo };
