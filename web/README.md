# MakHosFish web board

A local browser board for playing หมากฮอส (Thai checkers) against `makhosfish`.
The engine stays the source of truth for rules — the UI only renders the FEN and
legal-move list it receives and sends back the notation you click.

```
Browser board  ──HTTP + WebSocket──►  bridge (Node + ws)  ──stdin/stdout TCI──►  makhosfish tci
```

## Run

```bash
./web/serve.sh
```

This builds `makhosfish` if stale, installs the bridge's single dependency (`ws`),
starts the server, and prints the URL (default <http://127.0.0.1:8787>).

## Configuration (env vars)

| Variable         | Default                        | Meaning                       |
|------------------|--------------------------------|-------------------------------|
| `PORT`           | `8787`                         | listen port (loopback only)   |
| `MAKHOS_BIN`     | `build/makhosfish`             | engine binary                 |
| `MAKHOS_PATTERN` | `data/patterns/thai_e2.mhpw`   | E2 weights (optional)         |

## Layout

```
web/
├── serve.sh              # build + run
├── server/
│   ├── index.js          # http (static) + ws (sessions)
│   ├── engine.js         # spawn `makhosfish tci`, line framing, promise API
│   └── package.json      # dep: ws
└── public/
    ├── index.html
    ├── board.css
    └── board.js          # render FEN, click-to-move, WS client
```

## WebSocket API (`/ws`)

Client → server:

| Message                                   | Effect                              |
|-------------------------------------------|-------------------------------------|
| `{type:"newgame", side, ruleset, eval}`   | reset engine + options              |
| `{type:"state"}`                          | current `{fen, legal, stm}`         |
| `{type:"move", m}`                        | validate `m` against `legal`, apply |
| `{type:"undo", plies}`                    | rewind and re-sync                  |
| `{type:"go", depth|movetime|nodes}`       | search; engine plays its own move   |
| `{type:"stop"}`                           | stop the running search             |

Server → client: `state`, `info`, `bestmove`, `thinking`, `error`.

## Notes

- Terminal condition: the side with **no legal move loses** (spec §3.5) — no
  stalemate draw.
- Board numbering matches the engine exactly: Black back rank `1..4` on top,
  White back rank `29..32` at the bottom (spec §3.1).
- One `makhosfish tci` child per WebSocket session; it is restarted on demand
  after a crash.
