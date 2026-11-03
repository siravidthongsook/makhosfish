# AGENTS.md — MakHosFish

Agent operating guide + **index** for this repository. Read this first; it points to
everything else. The authoritative rule/design contract is [`spec.md`](spec.md); a human
overview is in [`README.md`](README.md); deeper notes in [`docs/`](docs/).

**What this is:** a Stockfish-class engine for **Thai checkers (หมากฮอส / Makhos / Thai
Draughts)** — C++20, bitboard move generation, PVS search, and a self-play-tuned pattern
evaluation. Thai is the product; other draughts variants exist *only* as a perft cross-check.

---

## Golden rules (do not violate)

1. **Thai is the default and the target.** `thai_rules()` is the default `RuleSet`. Never
   change Thai behavior to accommodate a variant.
2. **Frozen formats.** FEN (`<stm>:W<list>:B<list>[:<rule50>]`), move notation
   (`32x23x14`, every landing square), external numbering `= internal + 1`. See spec §11.0.
   Do not change these without updating `spec.md` and the fixtures together.
3. **Perft is ground truth.** Thai perft(9) = `114530830` must never change. The fast
   generator must agree with the independent reference generator (`tests/reference_movegen.*`).
4. **10×10 International is out of scope** (does not fit the 32-bit board).
5. **Self-generated data only** (no third-party data or code copied except the vendored
   `doctest`, which is MIT and GPL-compatible).
6. **License is GPL-3.0-or-later.** New files carry an SPDX header; contributions are accepted
   under the same terms.

---

## Build, test, run

```bash
# Configure (macOS: pass the Xcode SDK sysroot or linking fails on this machine)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_SYSROOT=/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
cmake --build build -j

# Tests (doctest; MAKHOS_SLOW=1 enables the deep perft fixtures)
./build/makhosfish_tests
ctest --test-dir build
MAKHOS_SLOW=1 ./build/makhosfish_tests

# Sanitizer build (ASan+UBSan) — must stay clean
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DMAKHOS_SANITIZE=ON \
  -DCMAKE_OSX_SYSROOT=/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
cmake --build build-asan -j && ./build-asan/makhosfish_tests
```

Engine / protocol / tools:

```bash
./build/makhosfish play                       # interactive: you (White) vs engine
./build/makhosfish tci                        # TCI v1 server on stdin/stdout (GUI entry point)
./build/makhosfish startpos 9                 # perft(9) regression (expect 114530830)
./build/makhosfish bench 12 [e0|e1|e2]        # fixed-position NPS
./build/makhosfish go startpos 10 e2          # single search, full-chain PV
./build/makhosfish --pattern data/patterns/thai_e2.mhpw go startpos 10 e2
./build/makhosfish gauntlet 1000 6 e2 e1 4    # A/B Elo (alternating colours, random openings)

# Data pipeline (E2 weights)
./build/makhosfish_datagen 20000 6 6 /tmp/data.txt <seed>   # self-play -> <fen> <result>
./build/makhosfish_tune /tmp/data.txt data/patterns/thai_e2.mhpw 80 0.05 - 6000000
```

**Before every commit:** `./build/makhosfish_tests` green, ASan green, perft(9) unchanged,
and (for eval/search changes) a gauntlet that shows the change helps.

---

## Repository map (index)

### Core — `src/core/`
| File | Purpose |
|------|---------|
| `types.hpp` | `NB_SQUARES=32`, `Color`, `bit/popcount/lsb/msb` |
| `bitboard.{hpp,cpp}` | `BoardTables`: per-square `neighbor`, `stepMask`, `rayMask`; `rcToIdx` |
| `move.hpp` | `Move` (from/to/captured/capturedKings/promotes/n_captured) |
| `position.{hpp,cpp}` | `Position` (4×`uint32_t` bitboards + stm/ply/rule50/hash), `start_position`, `do/undo_move` |
| `movegen.{hpp,cpp}` | `generate_moves`, `generate_captures`, `generate_moves_with_paths`, `legal_moves`, `MAX_MOVE_PATH` |
| `zobrist.{hpp,cpp}` | Zobrist keys (`piece[4][32]`, stm) |
| `rules.hpp` | `RuleSet` + `thai_rules()`/`english_rules()`/`russian_rules()`/`brazilian_rules()` |
| `perft.hpp` | `perft()` (copy-make) |

### Search — `src/search/`
| File | Purpose |
|------|---------|
| `tt.hpp` | `MAX_PLY`, score constants, `TTEntry`, `TTable` (cluster-of-4, generation aging) |
| `search.{hpp,cpp}` | `Searcher` (PVS + aspiration + LMR + killers/history + IID + single-reply extension), `request_stop`/`clear_stop`, `set_info_callback`, `set_eval_mode` |

### Eval — `src/eval/`
| File | Purpose |
|------|---------|
| `eval.{hpp,cpp}` | `EvalMode{E0,E1,E2}`, `EvalDetail`, `evaluate`, `pst_of`, `move_pst_delta` |
| `pattern.{hpp,cpp}` | `PatternEval` (E2 windows/codes/weights, `.mhpw` load/save), `active_pattern()` |

### Endgame DB — `src/egdb/`
| File | Purpose |
|------|---------|
| `bitbase.{hpp,cpp}` | `Wdl` (side-to-move-relative), `EgdbLayout` (combinatorial index over the 4 piece classes; `encode`/`decode`, dense per-piece-count tables) |
| `retrograde.{hpp,cpp}` | `Egdb`, `Egdb::probe`, `solve_upto(kmax)` — Thompson/Bellman backward induction using reverse **quiet**-move predecessor generation |

