// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Siravid Thongsook and MakHosFish contributors

'use strict';

// Board + WebSocket client. The engine is the sole authority on rules: this file
// only renders the FEN and legal-move list the bridge hands it, and sends back
// the notation the user clicks or drags. It never decides legality.

const $ = (sel) => document.querySelector(sel);

const model = {
  fen: '',
  legal: [],
  stm: '',
  moves: [],
  side: 'W',
  opts: {},
  thinking: false,
  analyzing: false,
  lines: new Map(),  // multipv (1..3) -> latest {depth, seldepth, score, nodes, time, pv}
};

const LINE_COLORS = { 1: '#5aa9ff', 2: '#d8a24a', 3: '#c96a5a' };
const MATE_CP = 30000 - 128;  // mirrors MATE_IN_MAX in src/search/tt.hpp

let ws = null;
let selected = null;          // external square of the selected piece
let targets = new Map();      // external target square -> move notation
let engineRequested = false;  // a `go` is in flight / awaited
let analysisSeq = 0;          // tags analyze/go requests; stale replies are dropped
let analyzeTimer = null;      // debounce for background hints on the human turn
let pinned = null;            // pinned multipv line for full-PV preview (or null)
let hoverLine = null;         // line emphasized by panel hover
let showEval = true;
let showArrows = true;
let showLines = true;

// ---- FEN ----------------------------------------------------------------

function parseFen(fen) {
  const pieces = {};
  const parts = fen.split(':');
  const lists = [['W', parts[1]], ['B', parts[2]]];
  for (const [color, list] of lists) {
    if (!list) continue;
    const body = list.replace(/^[WwBb]/, '');
    if (!body) continue;
    for (const item of body.split(',')) {
      if (!item) continue;
      let t = item;
      let king = false;
      if (t[0] === 'K' || t[0] === 'k') { king = true; t = t.slice(1); }
      const dash = t.indexOf('-');
      if (dash === -1) {
        const n = parseInt(t, 10);
        if (n >= 1 && n <= 32) pieces[n] = { color, king };
      } else {
        const a = parseInt(t.slice(0, dash), 10);
        const b = parseInt(t.slice(dash + 1), 10);
        for (let n = a; n <= b; n++) if (n >= 1 && n <= 32) pieces[n] = { color, king };
      }
    }
  }
  return pieces;
}

// ---- move notation ------------------------------------------------------

function parseMove(not) {
  const nums = not.split(/[x-]/).map((s) => parseInt(s, 10)).filter((n) => !Number.isNaN(n));
  return { from: nums[0], to: nums[nums.length - 1] };
}

// Every square a move (or full PV) passes through. The frozen notation already
// lists each landing square of a multi-capture, so the arrow polyline is exact.
function moveSquares(not) {
  return not.split(/[x-]/).map((s) => parseInt(s, 10)).filter((n) => n >= 1 && n <= 32);
}

// External square -> board pixel in the 800x800 arrow overlay. Inverse of
// `squareExt` above: row holds 4 dark squares, odd rows start on file 0.
function squareCenter(ext) {
  const r = (ext - 1) >> 2;
  const f = (ext - 1) & 3;
  const c = (r & 1) ? f * 2 : f * 2 + 1;
  return { x: (c + 0.5) * 100, y: (r + 0.5) * 100 };
}

// Engine scores are side-to-move-relative; flip for a White-relative view.
function whiteCp(score) {
  return model.stm === 'W' ? score : -score;
}

function isMate(score) {
  return Math.abs(score) >= MATE_CP;
}

function formatScore(score) {
  const w = whiteCp(score);
  if (isMate(score)) {
    const plies = Math.max(1, 30000 - Math.abs(score));
    const moves = Math.ceil(plies / 2);
    return (w > 0 ? '' : '-') + '#' + moves;
  }
  return (w >= 0 ? '+' : '') + (w / 100).toFixed(1);
}

function whiteShare(score) {
  const w = whiteCp(score);
  if (isMate(score)) return w > 0 ? 1 : 0;
  const p = 1 / (1 + Math.pow(10, -w / 400));
  return Math.min(0.97, Math.max(0.03, p));
}

function movesFrom(from) {
  const map = new Map();
  for (const not of model.legal) {
    const { from: f, to } = parseMove(not);
    if (f === from && !map.has(to)) map.set(to, not);
  }
  return map;
}

// Squares that own a legal move for the side to move.
function movableSet() {
  const s = new Set();
  for (const not of model.legal) s.add(parseMove(not).from);
  return s;
}

