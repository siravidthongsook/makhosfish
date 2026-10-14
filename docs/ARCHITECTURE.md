# Architecture

MakHosFish is a layered engine: a dependency-free core, a search layer on top, an evaluation
layer, and a thin protocol/CLI shell. Nothing in the hot path allocates or touches hidden globals
(the one process-wide read-only table is the loaded pattern weights).

```
                 +---------------------------+
   GUI / CLI <-> |  Protocol layer (TCI v1)  |   src/protocol/{tci,pdn,fen}.*
                 +---------------------------+
                              |
 +---------------------------------------------------------------+
 |  Position (bitboards) --> MoveGen (make/unmake) --> Search     |
 |  src/core/*              src/core/*                src/search/*  |
 |         |                                          |            |
 |         v                                          v            |
 |     Zobrist hashing                    Eval (E0/E1/E2), Egdb    |
 |     src/core/zobrist.*                 src/eval/*, src/egdb/*   |
 +---------------------------------------------------------------+
                              |
                 +---------------------------+
                 |  Offline tooling          |
                 |  datagen | tune           |
                 |  src tools/*.cpp          |
                 +---------------------------+
```

## 1. Board representation (`src/core`)

- **32-bit dense bitboards.** The 32 dark squares are indexed internally `0..31 = row*4 + file`,
  row `0` = White's promotion rank. External PDN/FEN numbering is `internal + 1`.
- Four `uint32_t` bitboards: `Position::wm, wk, bm, bk` (`src/core/position.hpp`). Derived masks
  (`occupied`, `empty`, `pieces(c)`, `men(c)`, `kings(c)`) are computed on demand.
- **`BoardTables`** (`src/core/bitboard.{hpp,cpp}`) precomputes, per square and per diagonal
  direction (`NW, NE, SW, SE`):
  - `neighbor[sq][d]` — the adjacent square (or `-1`),
  - `stepMask[sq][d]` — that neighbor as a one-bit mask,
  - `rayMask[sq][d]` — the whole diagonal ray.
  `rcToIdx(row,col)` maps board coordinates (or `-1`) and encodes the "playable iff `(row+col)` odd"
  rule in one place.

## 2. Moves and move generation (`src/core/move.hpp`, `movegen.*`)

- A `Move` is a *complete* turn: `from`, `to`, the full `captured` mask, `capturedKings`, a
  `promotes` flag and `n_captured`. Multi-jump chains are enumerated depth-first; each move records
  every captured square (needed for the immediate-removal and majority rules).
- `generate_moves` returns pseudo-legal moves. **In draughts pseudo-legal == legal** — there is no
  check/pin concept — so there is no separate legality pass. Compulsory capture is enforced here:
  if any capture exists, quiet moves are discarded.
- **Thai king capture** lands on the square *immediately behind* the captured piece
  (`king_must_land_immediately_behind`), and the victim is removed *during* the sequence
  (`immediate_removal`), so a later diagonal can open mid-chain.
- **Paths.** `generate_moves_with_paths` optionally records each move's landing chain (internal
  squares) so full multi-jump notation (`32x23x14`) can be emitted and parsed. This is off the
  search hot path and only used by the cold protocol/PDN code (`MAX_MOVE_PATH` bounds a chain).
- **`do_move`/`undo_move`** (`position.cpp`) keep bitboards, side, counters and the Zobrist hash in
  sync. Perft uses copy-make (`src/core/perft.hpp`); the search uses `do`/`undo` with a saved
  `rule50`.

## 3. Variant configuration (`src/core/rules.hpp`)

A `RuleSet` is pure data (backward man capture, flying kings, immediate removal, max-capture
requirement, mid-capture promotion, force-continue, draws). `thai_rules()` is the default and the
product; `english_rules()` / `russian_rules()` / `brazilian_rules()` exist only to cross-check the
generic generator against published perft counts (`tests/test_variants.cpp`).

## 4. Search (`src/search`)

`Searcher::go` (`search.cpp`) runs iterative deepening with **aspiration windows**, wrapping a
**PVS** `negamax`:

- Transposition table (`tt.hpp`): cluster-of-4 buckets keyed by the Zobrist hash, with
  generation aging; stores `depth`, `score`, `flag`, `move`, `move2`.
- Move ordering: TT move → winning captures → killers → history + PST delta.
- Internal iterative deepening when the TT has no move; **single-reply extension** for a lone quiet
  move; LMR with re-search.
- Draughts-specific: **no null-move pruning** (zugzwang makes it unsound); instead forced captures
  make most nodes tiny, so tactical lines are searched fully.
- Repetition/50-move/insufficient-material draws are detected in-tree; scores use mate-distance
  pruning.
- Protocol hooks: `request_stop()`/`clear_stop()` (cooperative stop from another thread),
  `set_info_callback()` (per-iteration `info`), `set_eval_mode()`, `resize_hash()`.

## 5. Evaluation (`src/eval`)

- **E0** — material + hand-tuned piece-square tables. Man ≈ 100cp, king ≈ 300cp.
- **E1** — adds hand features: men mobility, flying-king reach, immediate capture threats,
  defended/supported men, first-king bonus, pre-crown control. Selected at runtime and A/B-tested.
