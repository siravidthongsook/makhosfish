'use strict';

// Board + WebSocket client. The engine is the sole authority on rules: this file
// only renders the FEN and legal-move list the bridge hands it, and sends back
// the notation the user clicked. It never decides legality.

const $ = (sel) => document.querySelector(sel);

const model = {
  fen: '',
  legal: [],
  stm: '',
  moves: [],
  side: 'W',
  opts: {},
  thinking: false,
};

let ws = null;
let selected = null;          // external square of the selected piece
let targets = new Map();      // external target square -> move notation
let engineRequested = false;  // a `go` is in flight / awaited

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

        cell.addEventListener('click', () => onSquareClick(ext));
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

function toast(message) {
  const el = document.createElement('div');
  el.className = 'toast';
  el.textContent = message;
  $('#toasts').appendChild(el);
  setTimeout(() => el.remove(), 3500);
}

// ---- interaction --------------------------------------------------------

function onSquareClick(ext) {
  if (!model.fen || model.thinking) return;
  if (model.stm !== model.side) return;

  if (selected !== null && targets.has(ext)) {
    const move = targets.get(ext);
    selected = null;
    targets = new Map();
    send({ type: 'move', m: move });
    return;
  }

  const pieces = parseFen(model.fen);
  const p = pieces[ext];
  if (p && p.color === model.stm) {
    // Under a compulsory capture, only the capturing pieces are selectable.
    if (!isForcedCapture() || movableSet().has(ext)) {
      selected = ext;
      targets = movesFrom(ext);
    } else {
      selected = null;
      targets = new Map();
      toast('capture is compulsory — move a highlighted piece');
    }
  } else {
    selected = null;
    targets = new Map();
  }
  renderBoard();
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
  send({ type: 'go', ...currentLimits() });
}

function currentLimits() {
  const [kind, value] = $('#difficulty').value.split(':');
  return kind === 'movetime' ? { movetime: parseInt(value, 10) } : { depth: parseInt(value, 10) };
}

// ---- protocol -----------------------------------------------------------

function send(obj) {
  if (ws && ws.readyState === WebSocket.OPEN) ws.send(JSON.stringify(obj));
}

function handle(msg) {
  switch (msg.type) {
    case 'state': {
      Object.assign(model, {
        fen: msg.fen, legal: msg.legal, stm: msg.stm,
        moves: msg.moves || [], side: msg.side || model.side, opts: msg.opts || {},
      });
      selected = null;
      targets = new Map();
      autoSelectForced();
      renderBoard(); renderMoves(); renderStatus();
      if (model.stm === model.side) engineRequested = false;
      maybeEngineMove();
      break;
    }
    case 'info': renderInfo(msg); break;
    case 'thinking':
      model.thinking = msg.value;
      if (msg.value) renderInfo(null);
      renderStatus();
      break;
    case 'error': toast(msg.message); break;
    case 'bestmove': break; // reflected by the following `state`
    default: break;
  }
}

function newGame() {
  engineRequested = false;
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
  send({ type: 'undo', plies });
});
$('#side').addEventListener('change', newGame);
$('#eval').addEventListener('change', newGame);

renderStatus();
connect();
