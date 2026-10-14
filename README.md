# MakHosFish

A **Stockfish-class engine for Thai checkers (หมากฮอส / Makhos / Thai Draughts)**.
There is no dedicated super-strong makhos engine today — this project aims to be the first.

- **Fast:** 32-bit dense bitboards, pseudo-legal == legal move generation (no check/pins in
  draughts), copy-make make/unmake. ~2.7–3.6 M nodes/s/core on this machine today; more to come.
- **Correct:** the exotic Thai rules are pinned by a perft suite cross-checked against an
  independent reference generator. Thai perft(9) = **114,530,830**.
- **Good:** PVS search with a transposition table, and an evaluation staged E0 → E1 → E2, where
  **E2 is a pattern evaluation learned from self-play** (Scan/Kingsrow style).
- **Drivable:** a UCI-shaped **TCI v1** protocol server (any GUI), an interactive CLI, and full
  PDN / FEN support.

> **Thai is the product.** Other draughts variants (English, Russian, Brazilian, …) are supported
> only as a perft cross-check harness to validate the generic move generator, never as a goal.
> 10×10 International does not fit the 32-bit board and is out of scope. See [`spec.md`](spec.md).

## Rules implemented (Thai)

8×8 board, 8 men a side on the two nearest rows. Men move/capture forward only; a king ("ฮอส")
flies along diagonals. Two rules make Thai distinct and are implemented exactly: a flying king
**must land on the square immediately behind** the piece it captures, and captured pieces are
**removed immediately** during the capture sequence. Captures are compulsory, there is **no
maximum-capture rule**, and no move = loss (no stalemate-as-draw).

## Quickstart

```bash
# macOS: pass the Xcode SDK sysroot, or linking fails on this machine.
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_SYSROOT=/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
cmake --build build -j

./build/makhosfish_tests              # full unit + perft suite
MAKHOS_SLOW=1 ./build/makhosfish_tests   # add the deep perft fixtures
```

Play / drive the engine:

```bash
./build/makhosfish play                                  # you (White) vs the engine
./build/makhosfish tci                                   # TCI v1 server on stdin/stdout
./build/makhosfish startpos 9                            # perft(9) -> 114530830
./build/makhosfish bench 12 e2                           # fixed-position NPS
./build/makhosfish go startpos 12 e2                     # one search, full-chain PV
./build/makhosfish --pattern data/patterns/thai_e2.mhpw go startpos 12 e2
```

## Evaluation stages

| Stage | What it is | Notes |
|-------|------------|-------|
| `e0` | material + piece-square tables | baseline |
| `e1` | `e0` + hand features | mobility, captures, defended men, first-king, pre-crown |
| `e2` | `e1` + **learned pattern windows** | default; needs a `.mhpw` weight file |

E2 divides the board into six overlapping 4×4 dark-square windows; each window is encoded from
the scoring side's point of view and scored by a learned weight. Because the black perspective is
read from the rot180-paired window, the evaluation is *exactly* colour-antisymmetric for any
weights — the engine cannot take a colour bias. With no weights loaded, E2 is bit-identical to E1.

Weights are trained from self-play:

```bash
# 1. generate games: <games> <depth> <opening> <out> [seed]
./build/makhosfish_datagen 20000 6 6 /tmp/data.txt 1

# 2. fit pattern weights: <data> <out.mhpw> [epochs] [lr] [init|-] [max_samples]
./build/makhosfish_tune /tmp/data.txt data/patterns/thai_e2.mhpw 80 0.05 - 6000000

# 3. measure (alternating colours, random openings)
./build/makhosfish --pattern data/patterns/thai_e2.mhpw gauntlet 1000 6 e2 e1 4
```

## Strength

Head-to-head self-play Elo (the project's measurable proxy; no external makhos opponent exists):

- **E2 vs E1: +102 Elo** over 1000 games @ depth 6, **+128 Elo** over 300 games @ depth 9.
- E2 last trained on **9.7M positions** from **120k** self-play games.

## Protocol (TCI v1)

A UCI-shaped line protocol — `tci` handshake, `isready`, `setoption` (Hash / Eval / PatternFile /
RuleSet), `position startpos|fen … moves …`, `go` (depth / nodes / movetime / wtime / btime /
winc / binc / infinite), streamed `info`, `bestmove`, `stop`, `quit`, `perft`. `go` runs on a
worker thread so `stop`/`isready` stay responsive. See spec §11.1.

## Project layout

```
src/core/      board, movegen, position, zobrist, rules, perft
src/search/    PVS search + transposition table
src/eval/      E0/E1 hand eval + E2 pattern eval
src/protocol/  FEN, PDN, TCI v1
src/main.cpp   CLI dispatch
tools/         perft, bench, datagen, tune
tests/         doctest suite + independent reference movegen + perft fixtures
data/patterns/ trained pattern weights (.mhpw)
spec.md        the authoritative design contract
```

## Status

Milestones M0 → M4 are complete (rules + perft, search, E0/E1, protocol/CLI, E2 pattern eval).
Next: **M5 — endgame bitbases**, then opening book + parallel search (M6) and NNUE (M7).
See [`spec.md` §16](spec.md) and [`AGENTS.md`](AGENTS.md) for the full picture.

## License

Own code and self-generated data only (MIT); no third-party engine code or data is used, except
the vendored [`doctest`](third_party/doctest/doctest.h) test framework.