// Thai rules make capture compulsory (§3.4): when any capture exists the engine
// lists *only* captures, so a side-to-move piece with no move cannot be played.
function isForcedCapture() {
  return model.legal.length > 0 && model.legal.every((n) => n.includes('x'));
}

// ---- rendering ----------------------------------------------------------

function squareExt(r, c) {
  // Mirror of `render_board` in src/main.cpp: dark iff (r+c) is odd.
  const f = (r & 1) ? c / 2 : (c - 1) / 2;
  return r * 4 + f + 1; // external = internal + 1
}

function renderBoard() {
  const grid = $('#board');
  grid.innerHTML = '';
  const pieces = model.fen ? parseFen(model.fen) : {};
  const humanTurn = !!model.fen && model.stm === model.side;
  const movable = humanTurn ? movableSet() : new Set();
  const forced = humanTurn && isForcedCapture();

  for (let r = 0; r < 8; r++) {
    for (let c = 0; c < 8; c++) {
      const dark = ((r + c) & 1) === 1;
      const cell = document.createElement('div');
      cell.className = 'sq ' + (dark ? 'dark' : 'light');

      if (dark) {
        const ext = squareExt(r, c);
        cell.dataset.sq = String(ext);

        const num = document.createElement('span');
        num.className = 'num';
        num.textContent = ext;
        cell.appendChild(num);

        const p = pieces[ext];
        if (p) {
          const piece = document.createElement('div');
          piece.className = 'piece ' + (p.color === 'W' ? 'white' : 'black') + (p.king ? ' king' : '');
          // Only under a compulsory capture do we dim the pieces that cannot
          // take; elsewhere a blocked piece is just a normal (if doomed) choice.
          if (forced && p.color === model.stm) {
            piece.classList.add(movable.has(ext) ? 'must-capture' : 'invalid');
          }
          cell.appendChild(piece);
        }
        if (selected === ext) cell.classList.add('selected');
        if (targets.has(ext)) cell.classList.add('target');
      }
      grid.appendChild(cell);
    }
  }
}

function renderMoves() {
  const el = $('#movelist');
  el.innerHTML = '';
  model.moves.forEach((mv, i) => {
    const row = document.createElement('div');
    row.className = 'ply' + (i === model.moves.length - 1 ? ' latest' : '');
    const idx = document.createElement('span');
    idx.className = 'idx';
    idx.textContent = (Math.floor(i / 2) + 1) + (i % 2 === 0 ? '.' : '…');
    const m = document.createElement('span');
    m.className = 'mv';
    m.textContent = mv;
    row.append(idx, m);
    el.appendChild(row);
  });
  el.scrollTop = el.scrollHeight;
}

function renderStatus() {
  const banner = $('#banner');
  const status = $('#status');

  if (!model.fen) { status.textContent = 'connecting…'; banner.classList.add('hidden'); return; }

  if (model.legal.length === 0) {
    const winner = model.stm === 'W' ? 'Black (ดำ)' : 'White (ขาว)';
    banner.textContent = `${model.stm === 'W' ? 'White' : 'Black'} has no move → ${winner} wins`;
    banner.classList.remove('hidden');
    status.textContent = 'game over';
    return;
  }
  banner.classList.add('hidden');

  const human = model.stm === model.side;
  status.textContent = (model.thinking ? 'engine thinking…' : (human ? 'your move' : 'engine to move'))
    + ' · ' + (model.stm === 'W' ? 'White' : 'Black') + ' to move';
}

function renderInfo(info) {
  if (!info) { $('#info').textContent = ''; return; }
  const ms = info.time != null ? `${(info.time / 1000).toFixed(2)}s` : '—';
  const pv = info.pv && info.pv.length ? info.pv.slice(0, 6).join(' ') : '';
  $('#info').textContent =
    `depth ${info.depth}/${info.seldepth}  score ${info.score} cp\n` +
    `nodes ${info.nodes}  time ${ms}\n${pv}`;
}

function renderEval() {
  const bar = $('#evalbar');
  bar.classList.toggle('hidden', !showEval);
  if (!showEval) return;
  const best = model.lines.get(1);
  if (!best || !model.fen) {
    $('#evalwhite').style.height = '50%';
    $('#evallabel').textContent = '—';
    return;
  }
  $('#evalwhite').style.height = `${(whiteShare(best.score) * 100).toFixed(1)}%`;
  $('#evallabel').textContent = formatScore(best.score);
}

