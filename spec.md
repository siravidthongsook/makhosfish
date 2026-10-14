# Thai Checkers Engine — Specification

> Working title: **MakHosFish** (a Stockfish-class engine for หมากฮอส / Makhos / Thai Draughts).
> Status: Draft v0.1 — research-backed, implementation-ready.
> Owner: Boss.

---

## 0. One-paragraph vision

Build the strongest, fastest Thai checkers (Thai draughts, *Mak-hot / Makhos*) engine in
existence — the analogue of Stockfish for chess. It must be **super fast** (bitboard
move generation, alpha-beta search at tens of millions of nodes/second/core) and
**super good** (self-play-tuned evaluation, endgame bitbases, opening book), runnable as a
standalone CLI plus a UCI-like protocol so any GUI can drive it, with a correctness suite
(perft) that pins the exotic Thai rules exactly.

---

## 1. Goals / Non-goals

### Goals
- G1. **Correct rules.** Thai rules implemented exactly, including the two things that make
  Thai distinct from every other draughts variant:
  1. Flying king **must land on the square immediately behind** the captured piece.
  2. Captured pieces are **removed immediately** during the capture sequence (Turkish-style).
- G2. **Speed.** Move generation and make/unmake are an order of magnitude faster than chess;
  the engine should reach 10M–100M+ nodes/sec/core and search very deep because captures are forced.
- G3. **Strength.** Beat every existing makhos program and strong human masters; ultimately
  *strongly solve* the game if feasible (it is a small 8×8 game — see §12).
- G4. **Portability.** Same core builds for CLI, protocol server, and library; deterministic;
  no hidden global state in the hot path.
- G5. **Extensibility.** Rule set is configurable so the core can also play closely related
  variants (International 10×10, Russian, English, Brazilian, Turkish-8×8) — this doubles as a
  validation harness against published perft numbers.

### Non-goals (v1)
- NG1. GUI. (Protocol + CLI only; GUIs consume the protocol.)
- NG2. Online matchmaking / server.
- NG3. Mobile/Web builds (possible later via the library, not v1).
- NG4. Distributed/cloud endgame generation on day one (single machine first).

---

## 2. Domain research summary (what we learned)

### 2.1 Thai checkers rules (authoritative synthesis)

Sources: Thai competition rules site (makhos-games), Checkers & Draughts Wiki (*Mak-hot*),
draughts.io, rules.moddable.games, BoardGameGeek discussion.

- **Board:** 8×8, play on the 32 dark squares only.
- **Setup:** 8 men per side, filling the dark squares of the **two** rows nearest each player.
  Four empty rows separate the armies.
- **First move:** sources disagree (some White, some Black). → **configurable**, default White.
- **Men** (เบี้ย): move 1 square diagonally forward; capture by jumping an adjacent enemy
  diagonally forward onto the empty square beyond. **Men never move or capture backward.**
- **Promotion** ("kern hos" / ฮอส): a man that *finishes a turn* on the opponent's back rank
  becomes a **king**.
- **Kings** (ฮอส): **flying** — move any number of empty squares along a diagonal in all 4
  directions. On capture, a king slides along the diagonal, jumps the **first** enemy piece it
  meets, and **must land on the square immediately behind that piece** (the first empty square
  after it). *(International draughts lets the king choose any landing square beyond; Thai does not.)*
- **Immediate removal:** a captured piece is removed **during** the sequence, so a previously
  blocked diagonal can open (or, with flying blockers, the geometry changes mid-sequence). This
  is the Turkish-checkers behavior, not the international behavior.
- **Multi-capture:** the same piece continues capturing while any capture remains available,
  changing direction (including 180°) as needed. The route may zig-zag.
- **Compulsory capture:** if any capture exists, a quiet move is illegal.
- **No maximum-capture rule:** when several capture sequences exist, the player may choose any
  legal one; capturing the most pieces is **not** required. (Contrast: International/Russian
  require the longest.)
- **Terminal:** you lose if you have **no legal move** — all pieces captured *or* all blocked.
  There is no stalemate-as-draw.
- **Draws:** by agreement, by repetition, or by competition rule. Thai live competition often
  uses clocks plus a material count at timeout (man = 1, king = 2), which the engine does not need;
  the engine needs deterministic draw rules (see §3.6).

### 2.2 Rule ambiguities to resolve in-spec (configurable, with chosen defaults)

