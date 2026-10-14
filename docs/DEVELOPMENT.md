# Development guide

## Prerequisites

- CMake ≥ 3.20 and a C++20 compiler (clang or gcc).
- **macOS note:** on this machine you must pass the Xcode SDK sysroot to CMake, otherwise linking
  fails (`tapi error: malformed file … unknown architecture arm64e`):

  ```
  -DCMAKE_OSX_SYSROOT=/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
  ```

The only third-party dependency is the vendored `third_party/doctest/doctest.h`.

## Build options (`CMakeLists.txt`)

| Option | Default | Effect |
|--------|---------|--------|
| `MAKHOS_NATIVE` | `ON` | `-march=native` (turn OFF for portable/CI builds) |
| `MAKHOS_SANITIZE` | `OFF` | ASan + UBSan, no LTO |
| `MAKHOS_LTO` | `ON` | link-time optimization in release |

Targets: `makhosfish_core` (static lib), `makhosfish` (CLI/protocol), `makhosfish_perft`,
`makhosfish_bench`, `makhosfish_datagen`, `makhosfish_tune`, `makhosfish_tests`.

## Testing

```bash
cmake --build build -j
./build/makhosfish_tests              # unit + perft (fast)
ctest --test-dir build                # registered tests
MAKHOS_SLOW=1 ./build/makhosfish_tests   # + the deep perft fixtures
```

Test files live in `tests/` (doctest). Two are worth calling out:

- `tests/reference_movegen.{hpp,cpp}` — a deliberately simple, obviously-correct move generator.
  `test_perft_agreement.cpp` asserts it agrees with the fast bitboard generator, which is how we
  trust the exotic Thai rules.
- `tests/perft_fixtures.txt` — frozen Thai counts (`<FEN> ; D<d> <count> ; …`).
  `tests/perft_variants.txt` — published English/Russian counts (cross-check only).

### Regression gates (run before every commit)

1. `./build/makhosfish_tests` green.
2. ASan/UBSan build (`build-asan`) green.
3. `./build/makhosfish startpos 9` → **114530830** (unchanged).
4. For eval/search changes: a `gauntlet` that shows the change helps.

## Perft workflow

Perft is the ground truth for the rules. To add or change a fixture:

1. Compute the count with the fast generator **and** the reference generator.
2. They must agree; only then freeze the number in `tests/perft_fixtures.txt`.
3. Never "fix" a discrepancy by editing the fixture — find the rule/generator bug.

Deep counts are gated behind `MAKHOS_SLOW=1` so the default suite stays quick.

## Data pipeline (E2 weights)

```bash
# 1. Self-play data: <games> <depth> <opening> <out> [seed] [e0|e1] [max_plies]
./build/makhosfish_datagen 20000 6 6 /tmp/data.txt 1
#    (runs single-threaded; launch several with different seeds and `cat` the files)

# 2. Train: <data> <out.mhpw> [epochs] [lr] [init|-] [max_samples]
./build/makhosfish_tune /tmp/data.txt data/patterns/thai_e2.mhpw 80 0.05 - 6000000
#    prints train/val loss per epoch; keeps the best validation epoch.

# 3. Measure: <games> <depth> <A> <B> <opening>
./build/makhosfish --pattern data/patterns/thai_e2.mhpw gauntlet 1000 6 e2 e1 4
```

Reference numbers from the M4 run: 120k games → 9.7M positions; val loss 0.6644 → 0.6303;
E2 vs E1 = **+102 Elo @ depth 6**, **+128 Elo @ depth 9**. The gauntlet alternates colours and
uses random openings for diversity; use `e2 vs e2` and `e1 vs e1` as ~50% controls.

## Adding a feature

- Test-first where practical; every behavior change ships with a test.
- Keep the search/eval hot path allocation-free and free of hidden global state.
- Correctness changes must keep perft(9) unchanged.
- For strength changes, land with a gauntlet that shows the change helps.
- Comment only non-obvious code, and explain **why** (matching the existing `src/` style).

## Debugging

- `./build/makhosfish go <fen> <depth> e1` prints a full-chain PV; `bench` gives NPS.
- `./build/makhosfish pdn <file> [--fens]` replays a game and prints the FEN after each ply.
- `printf 'tci\nposition startpos\ngo depth 6\nquit\n' | ./build/makhosfish tci` for a quick
  protocol check.
- Build `build-asan` and run the tests to catch memory/UB issues.

## Git

Commit style: `type(scope): summary` (e.g. `feat(m4): E2 pattern eval + tuning`). Ask before
committing. Historical commits: `0237fac` (M0), `fce2d2c` (M1/M0.5), `c4fbae1` (M2),
`9372f09` (M3), `37a8665` (M4).