function renderLines() {
  const box = $('#linelist');
  box.classList.toggle('hidden', !showLines);
  $('#analyzing').classList.toggle('hidden', !model.analyzing);
  if (!showLines) return;
  box.innerHTML = '';
  if (model.lines.size === 0) {
    const d = document.createElement('div');
    d.className = 'line empty';
    d.textContent = model.analyzing || model.thinking ? 'reading the position…' : 'no engine lines yet';
    box.appendChild(d);
    return;
  }
  for (let k = 1; k <= 3; k++) {
    const ln = model.lines.get(k);
    if (!ln || !ln.pv.length) continue;
    const row = document.createElement('div');
    row.className = 'line' + (pinned === k ? ' pinned' : '');
    const dot = document.createElement('span');
    dot.className = 'dot';
    dot.style.background = LINE_COLORS[k];
    const sc = document.createElement('span');
    sc.className = 'sc';
    sc.textContent = formatScore(ln.score);
    const pv = document.createElement('span');
    pv.className = 'pv';
    pv.textContent = ln.pv.slice(0, 5).join(' ');
    const dp = document.createElement('span');
    dp.className = 'dp';
    dp.textContent = `d${ln.depth}`;
    row.append(dot, sc, pv, dp);
    row.title = pinned === k ? 'click to unpin' : 'click to preview this line';
    row.addEventListener('click', () => {
      pinned = (pinned === k) ? null : k;
      renderLines();
      renderArrows();
    });
    row.addEventListener('mouseenter', () => { hoverLine = k; renderArrows(); });
    row.addEventListener('mouseleave', () => { hoverLine = null; renderArrows(); });
    box.appendChild(row);
  }
}

const SVG_NS = 'http://www.w3.org/2000/svg';

function arrowMarker(defs, k) {
  const m = document.createElementNS(SVG_NS, 'marker');
  m.setAttribute('id', `ah${k}`);
  m.setAttribute('markerWidth', '4');
  m.setAttribute('markerHeight', '4');
  m.setAttribute('refX', '2.6');
  m.setAttribute('refY', '2');
  m.setAttribute('orient', 'auto');
  const p = document.createElementNS(SVG_NS, 'path');
  p.setAttribute('d', 'M0,0 L4,2 L0,4 z');
  p.setAttribute('fill', LINE_COLORS[k]);
  m.appendChild(p);
  defs.appendChild(m);
}

// Pull the arrow tip back so the head lands beside the piece, not under it.
function trimEnd(pts, back) {
  if (pts.length < 2) return pts;
  const a = pts[pts.length - 2];
  const b = pts[pts.length - 1];
  const dx = b.x - a.x;
  const dy = b.y - a.y;
  const len = Math.hypot(dx, dy) || 1;
  const t = Math.min(back, len * 0.45);
  return [...pts.slice(0, -1), { x: b.x - (dx / len) * t, y: b.y - (dy / len) * t }];
}

function drawPolyline(svg, k, squares, { width, opacity, dashed, head }) {
  if (squares.length < 2) return;
  const pts = trimEnd(squares.map(squareCenter), 30);
  const pl = document.createElementNS(SVG_NS, 'polyline');
  pl.setAttribute('points', pts.map((p) => `${p.x.toFixed(1)},${p.y.toFixed(1)}`).join(' '));
  pl.setAttribute('fill', 'none');
  pl.setAttribute('stroke', LINE_COLORS[k]);
  pl.setAttribute('stroke-width', String(width));
  pl.setAttribute('stroke-linecap', 'round');
  pl.setAttribute('stroke-linejoin', 'round');
  pl.setAttribute('opacity', String(opacity));
  if (dashed) pl.setAttribute('stroke-dasharray', '16 12');
  if (head) pl.setAttribute('marker-end', `url(#ah${k})`);
  svg.appendChild(pl);
}

function drawDot(svg, k, ext, label) {
  const { x, y } = squareCenter(ext);
  const c = document.createElementNS(SVG_NS, 'circle');
  c.setAttribute('cx', String(x));
  c.setAttribute('cy', String(y));
  c.setAttribute('r', label != null ? '22' : '10');
  c.setAttribute('fill', LINE_COLORS[k]);
  c.setAttribute('opacity', label != null ? '0.95' : '0.8');
  if (label == null) return svg.appendChild(c);
  svg.appendChild(c);
  const t = document.createElementNS(SVG_NS, 'text');
  t.setAttribute('x', String(x));
  t.setAttribute('y', String(y + 8));
  t.setAttribute('text-anchor', 'middle');
  t.setAttribute('font-size', '26');
  t.setAttribute('font-weight', '700');
  t.setAttribute('fill', '#14100d');
  t.textContent = String(label);
  svg.appendChild(t);
}

