# Contributing to MakHosFish

Thanks for helping with the first Stockfish-class engine for Thai checkers (หมากฮอส / Makhos).

Contributions of every size are welcome: rule references, perft positions, tests, eval ideas,
strength results, documentation, and code.

## Ways to help

- **Rules & perft.** Send FEN positions with counts computed by an independent reference
  implementation. Discrepancies are the most valuable bug reports this project can get.
- **Strength.** Run a `gauntlet` and post the numbers (depth, games, openings, Elo, dataset
  size). Negative results are useful too — say what you tried.
- **Portability.** Builds and test runs on compilers/OSes other than clang/macOS.
- **Docs.** Anything unclear in `README.md`, `docs/`, or `spec.md`.

## Development setup

Requires CMake ≥ 3.20 and a C++20 compiler. The only dependency is the vendored
`third_party/doctest/doctest.h`; nothing is downloaded at build time.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/makhosfish_tests          # unit + perft (fast)
MAKHOS_SLOW=1 ./build/makhosfish_tests   # + the deep perft fixtures
ctest --test-dir build --output-on-failure
```

Sanitizer build (keep it clean):

```bash
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DMAKHOS_SANITIZE=ON
cmake --build build-asan -j && ./build-asan/makhosfish_tests
```

On macOS you may need to pass the SDK sysroot, see [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md).

## Before you open a pull request

1. `./build/makhosfish_tests` is green.
2. The ASan/UBSan build is green.
3. `./build/makhosfish startpos 9` still prints **114530830** — Thai perft(9) is frozen.
4. New behaviour ships with a test; a correctness change never "fixes" a fixture by editing it.
5. For eval/search changes, include a `gauntlet` showing the change helps (games, depth, Elo).

## Code conventions

- C++20, namespace `mhf`, `-Wall -Wextra -Wpedantic` clean.
- Comments are short and explain **why**, not what.
- Keep the hot path allocation-free and free of hidden global state.
- Formats are frozen (FEN, move notation, `.mhpw`, TCI v1). Changing one requires a `spec.md`
  update and matching fixture updates in the same PR.
- The Thai ruleset is the product. Never change Thai behaviour to accommodate a variant.

See [AGENTS.md](AGENTS.md) for the repository map and the full set of golden rules, and
[`spec.md`](spec.md) for the authoritative design contract.

## Commits and pull requests

- Commit messages: `type(scope): summary`, e.g. `feat(search): add aspiration windows`.
- Keep commits focused; one logical change per commit.
- PRs should describe the change, the tests run, and (for strength changes) the measured effect.
- CI must pass on Ubuntu and macOS, both release and sanitizer configurations.

## License of contributions

MakHosFish is licensed under the **GNU General Public License v3.0 or later**
(`GPL-3.0-or-later`). By submitting a pull request you agree that your contribution is licensed
under the same terms, and you confirm you wrote it or that it comes from a GPL-3.0-compatible
source with proper attribution.

Do not copy code or data from other engines, books, or game databases. Evaluation weights and
datasets must be self-generated; the only third-party code is the vendored `doctest`.

## Reporting bugs and security issues

- Bugs and feature requests: open an issue using the templates.
- Security problems: **do not** open a public issue — see [SECURITY.md](SECURITY.md).

This project follows the [Contributor Covenant](CODE_OF_CONDUCT.md). Be kind and technical.