| # | Question | Decision (default) | Config key |
|---|----------|--------------------|------------|
| D1 | First mover | White | `rules.first_move = white` |
| D2 | Man landing on back rank **mid-capture** while more captures remain | **Does not promote; continues as a man.** Promotes only if the turn ends on the back rank. | `rules.midcapture_promo = continue_man` |
| D3 | Must the player continue a started capture sequence to its full extent (with the same piece)? | Yes — must keep capturing while any capture exists with that piece. | `rules.force_continue = true` |
| D4 | Maximum-capture requirement | None (free choice). | `rules.max_capture = false` |
| D5 | Can a king pass over a square vacated by an immediately-removed piece, or over a square that will be captured later? | Governed by immediate removal: the board updates after each jump; later captures don't block earlier ones. | — |
| D6 | Repetition / draw rule | Threefold repetition = draw; plus a 25-move "no man move and no capture" draw rule; insufficient material (K vs K, K vs nothing) = draw. | `rules.draw_*` |

D2 and D3 are the two most likely to differ between Thai implementations; both are explicit and
switchable, and both must have dedicated unit tests.

### 2.3 What the strongest draughts engines do (techniques to steal)

- **Scan** (Letouzey) — strongest international draughts engine. Bitboard move gen, PVS search,
  aspiration windows, singular extensions, LMR, **parallel search**, opening book, endgame
  bitbases (up to 6–7 pieces), and a **pattern-based evaluation learned entirely by self-play**
  (16 overlapping 4×4 regions; technique borrowed from Othello engines). The "scan" name comes
  from scanning overlapping rectangles.
- **Kingsrow** (Ed Gilbert) — 8×8 English/Italian and 10×10 international. Shallow
  **neural-net / pattern** evaluation with features (balance, tempo, men, kings, first king) over
  overlapping 4×6 areas, two phases; trained by gradient descent. 10-piece WLD databases.
- **Chinook** (Schaeffer et al.) — proved 8×8 English checkers is a draw (2007). Backbone =
  huge **retrograde endgame databases** (all ≤10 pieces = 3.9×10¹³ positions; ≤8 pieces =
  4.4×10¹¹) plus a proof-tree manager. Confirms: forced captures shrink the tree fast, so
  endgame DBs dominate practical strength.
- **py-draughts / rapid-draughts / Daan Noordenbos blog** — practical 8×8 bitboard details:
  three 32-bit bitboards (white, black, kings) or a padded 64-bit layout; move gen via
  `shift(AND(shift(men, x), empty), -x)` style bit tricks; copy-make over plain ints.
- **Key draughts-specific search note:** **null-move pruning does not work** (zugzwang). The
  substitute is a Scan-style **forward pruning / verification search** (shallow re-search at a
  raised beta). Forced captures let us extend along capture chains instead of a chess-like quiescence.

### 2.4 Existing Thai checkers programs (the bar to beat)

No known *dedicated super-strong* makhos engine exists (only mobile apps with 3 difficulty
levels: duckprog "MakHos Thai"; web sites: playok, thaibg, makhos.com; the Dagaz generic
checkers framework). **This is the gap** — a Stockfish-class makhos engine would be the first.

---

## 3. Rules specification (normative)

### 3.1 Geometry
```
Standard 32-square numbering (dark squares only; 0-indexed 0..31 internally):

   0   1   2   3      rank 8  (top / Black back rank is set B in Thai setup)
 4   5   6   7        rank 7
   8   9  10  11      rank 6
12  13  14  15        rank 5
  16  17  18  19      rank 4
20  21  22  23        rank 3
  24  25  26  27      rank 2
28  29  30  31        rank 1  (bottom / White back rank)

Start: Black men = squares 0..7 (ranks 7–8). White men = squares 24..31 (ranks 1–2).
```
(Exact numbering convention will be frozen and mirrored in PDN/FEN; test fixtures depend on it.)

### 3.2 Directions
- Diagonals are N, E, S, W of the checkerboard; represent as precomputed per-square masks.
- Men: forward only (Black forward = toward rank 1 = "down"; White forward = toward rank 8).

### 3.3 Move generation (quiet)
- Man: forward diagonal neighbor is empty.
- King: all empty squares along each of the 4 diagonals until first blocker.

### 3.4 Move generation (capture)
- **Man:** enemy on forward diagonal, empty square immediately beyond → jump; remove enemy
  immediately; repeat from landing square (forward only).
- **King:** slide along diagonal; first non-empty square must be enemy; the square immediately
  beyond must be empty → land there; remove enemy immediately; repeat from landing square in any
  of the 4 directions; continue until no capture remains.
- Multi-capture = depth-first enumeration; each move records the ordered list of captured squares.
- If any capture exists for the side to move, only capture moves are legal.

### 3.5 Terminal conditions
- Side to move has no legal move → that side **loses**.
- Draw conditions per §3.6.