function renderArrows() {
  const svg = $('#arrows');
  svg.classList.toggle('hidden', !showArrows);
  svg.innerHTML = '';
  if (!showArrows || model.lines.size === 0) return;
  const defs = document.createElementNS(SVG_NS, 'defs');
  for (let k = 1; k <= 3; k++) arrowMarker(defs, k);
  svg.appendChild(defs);

  const keys = [1, 2, 3].filter((k) => model.lines.has(k));
  // Faint lines first so the pinned/hovered/best line paints on top.
  keys.sort((a, b) => {
    const rank = (k) => (pinned === k || hoverLine === k) ? 0 : k;
    return rank(b) - rank(a);
  });
  for (const k of keys) {
    const ln = model.lines.get(k);
    if (!ln || !ln.pv.length) continue;
    const focus = pinned === k || hoverLine === k;
    const base = k === 1 ? 0.9 : 0.5;
    drawPolyline(svg, k, moveSquares(ln.pv[0]), {
      width: focus ? 16 : 12,
      opacity: focus ? 1 : base,
      head: true,
    });
    if (pinned === k) {
      // Full-line preview: every later ply dashed, each stop numbered.
      ln.pv.slice(1, 6).forEach((mv, i) => {
        drawPolyline(svg, k, moveSquares(mv), {
          width: 7, opacity: Math.max(0.35, 0.85 - i * 0.12), dashed: true,
        });
      });
      let n = 0;
      for (const mv of ln.pv.slice(0, 6)) {
        for (const sq of moveSquares(mv).slice(1)) drawDot(svg, k, sq, ++n);
      }
    } else {
      const sqs = moveSquares(ln.pv[0]);
      drawDot(svg, k, sqs[sqs.length - 1], null);
    }
  }
}

function toast(message) {
  const el = document.createElement('div');
  el.className = 'toast';
  el.textContent = message;
  $('#toasts').appendChild(el);
  setTimeout(() => el.remove(), 3500);
}

// ---- interaction (click-to-move + drag-and-drop) ---------------------------
// Single delegated pointer handler: taps/clicks select or place, drags lift a
// ghost piece and drop it on a target. A press anywhere else clears selection.

let drag = null;

function cellFor(ext) {
  return document.querySelector(`#board .sq[data-sq="${ext}"]`);
}

function canSelect(ext) {
  if (!model.fen || model.thinking || model.stm !== model.side) return false;
  const p = parseFen(model.fen)[ext];
  if (!p || p.color !== model.stm) return false;
  // Under a compulsory capture, only the capturing pieces are selectable.
  return !isForcedCapture() || movableSet().has(ext);
}

function selectSquare(ext) {
  selected = ext;
  targets = movesFrom(ext);
  renderBoard();
}

function deselect() {
  if (selected === null && targets.size === 0) return;
  selected = null;
  targets = new Map();
  renderBoard();
}

function placeOn(to) {
  if (selected !== null && targets.has(to)) {
    const m = targets.get(to);
    selected = null;
    targets = new Map();
    stopSearch();  // kill the background hint so the move applies at once
    send({ type: 'move', m });
    return true;
  }
  return false;
}

function onPointerDown(e) {
  if (e.button !== undefined && e.button !== 0) return;
  if (!model.fen || model.thinking || model.stm !== model.side) return;
  const cell = e.target && e.target.closest ? e.target.closest('.sq[data-sq]') : null;
  const ext = cell ? Number(cell.dataset.sq) : null;

  if (ext != null && canSelect(ext)) {
    if (selected !== ext) selectSquare(ext);
    drag = { from: ext, x0: e.clientX, y0: e.clientY, moved: false, ghost: null };
    e.preventDefault();
  } else if (selected !== null && ext != null && targets.has(ext)) {
    drag = { from: selected, x0: e.clientX, y0: e.clientY, moved: false, ghost: null };
  } else {
    if (ext != null && isForcedCapture()) {
      const p = parseFen(model.fen)[ext];
      if (p && p.color === model.stm) toast('capture is compulsory — move a highlighted piece');
    }
    deselect();
    drag = { from: null, x0: e.clientX, y0: e.clientY, moved: false, ghost: null };
  }
}