### Protocol / CLI — `src/protocol/`, `src/main.cpp`
| File | Purpose |
|------|---------|
| `fen.{hpp,cpp}` | strict `parse_fen`, `to_fen`, `move_to_string`, `move_to_notation`, chain-aware `parse_move` |
| `pdn.{hpp,cpp}` | PDN parse/format/replay |
| `tci.{hpp,cpp}` | `TciSession` TCI v1 server (async `go` on a worker thread) |
| `main.cpp` | command dispatch: `tci`, `pdn`, `play`, `perft`, `go`, `bench`, `selfplay`, `gauntlet`; `--pattern <file>` |

### Tools — `tools/`
`perft.cpp`, `bench.cpp`, `datagen.cpp` (self-play data), `tune.cpp` (Texel SGD → `.mhpw`).

### Tests — `tests/`
`test_{movegen,rules,zobrist,fen,perft_agreement,perft_fixtures,variants,search,eval,pattern,
egdb,notation,pdn,tci}.cpp`; independent generator `reference_movegen.{hpp,cpp}`; fixtures
`perft_fixtures.txt` (Thai) and `perft_variants.txt`; helpers `test_util.hpp`, `test_main.cpp`.

### Data & other
`data/patterns/thai_e2.mhpw` — shipped E2 weights (9.4 MB; `.mhpw` format spec §20.6).
`third_party/doctest/doctest.h` — vendored test framework.
`.github/workflows/ci.yml` — matrix {ubuntu, macos} × {release, sanitize}; `.github/workflows/codeql.yml` — CodeQL (C++ + web JS).
`LICENSE` (GPL-3.0), `CONTRIBUTING.md`, `CODE_OF_CONDUCT.md`, `SECURITY.md`, `CHANGELOG.md`,
`.github/ISSUE_TEMPLATE/`, `.github/PULL_REQUEST_TEMPLATE.md` — public-repo process.

### Web board — `web/`
Optional local browser UI (`web/serve.sh`). A Node + `ws` bridge (`web/server/`)
spawns one `makhosfish tci` child per session and translates JSON ⇄ TCI; the
vanilla-JS UI (`web/public/`) renders the FEN/legal-move list and never
reimplements rules. Engine side: the `state` command in `src/protocol/tci.cpp`
returns `<fen> / <moves> / <stm>`. See `web/README.md`.

---

## Design notes (why things are the way they are)

- **Board:** 32-bit dense, internal `0..31 = row*4 + file`; external PDN/FEN `= internal + 1`.
  Playable iff `(row+col)` is odd. Fast generator + a slow independent reference must agree.
- **Thai-specific rules** (spec §2.2/§3): flying kings **must land immediately behind** the
  captured piece; captured pieces are **removed immediately** mid-sequence; compulsory capture;
  **no** maximum-capture rule; no stalemate (no move = loss).
- **Search:** pseudo-legal == legal (no check/pins), so there is no legality pass. **Null-move
  pruning is unsound** (zugzwang); forced captures are extended instead of a chess quiescence.
- **E2 antisymmetry:** the pattern score reads the black perspective from the **paired** window
  (`PAIR[w]`), which makes `score(mirror(p)) == -score(p)` for *any* weights, so the engine can
  never acquire a colour bias and the tuner needs no symmetry constraints. `E2 = E1 + pattern`,
  so unloaded weights are bit-identical to E1. See `src/eval/pattern.hpp` for the proof sketch.
- **Endgame DB:** WLD bitbases via Thompson/Bellman backward induction. `EgdbLayout` indexes
  every placement of the 4 piece classes on the 32 dark squares (combinatorial `encode`/`decode`);
  `solve_upto(k)` solves 1..k pieces ascending, reading captures from the already-solved lower
  tables. Predecessors come from reverse **quiet**-move generation (no edge storage, so it scales),
  and a candidate parent is only kept if it has no compulsory capture. A colour-mirror invariant
  (`WDL(p) == WDL(mirror(p))`) holds by construction and is tested. The 50-move/repetition rules
  are ignored (standard WLD convention).
- **Not yet built:** opening book (`src/book/`); endgame DB file storage + search probe (M5 still in
  progress — in-memory solving to ≤4 works today).

---

## Milestones (see spec §16 for full text)

| M | Scope | Status |
|---|-------|--------|
| M0 | rules, bitboard movegen, perft | DONE (`0237fac`) |
| M0.5 | generic variants (cross-check only) | DONE (`fce2d2c`) |
| M1 | PVS + TT + iterative deepening + time mgmt | DONE (`fce2d2c`) |
| M2 | E0/E1 eval + self-play gauntlet | DONE (`c4fbae1`) |
| M3 | TCI v1 + full notation + PDN/FEN + CLI | DONE (`9372f09`) |
| M4 | E2 pattern eval + datagen + tuning | DONE (`37a8665`) |
| M5 | endgame bitbases (≤6, then ≤8) | IN PROGRESS — indexer + WLD solver ≤4, reference cross-check, tests; file storage + probe next |
| M6 | opening book + Lazy SMP | — |
| M7 | NNUE (E3) + extend DBs toward full solve | — |

Current head is E2 (`--pattern data/patterns/thai_e2.mhpw`), **+102 Elo vs E1 @ depth 6**,
**+128 Elo @ depth 9**.

---

## Conventions

- **C++20**, namespace `mhf`, `-Wall -Wextra -Wpedantic` clean.
- Comments: short, future-facing, explain **why** not **what** (see repo style in `src/`).
- Commit style: `type(scope): summary` (e.g. `feat(m4): ...`). Do not commit without asking Boss.
- Keep the hot path free of hidden global state; `active_pattern()` is read-only after load.
- New behavior must ship with a test; correctness changes must keep perft(9) unchanged.