### 3.6 Draws (engine rules)
- Threefold repetition of the same position (same side to move, same material) → draw.
- 25 consecutive plies by each side with no man move and no capture → draw.
- Insufficient material: e.g. king vs king, single king vs lone man that cannot force progress
  → draw (refined during testing).
- (Optional) numeric count rules if adopting Thai clock competitions — out of scope for v1.

### 3.7 Rule config surface
All of §2.2 defaults are data, not code branches, so perft can be reused across variants:
```text
rule_set = {
  board_size, pieces_per_side, rows_of_pieces,
  men_capture_backward: bool,
  flying_kings: bool,
  king_must_land_immediately_behind: bool,   # TRUE for Thai, FALSE for international
  immediate_removal: bool,                    # TRUE for Thai/Turkish
  max_capture_required: bool,
  midcapture_promo: {stop | continue_man | promote_and_continue},
  first_move: white|black,
  draw_repetition, draw_no_progress, ...
}
```

---

## 4. Architecture overview

```
                     +---------------------------+
   GUI / CLI  <----> |  Protocol layer (TCI v1)  |  UCI-like text protocol
                     +---------------------------+
                                  |
+---------------------------------------------------------------+
|                            Core engine                         |
|  +-----------+   +-------------+   +----------------------+   |
|  | Position  |   | MoveGen      |   | Search (PVS + TT)    |   |
|  | (bitboard)|<--| (make/unmake)|-->| iterative deepening  |   |
|  +-----------+   +-------------+   +----------------------+   |
|        |                                   |                   |
|        v                                   v                   |
|  +-----------+   +-------------+   +----------------------+   |
|  | Zobrist   |   | Eval         |   | Opening book         |   |
|  | hashing   |   | (PST->pattern|   | Endgame bitbases     |   |
|  +-----------+   |  ->NNUE)     |   +----------------------+   |
|                  +-------------+                               |
+---------------------------------------------------------------+
                                  |
                     +---------------------------+
                     | Offline tooling           |
                     | perft | tuning | datagen  |
                     | retrograde DB generator   |
                     +---------------------------+
```

**Language: C++20 (LOCKED).** Rationale: matches the Scan/Kingsrow/Stockfish lineage, so proven
draughts-engine techniques and reference source can be ported rather than translated; best-known
codegen for the bit-trick hot path; native intrinsics (`__builtin_ctz/clz`, `popcnt`, SIMD) and
trivial shared-TT concurrency for Lazy SMP. We accept the usual C++ memory-safety risk; it is
contained with sanitizers (ASan/UBSan), the perft suite, and make/unmake invariance fuzzing (§14).
Rust was evaluated (safer but slower to reach proven strength) and rejected. Build: CMake,
C++20 (`-O3 -march=native` for release, `-fsanitize=address,undefined` for debug).

---

## 5. Board representation

### 5.1 Bitboards
Four bitboards, one bit per dark square (32 bits), plus side-to-move:
```text
white_men, white_kings, black_men, black_kings
```
Derived: `white = white_men|white_kings`, `black = ...`, `occupied`, `empty = ~occupied`,
`kings = white_kings|black_kings`.

**Layout: 32-bit dense (LOCKED).** Four `uint32_t` bitboards, internal square index **0..31**
(§3.1 diagram). Move generation uses **precomputed per-square neighbor/ray masks**
(`uint32_t nw[32], ne[32], sw[32], se[32]` and `ray_*[32]`); kings use `ctz/clz` on
`ray & occupied` to find the first blocker in O(1). Chosen over the 64-bit padded-border
layout because the dense 32-bit word makes copy-make trivial and matches the PDN numbering
1:1 (external 1..32 = internal 0..31 after `-1`). Revisit padded-border only if profiling
shows the mask lookups dominate.

**External ↔ internal mapping (frozen):** external PDN/FEN square `n` (1..32) = internal `n-1`.
Black back rank = external 1..4; White back rank = external 29..32.

### 5.2 Python/NumPy and other langs are explicitly rejected for the hot path
The py-draughts benchmark (~21 µs legal move gen) shows interpreted langs are 100–1000× too
slow. Hot path is native only.

### 5.3 Position state
```text
struct Position {
  uint32_t wm, wk, bm, bk;
  uint8_t  stm;          // side to move
  uint16_t ply;          // for 50-move-ish counters
  uint64_t hash;         // Zobrist
  // counters for draw rules, king flags, etc.
}
```