function startGhost(d, e) {
  const src = cellFor(d.from);
  const piece = src && src.querySelector('.piece');
  if (!piece) return;
  const r = piece.getBoundingClientRect();
  const g = piece.cloneNode(true);
  g.classList.remove('must-capture');
  g.classList.add('ghost');
  g.style.width = r.width + 'px';
  g.style.height = r.height + 'px';
  d.w = r.width;
  d.h = r.height;
  moveGhost(d, e);
  document.body.appendChild(g);
  d.ghost = g;
  piece.style.visibility = 'hidden';
}

function moveGhost(d, e) {
  if (d.ghost) {
    d.ghost.style.left = (e.clientX - d.w / 2) + 'px';
    d.ghost.style.top = (e.clientY - d.h / 2) + 'px';
  }
}

function endGhost(d) {
  if (d.ghost) { d.ghost.remove(); d.ghost = null; }
  setHover(null);
}

function setHover(ext) {
  if (setHover.cur === ext) return;
  if (setHover.cur != null) {
    const c = cellFor(setHover.cur);
    if (c) c.classList.remove('hover');
  }
  setHover.cur = ext;
  if (ext != null && targets.has(ext)) {
    const c = cellFor(ext);
    if (c) c.classList.add('hover');
  }
}

function hoverAt(x, y) {
  const el = document.elementFromPoint(x, y);
  const cell = el && el.closest ? el.closest('.sq[data-sq]') : null;
  setHover(cell ? Number(cell.dataset.sq) : null);
}

function onPointerMove(e) {
  if (!drag || drag.from == null) return;
  if (!drag.moved) {
    if (Math.hypot(e.clientX - drag.x0, e.clientY - drag.y0) < 5) return;
    drag.moved = true;
    startGhost(drag, e);
  }
  moveGhost(drag, e);
  hoverAt(e.clientX, e.clientY);
}

function onPointerUp(e) {
  const d = drag;
  drag = null;
  if (!d || d.from == null) return;
  endGhost(d);
  const el = document.elementFromPoint(e.clientX, e.clientY);
  const cell = el && el.closest ? el.closest('.sq[data-sq]') : null;
  const to = cell ? Number(cell.dataset.sq) : null;
  if (to != null && to !== d.from && placeOn(to)) return;
  if (d.moved) renderBoard(); // dropped nowhere legal -> snap back
}

function cancelDrag() {
  if (!drag) return;
  endGhost(drag);
  drag = null;
  renderBoard();
}

// Slide the moved piece from its source square to its landing square. The board
// is already re-rendered, so measure the (now empty) source cell and animate
// the piece sitting on the destination cell across the gap.
function animateMove(fromExt, toExt) {
  if (fromExt === toExt) return;
  const fromCell = cellFor(fromExt);
  const toCell = cellFor(toExt);
  if (!fromCell || !toCell) return;
  const piece = toCell.querySelector('.piece');
  if (!piece || !piece.animate) return;
  const a = fromCell.getBoundingClientRect();
  const b = toCell.getBoundingClientRect();
  const dx = (a.left + a.width / 2) - (b.left + b.width / 2);
  const dy = (a.top + a.height / 2) - (b.top + b.height / 2);
  piece.animate(
    [{ transform: `translate(${dx}px, ${dy}px) scale(1.08)` }, { transform: 'translate(0, 0)' }],
    { duration: 280, easing: 'cubic-bezier(.25,.8,.3,1)' }
  );
}

// When capture is compulsory and only one piece can make it, select it so its
// landing squares are shown without an extra click.
function autoSelectForced() {
  if (model.stm !== model.side || !isForcedCapture()) return;
  const movable = movableSet();
  if (movable.size === 1) {
    selected = [...movable][0];
    targets = movesFrom(selected);
  }
}

function maybeEngineMove() {
  if (!model.fen || model.thinking || engineRequested) return;
  if (model.legal.length === 0 || model.stm === model.side) return;
  engineRequested = true;
  send({ type: 'go', ...currentLimits(), multipv: 3, seq: ++analysisSeq });
}

// Background hint while the human thinks: streams top-3 infos without moving.
function maybeAnalyze() {
  clearTimeout(analyzeTimer);
  if (!model.fen || model.thinking || model.legal.length === 0) return;
  if (model.stm !== model.side) return;
  if (!showLines && !showEval && !showArrows) return;
  analyzeTimer = setTimeout(() => {
    send({ type: 'analyze', ...currentLimits(), multipv: 3, seq: ++analysisSeq });
  }, 350);
}