- **E2** — adds a learned **pattern** contribution (`pattern.{hpp,cpp}`):
  - Six overlapping windows, each a 4-row × 2-file block of the internal lattice (= a 4×4
    dark-square region). Each window's 8 cells are encoded base-5 (empty / own man / own king /
    opp man / opp king) from the scoring side's view; the code is `Σ state_i · 5^i`.
  - Weight tables are `int16` per `(window, phase, code)`, with two phases (midgame / ≤8 pieces).
  - The score is `Σ_w  T[w][ph][code(p,w,WHITE)] − T[PAIR[w]][ph][code(p,w,BLACK)]`. Because the
    black term reads from the **rot180-paired** window (whose cell order is aligned), substituting
    `w → PAIR[w]` flips the whole sum: `score(mirror(p)) == -score(p)` for **any** weights. This
    guarantees no colour bias and removes the need for symmetry constraints in the tuner.
  - `E2 = E1 + pattern`, so an unloaded (zero) table is bit-identical to E1 — used as a control in
    the gauntlet harness.
  - Weights live in `data/patterns/thai_e2.mhpw` (`.mhpw`, spec §20.6) and are loaded via CLI
    `--pattern` or TCI `setoption name PatternFile` into `active_pattern()`.

## 6. Endgame databases (`src/egdb`)

WLD (win/loss/draw) bitbases solved by **Thompson/Bellman backward induction** — the same
technique as Chinook/Scan. Small endgames are common in Thai because captures strip pieces fast.

- **Indexing** (`bitbase.cpp`): a position is indexed combinatorially. The four piece classes
  (WM/WK/BM/BK) are packed in fixed order; each class contributes a colex-rank digit over the
  squares not yet used by earlier classes, giving one dense slot per `(placement, side to move)`.
  `EgdbLayout::encode`/`decode` are inverse bijections; `k_size[k]` is the slot count for exactly
  k pieces.
- **Solver** (`retrograde.cpp`): `solve_upto(kmax)` solves k = 1..kmax ascending. A first forward
  sweep marks positions with no legal move as Loss, any position with a capture into an
  already-solved *losing* lower position as Win, and counts each position's remaining children.
  A work queue then propagates: a Loss child makes every parent Win, and a Win child decrements
  each parent's remaining count (reaching zero ⇒ Loss). Whatever stays unresolved after the queue
  drains is a Draw (a cycle). Because the queue needs *predecessors*, the solver uses **reverse
  quiet-move generation** rather than storing edges: for each king/man in the child it walks back
  to the squares it could have come from, keeping a candidate parent only if it has no compulsory
  capture (so the quiet move is actually legal). This keeps memory linear in the position count and
  is what will let the same code reach ≤6/≤8.
- **Independence & invariants**: `tests/test_egdb.cpp` cross-checks `solve_upto` against a second,
  independently written forward value-iteration solver, and asserts the colour-mirror invariant
  `WDL(p) == WDL(mirror(p))` (swap colours + rotate the board + flip the side to move) over the
  whole space. The 50-move and repetition rules are ignored, so e.g. K vs K is an exact Draw.

## 7. Protocol and CLI (`src/protocol`, `src/main.cpp`)

- **FEN** (`fen.{hpp,cpp}`): frozen `<stm>:W<list>:B<list>[:<rule50>]`, strict parsing (a mid-list
  colour letter is rejected, not silently dropped).
- **Notation** (`fen.{hpp,cpp}`): `move_to_notation` emits every landing square; `parse_move`
  matches the full chain against a generated move.
- **PDN** (`pdn.{hpp,cpp}`): tolerant parser (skips comments, NAGs, variations), formatter, and a
  rule-aware replay; `[FEN]` overrides startpos.
- **TCI v1** (`tci.{hpp,cpp}`): `TciSession::run` is a line loop; `go` runs on a worker thread so
  `stop`/`isready`/`quit` stay responsive; `info` streams per iterative-deepening iteration.
- **CLI** (`main.cpp`): `tci`, `pdn`, `play`, `perft`, `go`, `bench`, `selfplay`, `gauntlet`, and a
  global `--pattern <file>` option.

## 8. Data pipeline (offline)

```
datagen (self-play)  ──►  <fen> <result>  ──►  tune (Texel SGD)  ──►  .mhpw  ──►  engine
```

`tools/datagen.cpp` plays games from short random openings with both sides searching at a fixed
depth, labelling every position with the final result from the side-to-move's perspective.
`tools/tune.cpp` fits the pattern weights by SGD on the cross-entropy between
`sigmoid(ln10·eval_stm/400)` and the result, using a 90/10 train/val split and best-val selection.
The E1 score is a fixed offset, so the tuner only learns the E2 correction on top of E1.

## 9. Key decisions and trade-offs

- **Dense 32-bit over padded 64-bit board:** trivial copy-make, matches the external numbering
  1:1. Revisit only if mask lookups dominate in profiling.
- **No null-move / no legality pass:** both follow from draughts rules (zugzwang; no check).
- **Paired-window E2:** buys exact colour-antisymmetry for free, at the cost of a slightly unusual
  indexing (black reads `PAIR[w]`).
- **Additive stages (E2 = E1 + pattern):** lets every stage stand alone and be A/B-tested, and
  makes the untrained default safe.