### 5.4 Move encoding
A move is up to 8 jumps; encode compactly:
```text
struct Move {
  uint8_t  from;         // 0..31
  uint8_t  to;           // 0..31
  uint32_t captured;     // bitboard mask of removed squares
  uint8_t  n_captured;
  bool     promotes;     // ends on back rank
}
```
For TT/book keys, pack `(from, to)` into a `uint16_t`; keep full captured-mask moves in the
per-node move list only.

---

## 6. Move generation

- **Quiet man moves:** `to = shift(mask[dir][from]); if empty -> emit`. Vectorized via
  per-direction shifts and masks, iterate with `ctz`/`poplsb`.
- **Man captures:** enemy on `mask[dir][from]`, `mask[dir][mask[dir][from]]` empty → recurse.
- **King moves:** ray scan in 4 directions, stop at first blocker.
- **King captures:** scan ray; first non-empty square must be enemy; square beyond must be empty;
  land there; recurse with enemy removed immediately (bitboard updated). This directly encodes
  the Thai "land immediately behind" rule.
- **Counting rule:** first generate any capture; if found, discard quiet moves (compulsory capture).
- **Make/unmake:** copy-make (preferred, cheap for 4×32-bit words) *and* an optional
  make/unmake stack for the deepest nodes; profile which wins.
- **Legal vs pseudo-legal:** because there is no check/pin concept (unlike chess), pseudo-legal
  generation is *complete* — the only legality filter is "no legal move = loss" and forced
  capture. This removes a whole legality pass → big speed win.

### 6.1 Direction/mask tables (precomputed at startup)

```text
nw[32], ne[32], sw[32], se[32]   // single-step neighbors as bitmasks (0 if off-board)
ray_nw[32], ... ray_se[32]       // full diagonal rays as bitmasks
```
Ray scans use `(ray & occupied)` → `ctz`/`clz` to find the first blocker in O(1) instead of a loop.

---

## 7. Search

Baseline: **Principal Variation Search (PVS)** with iterative deepening.

Components (in rough order of payoff):
1. **Transposition table** keyed by Zobrist hash; store `depth, flag(EXACT/LOWER/UPPER), score,
   best move`. Sized dynamically (e.g. 64–256 MB). Cluster-of-4 bucketing to reduce collisions.
2. **Aspiration windows** around the previous iteration's score.
3. **Move ordering:** hash/TT move, winning captures first, killer moves, history heuristic,
   counter-move history, then quiet moves by PST delta.
4. **Forced-capture extensions:** since captures are mandatory, extend along capture chains —
   this replaces a chess quiescence search in most nodes.
5. **Late Move Reductions (LMR)** with re-search on fail-high.
6. **Singular extensions** (Scan uses them).
7. **Forward pruning / verification search** (Scan's substitute for null-move — null-move is
   unsound in draughts because of zugzwang).
8. **Futility / razoring** at shallow depths (careful: zugzwang makes these risky; gate hard).
9. **Search enhancements:** internal iterative deepening when no TT move; multi-cut; PVS
   re-search windows.
10. **Parallel search:** Lazy SMP (shared TT, multiple threads) — straightforward, matches Scan.
11. **Time management:** soft/hard budgets, node budgets for reproducible tests, ponder mode.

### 7.1 Repetition / TT interaction
- Store generation-age in TT entries; detect repetition by walking the game history backed by the
  Zobrist hash, treating a repeated position within the search path as a draw score.
- Draw scores must be handled carefully with the TT (path-dependent), as in chess engines.

### 7.2 Node targets (perf budget)
- Target **≥ 10M nodes/sec/core**, stretch **≥ 30M**, in midgame (draughts move gen is far
  cheaper than chess).
- Depth target: **≥ 18–24 plies** in 1s midgame, deeper as pieces leave (forced captures + DBs).

---

## 8. Evaluation

Staged so each stage is independently shippable and measurable:

**Stage E0 — Material + PST.**
`score = Σ man_value + Σ king_value + Σ PST[p][sq]` with tempo. Man ≈ 100cp, King ≈ 300cp
(i.e. king ≈ 3 men — the "rule of thumb" Scan started from). Hand-tuned PSTs. This alone beats
casual players.

**Stage E1 — Hand features.**
Add: mobility, back-rank/promotion-route control, connected/defended pieces, edge/center
structure, first-king bonus, king-vs-men imbalance.

**Stage E2 — Pattern evaluation (the big jump, Scan/Kingsrow method).**
- Divide the 32 squares into **overlapping local windows** — Thai is 8×8, so use
  overlapping **4×4** (Scan-style, ~9–16 windows) and/or **4×2 / 4×6** (Kingsrow-style) regions.