function currentLimits() {
  const [kind, value] = $('#difficulty').value.split(':');
  return kind === 'movetime' ? { movetime: parseInt(value, 10) } : { depth: parseInt(value, 10) };
}

// Interrupt a running hint (or engine search) before a state-changing command
// so the engine queue never sits behind a stale analysis.
function stopSearch() {
  clearTimeout(analyzeTimer);
  analysisSeq++;  // drop late infos from the killed search
  send({ type: 'stop' });
}

// ---- protocol -----------------------------------------------------------

function send(obj) {
  if (ws && ws.readyState === WebSocket.OPEN) ws.send(JSON.stringify(obj));
}

function handle(msg) {
  switch (msg.type) {
    case 'state': {
      const prevCount = model.moves.length;
      const hadFen = !!model.fen;
      Object.assign(model, {
        fen: msg.fen, legal: msg.legal, stm: msg.stm,
        moves: msg.moves || [], side: msg.side || model.side, opts: msg.opts || {},
      });
      const justMoved = hadFen && model.moves.length === prevCount + 1 && model.moves.length > 0;
      selected = null;
      targets = new Map();
      // A new position invalidates every line; in-flight searches are dropped
      // by the seq bump (their replies arrive stale).
      analysisSeq++;
      model.lines.clear();
      pinned = null;
      autoSelectForced();
      renderBoard(); renderMoves(); renderStatus();
      renderEval(); renderLines(); renderArrows();
      if (justMoved) {
        const { from, to } = parseMove(model.moves[model.moves.length - 1]);
        animateMove(from, to);
      }
      if (model.stm === model.side) engineRequested = false;
      maybeEngineMove();
      maybeAnalyze();
      break;
    }
    case 'info': {
      if (msg.seq != null && msg.seq !== analysisSeq) break; // stale search
      const k = (msg.multipv >= 1 && msg.multipv <= 3) ? msg.multipv : 1;
      if (k === 1 && msg.pv && msg.pv.length) {
        // A new iteration restarts the line set; extra lines join below.
        if (msg.depth !== (model.lines.get(1) || {}).depth) {
          model.lines.clear();
          if (pinned != null && pinned > 1) pinned = null;
        }
      }
      model.lines.set(k, msg);
      if (k === 1) renderInfo(msg);
      renderEval(); renderLines(); renderArrows();
      break;
    }
    case 'thinking':
      if (msg.analysis) {
        model.analyzing = msg.value;
        renderLines();
      } else {
        model.thinking = msg.value;
        if (msg.value) renderInfo(null);
        renderStatus();
      }
      break;
    case 'analysis': break; // infos already painted; position never changes
    case 'error': toast(msg.message); break;
    case 'bestmove': break; // reflected by the following `state`
    default: break;
  }
}

function newGame() {
  engineRequested = false;
  stopSearch();
  const side = $('#side').value;
  send({
    type: 'newgame',
    side,
    ruleset: 'thai',
    eval: $('#eval').value,
  });
}

function connect() {
  const proto = location.protocol === 'https:' ? 'wss' : 'ws';
  ws = new WebSocket(`${proto}://${location.host}/ws`);
  ws.onopen = () => newGame();
  ws.onmessage = (ev) => {
    try { handle(JSON.parse(ev.data)); } catch (e) { toast('bad message: ' + e.message); }
  };
  ws.onclose = () => setTimeout(connect, 1000);
  ws.onerror = () => ws.close();
}

$('#newgame').addEventListener('click', newGame);
$('#undo').addEventListener('click', () => {
  if (!model.moves.length) return;
  const plies = model.stm === model.side ? Math.min(2, model.moves.length) : 1;
  engineRequested = false;
  stopSearch();
  send({ type: 'undo', plies });
});
$('#side').addEventListener('change', newGame);
$('#eval').addEventListener('change', newGame);
$('#tgl-eval').addEventListener('change', (e) => {
  showEval = e.target.checked;
  renderEval();
  maybeAnalyze();
});
$('#tgl-arrows').addEventListener('change', (e) => {
  showArrows = e.target.checked;
  renderArrows();
  maybeAnalyze();
});
$('#tgl-lines').addEventListener('change', (e) => {
  showLines = e.target.checked;
  renderLines();
  maybeAnalyze();
});

$('#board').addEventListener('pointerdown', onPointerDown);
window.addEventListener('pointermove', onPointerMove);
window.addEventListener('pointerup', onPointerUp);
window.addEventListener('pointercancel', cancelDrag);

renderStatus();
renderEval(); renderLines(); renderArrows();
connect();