- Each window maps its local contents (empty / each side's men / kings) to a **pattern index**.
- Look up a **learned weight** per (pattern index, phase). Add the local scores.
- Weights are **learned from self-play + position outcomes**, not hand-tuned.
- Implementation: chunk tables for O(#windows) lookups (py-draughts "TurboEngine" folds PSTs
  into 9×128-entry chunk tables for O(9) eval — same idea).

**Stage E3 — Shallow neural net / NNUE (optional upgrade).**
A small network (Kingsrow is a "very shallow NN" over pattern indices; modern path is NNUE —
incrementally-updated quantized eval). Ship only if it clearly beats E2 in self-play.

**Stage E4 — Self-play reinforcement.**
AlphaZero-style or simpler: generate games from the current best engine with randomized
openings, tune weights (Texel-style logistic regression on game results → pattern weights), or
train the NN. Iterate until strength plateaus.

---

## 9. Endgame databases

- **WLD bitbases** via **retrograde analysis** (Thompson/Bellman style, as in Chinook/Scan).
  - Values: WIN / LOSS / DRAW (+ optionally **DTW/DTZ** distance-to-win for play strength).
  - Thai starts at 16 pieces so small endgames are very common (forced captures strip pieces fast).
  - **Target: all positions with ≤ N pieces.** Start N=6, extend to 8, then attempt full solve
    (≤16) if storage/compute allow (see §12).
- **Bitbases** (WLD-only, no distance) are tiny and fast — the Scan approach (`bb-size = 6`,
  ~2 GiB RAM). Query at search leaves when `piece_count ≤ N`.
- **Licensing:** generate our own from our move generator (do not depend on Kingsrow/Scan data).
- **Correctness:** validate against a second independent retrograde implementation for small N
  and against known solved positions.

---

## 10. Opening book

- Format: compact binary, position-hash → list of (move, weight/count).
- Sources: (a) **self-play** generation (primary, no licensing issues), (b) optional human games
  from public PDN where licensing allows.
- Build pipeline: generate millions of games with varied time controls and score caps, filter by
  opening diversity, weight by results.
- Book usage: pick moves from book early, then hand off to search; book can be disabled for testing.

---

## 11. Interfaces, notation & protocols

### 11.0 Frozen notation (normative)

**Square numbering (external):** standard 1..32 on the dark squares, exactly as in §3.1
(`external = internal + 1`).

**Move notation (TCI/PDN, frozen):**
- Quiet move: `NN-NN` (e.g. `24-20`).
- Single capture: `NNxNN` (e.g. `26x17`).
- Multi-capture: `NNxNNxNN[xNN...]` — list **every landing square** in order (e.g. `32x23x14`).
- Promotion is **not** marked (as in draughts/checkers): the `to` square reveals it.

**Position FEN (frozen), format:**
```text
<stm>:W<whitesqlist>:B<blacksqlist>[:<rule50>]
```
- `<stm>` ∈ {`W`,`B`}.
- Each piece list: comma-separated **external** square numbers (1..32); a king is prefixed `K`.
- Ranges may be hyphenated for compactness.
- Examples (external 1..32 numbering):
  ```text
  start   W:W25-32:B1-8          (also `position startpos`)
  kings   B:WK5,BM3-7:W24-30,K31
  ```
This is a superset of the widely-used draughts FEN, adapted to 8-piece makhos setup.

**PDN:** standard PDN game records (move numbers, `x`/`-`, comments, variations) using the
notation above; a PDN `[FEN "..."]` tag carries the frozen FEN string.

### 11.1 TCI v1 (Thai Checkers Interface)

A UCI-like line protocol for GUIs.
  ```text
  tci
  id name MakHosFish <ver>
  id author ...
  option name Hash type spin default 128 min 1 max 32768
  option name Threads type spin default 1 min 1 max 64
  option name RuleSet type combo default thai var thai var international var russian ...
  uciok / tciok
  position startpos | position fen <fen> moves <moves>
  go wtime <ms> btime <ms> winc <ms> binc <ms> | go depth <n> | go movetime <ms> | go nodes <n>
  info depth <n> seldepth <n> score cp <x> nodes <n> nps <n> time <ms> pv <moves>
  bestmove <move>
  ```
- **DXP / Hub compatibility (optional):** bridges to existing draughts tooling (lidraughts, py-draughts
  HubEngine) for cross-testing.
- **CLI:** `makhosfish perft <depth>`, `makhosfish bench`, `makhosfish play`, `makhosfish selfplay`, `makhosfish tune`.

---

## 12. Solvability analysis (why this is a realistic "solve the game" project)

Chinook solved 8×8 **English** checkers (start = 24 pieces) in 2007:
- ≤8 pieces: 4.4×10¹¹ positions.
- ≤10 pieces: 3.9×10¹³ positions (237 GB compressed).

Thai starts with only **16 pieces**, so the full state space is *smaller* than English checkers':
- Stretch goal: **strongly solve Thai checkers** — compute WLD for the entire legal position space.
- Feasible plan: build retrograde DB up to 8–10 pieces first (practical strength immediately),
  then extend toward a full solve using a proof-tree manager + forced-capture reductions.
- Deliverable framing: v1 = superhuman practical play; v2 = solved (or near-solved) game.

---

## 13. Performance targets & benchmarks

| Metric | Target | Stretch |
|--------|--------|---------|
| Move gen (legal) | < 1 µs | < 0.2 µs |
| Make/unmake | < 100 ns | < 30 ns |
| Search NPS/core (midgame) | 10M | 30M+ |
| Depth @ 1s midgame | 18 | 24+ |
| Perft(10) from start | < 1 s | < 0.2 s |
| Strength | beats all makhos apps | beats strong masters; ~solved |
| Memory | default TT 128 MB, bitbases ≤ 2 GB | scalable |

Benchmark harness: fixed `perft` suite, fixed position suite with known best moves, self-play Elo
gauntlet vs previous versions, and (if any) external makhos programs. `nps` reporting from the
protocol.

---

## 14. Testing & correctness strategy

1. **Perft** (the anchor): counts of leaf nodes at depth d from start and from a curated set of
   positions. Freeze reference counts (computed by *two independent* implementations) into test
   fixtures. This catches exactly the subtle Thai rules (immediate removal, king landing).
2. **Variant cross-validation:** implement International/Russian/English rule configs and match
   published perft numbers (BikDam reference for International; well-known English counts). If our
   generic generator matches the known variants, confidence in the Thai path rises sharply.
3. **Unit tests** for each rule in §2.2 (esp. D2 mid-capture promotion, D3 force-continue, D5
   immediate-removal geometry).
4. **PDN replay:** parse and replay real makhos games; assert legality/no crashes.
5. **Search/eval sanity:** mate-in-N suites, endgame DB agreement, repetition/draw correctness.
6. **Regression:** perft + self-play gauntlet must not regress across commits.
7. **Fuzzing:** random legal games for move-gen/make-unmake invariance (`make` then `unmake`
   restores exact hash + bitboards); property tests on Zobrist incremental update.

---

## 15. Decisions (RESOLVED — blockers cleared)

All implementation blockers are resolved. Changeable later, but **frozen for M0**:

| # | Decision | Resolution | Where |
|---|----------|------------|-------|
| 1 | Language | **C++20** | §4 |
| 2 | Board layout | **32-bit dense**, internal 0..31, external 1..32 | §5.1, §11.0 |
| 3 | First move default | **White** (configurable) | §2.2 D1 |
| 4 | Mid-capture promotion (D2) | **Continue as man**; promote only if turn ends on back rank | §2.2 D2 |
| 5 | Force-continue (D3) | **true** | §2.2 D3 |
| 6 | Max-capture (D4) | **false** (free route choice) | §2.2 D4 |
| 7 | Immediate removal (D5) | **true** | §2.2 D5, §3.4 |
| 8 | Draws (D6) | threefold repetition; 25-ply no-progress; insufficient material | §3.6 |
| 9 | v1 scope | **Practical superhuman first** (bitbases ≤8, book, tuned eval); full solve → v2 | §12, §16 |
| 10 | Protocol | **bespoke TCI v1** (+ optional DXP/Hub bridge later) | §11.1 |
| 11 | Notation/FEN | **frozen** in §11.0 | §11.0 |
| 12 | Perft ground truth | **two independent move generators** (fast + slow reference) cross-checked, plus cross-variant validation | §14, §20.5 |
| 13 | Repo/licensing | our code **MIT**; all data self-generated | §16 |

Remaining *implementation* decisions (not blockers, settled inside M0–M1):
`open_addressing` vs `bucketed` TT, `copy-make` vs `make/unmake` at the leaf, and the exact
pattern-window shapes — all decided by benchmark evidence, not fiat.

---

## 16. Roadmap / milestones

- **M0 — Skeleton & rules.** Position struct, move gen (quiet + capture), make/unmake, perft.
  Exit: perft passes on Thai fixtures + International/English cross-check.
- **M1 — Search.** PVS + TT + iterative deepening + basic move ordering + time management.
  Exit: plays legally, beats random + a simple 1-ply engine.
- **M2 — Eval E0/E1.** Material + PST + hand features. Exit: beats casual mobile makhos app.
- **M3 — Protocol + CLI + PDN/FEN.** Exit: drivable from a GUI; games loadable.
- **M4 — Pattern eval (E2) + tuning.** Self-play datagen + weight tuning. Exit: clearly beats M2.
- **M5 — Endgame bitbases (≤6, then ≤8).** Exit: perfect play in small endgames.
- **M6 — Opening book + parallel search (Lazy SMP).** Exit: strong, fast, tournament-ready.
- **M7 — NNUE (E3) if it wins + extend DBs toward full solve.** Exit: near/at solved.

---

## 17. Risks

- **R1. Rule ambiguity.** Mitigated by config + perft + tests (§2.2, §14).
- **R2. Over-engineering eval before search is correct.** Mitigated by staged E0→E3 and perft gate.
- **R3. Endgame DB scale.** Mitigated by starting small (≤6), using bitbases, and single-machine
  feasibility analysis (§12).
- **R4. No external strong opponent to calibrate against.** Mitigated by self-play Elo ladder and
  optional DXP/Hub bridge to international engines for sanity (rules differ, so use as a sanity
  check only).
- **R5. Data licensing.** Mitigated by self-generated books/DBs.

---

## 18. References

Rules
- makhos-games rules (Thai competition rules) — sites.google.com/view/makhos-games/rules
- Checkers & Draughts Wiki, *Mak-hot* — checkers.fandom.com/wiki/Mak-hot
- draughts.io, *Thai Draughts* — draughts.io/thai-draughts
- rules.moddable.games, *Thai Draughts* (flying-king landing rule) — rules.moddable.games
- BoardGameGeek thread on Thai flying-king landing + no-max-capture

Engines / technique
- Scan (F. Letouzey) — github.com/rhalbersma/scan; pattern eval + PVS + bitbases
- Kingsrow (E. Gilbert) — edgilbert.org/Checkers/KingsRow.htm; pattern NN eval, 10-piece DBs
- Chinook — Solving Checkers (Science 2007); chinook endgame DB numbers
- py-draughts / TurboEngine docs — bitboard 8×8 + Scan-style eval
- Daan Noordenbos, *Draughts move generator* (bit trick move gen)
- TalkChess, *Checkers Bitboard representation* (BORDERS layout)
- Chess Programming Wiki — draughts, 8×8 board, bitboards

Variant validation
- BikDam perft reference counts (international draughts)
- Standard 1–32 checkers numbering/notation (Chinook notation page)

---

## 19. Definition of done (v1)

- [ ] Thai rules exact; all §2.2 decisions encoded, tested, documented.
- [ ] Perft passes on frozen fixtures (Thai + cross-variant).
- [ ] PVS search reaches target NPS/depth with TT, ordering, LMR, forward pruning.
- [ ] Pattern eval trained by self-play beats the PST baseline in an Elo gauntlet.
- [ ] Endgame bitbases ≤8 pieces generated and queried correctly.
- [ ] Opening book generated and selectable.
- [ ] TCI protocol + CLI + PDN/FEN working end-to-end.
- [ ] Benchmarks reproducible; no regression across commits.

---

## 20. Implementation plan (go-time)

### 20.1 Repository layout
```text
thaichecker-engine/
├── CMakeLists.txt
├── README.md
├── spec.md
├── src/
│   ├── core/         types.hpp, bitboard.{hpp,cpp}, move.hpp, position.{hpp,cpp},
│   │                 movegen.{hpp,cpp}, zobrist.{hpp,cpp}, rules.hpp
│   ├── search/       search.{hpp,cpp}, tt.{hpp,cpp}, moveorder.{hpp,cpp}, timeman.{hpp,cpp}
│   ├── eval/         eval.{hpp,cpp}, pst.hpp, pattern.{hpp,cpp}, nnue.{hpp,cpp}   (nnue → M7)
│   ├── book/         book.{hpp,cpp}
│   ├── egdb/         bitbase.{hpp,cpp}, retrograde.{hpp,cpp}
│   ├── protocol/     tci.{hpp,cpp}, pdn.{hpp,cpp}, fen.{hpp,cpp}
│   └── main.cpp
├── tools/            perft.cpp, datagen.cpp, tune.cpp, retrograde_main.cpp
├── tests/            reference_movegen.cpp (slow, independent),
│                     test_movegen.cpp, test_rules.cpp, test_zobrist.cpp,
│                     test_fen.cpp, perft_fixtures.txt
├── data/             pst/, patterns/, book.bin, egdb/
└── .github/workflows/ci.yml
```

### 20.2 Build targets & options (CMake, C++20)
Targets:
- `makhosfish_core` — static lib (core + search + eval + book + egdb + protocol).
- `makhosfish` — protocol/CLI executable.
- `makhosfish_tools` — `perft`, `datagen`, `tune`, `retrograde_main`.
- `makhosfish_tests` — registered with CTest.

Options: `MAKHOS_NATIVE=ON` (`-march=native`), `MAKHOS_SANITIZE=OFF` (`-fsanitize=address,undefined`),
`MAKHOS_LTO=ON` (release), `MAKHOS_ARCH` (portable builds for distribution).

CI (GitHub Actions): matrix {Linux clang, Linux gcc, macOS clang} × {Release, ASan/UBSan};
runs `ctest` (perft fixtures + unit tests) and `makhosfish bench`.

### 20.3 Core API signatures (frozen for M0)
```cpp
namespace mhf {

enum class Color : uint8_t { White = 0, Black = 1 };

struct Move {                 // 64-bit packed; cheap to copy
  uint8_t  from;              // 0..31
  uint8_t  to;                // 0..31
  uint8_t  n_captured;        // 0..8
  bool     promotes;
  uint32_t captured;          // bitmask of removed internal squares
};

struct Position {
  uint32_t wm, wk, bm, bk;    // bitboards (§5.1)
  uint8_t  stm;               // Color
  uint16_t game_ply;          // half-move counter
  uint16_t rule50;            // plies since last man move / capture (draw rule)
  uint64_t hash;              // Zobrist (stm + all 4 bitboards)
};

// Move generation (pseudo-legal == legal for draughts; no check/pins).
int  generate_moves    (const Position&, const RuleSet&, Move* out, int cap);
int  generate_captures (const Position&, const RuleSet&, Move* out, int cap);

// Apply / revert (exact inverse; hash + bitboards + counters restored).
void make_move  (Position&, const Move&, const RuleSet&);
void unmake_move(Position&, const Move&);

uint64_t perft(Position&, const RuleSet&, int depth);   // for tests/tools

// I/O (frozen formats §11.0).
bool        parse_fen (const std::string&, Position&, const RuleSet&);
std::string to_fen    (const Position&, const RuleSet&);
bool        parse_move(const Position&, const std::string&, Move&);
std::string to_string (const Move&);
} // namespace mhf
```

### 20.4 Perft fixtures (initial set)
`tests/perft_fixtures.txt`, one `FEN ; rule_set ; D1 n1 ; D2 n2 ; ...` per line. Start with:
1. **Start position**, depths 1..8.
2. **King long-capture + immediate landing** — validates the Thai landing rule.
3. **Immediate-removal geometry** — a piece vacated mid-chain opens a later diagonal.
4. **Mid-capture promotion** — man reaches back rank mid-chain and must continue as a man (D2).
5. **Forced-continue / 180° turn** — a chain that doubles back.
6. **Compulsory capture** — positions where quiet moves must be suppressed.
Reference counts are generated by **both** the fast generator and the slow independent reference
(`tests/reference_movegen.cpp`) and must agree before a fixture is committed.

### 20.5 Ground-truth workflow for perft (blocker #4)
1. Write a deliberately-simple, obviously-correct **reference movegen** (arrays, loops, no bit
   tricks) directly from §3 rules.
2. Cross-validate the fast bitboard generator against it for all depths up to a limit, and via
   random-position fuzzing (§14.7).
3. Cross-validate the *generic* generator against **published** International/Russian/English
   perft counts by switching the RuleSet. Matching a known variant is strong evidence the Thai
   path (a RuleSet delta from it) is correct.
4. Only then freeze Thai counts as CI fixtures.

### 20.6 Data file formats (frozen headers; versioned)
- **Pattern weights** `.mhpw`: magic `MHPW`, `u16 version`, `u16 n_windows`, window shape,
  `u16 n_classes`, `u16 n_phases` → then `int16` weights (little-endian).
- **Opening book** `.mhbk`: magic `MHBK`, `u16 version`, `u32 n_entries` → entries
  `{ u64 hash; u16 count; (u16 move; u16 weight)[count] }`.
- **Bitbase/EGDB** `.mhdb`: magic `MHDB`, `u16 version`, `u8 n_pieces`, `u32 rule_flags`,
  `u8 compression` → 2-bit/piece WLD payload; optional `u8` DTW/DTZ side table.
- **PST** `.mhpst`: plain text (dev); compiled to `constexpr` arrays for release.

### 20.7 Definition of "M0 done"
- `makhosfish_tools perft <fen> <d>` runs and matches **all** frozen fixtures.
- Fast generator == reference generator across the fixture set and fuzz runs.
- `ctest` green on the CI matrix; ASan/UBSan clean.
- `makhosfish bench` reports move-gen and make/unmake throughput meeting §13 targets.
