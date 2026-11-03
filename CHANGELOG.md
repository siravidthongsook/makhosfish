# Changelog

All notable changes to this project are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project
adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html) once versioned releases begin.

## [Unreleased]

### Added
- **MultiPV 1–5** in the TCI `go`/`search` output (`multipv` field on `info` lines), used by the
  web board to draw alternative lines.
- Web board: alternative-line arrows, eval bar, and line previews.

## [0.1.0] — 2026-10-14

First public snapshot of the engine. Milestones M0–M5 (partial) are complete.

### Added
- **M0 — rules and movegen.** Thai checkers on a 32-square dense bitboard: flying kings that must
  land immediately behind the captured piece, captures removed mid-sequence, compulsory capture, no
  maximum-capture rule, no stalemate. Perft suite cross-checked against an independent reference
  generator; Thai perft(9) = 114,530,830.
- **M0.5 — variants.** English, Russian, and Brazilian rules as a perft cross-check harness only.
- **M1 — search.** PVS with transposition table, aspiration windows, LMR, killers/history, IID,
  single-reply extension, iterative deepening, and time management.
- **M2 — evaluation.** E0 (material + PST) and E1 (hand features: mobility, captures, defended men,
  first king, pre-crown), plus a self-play gauntlet.
- **M3 — interfaces.** TCI v1 protocol (UCI-shaped) with an asynchronous search thread, full
  multi-jump notation, FEN and PDN support, and an interactive CLI.
- **M4 — pattern evaluation.** E2 learned 4×4 pattern windows with exact colour antisymmetry;
  self-play data generation (`makhosfish_datagen`) and Texel-style tuning (`makhosfish_tune`).
  E2 vs E1: +102 Elo @ depth 6, +128 Elo @ depth 9.
- **M5 (in progress).** WLD endgame bitbases: combinatorial `EgdbLayout`, backward-induction
  solver to ≤4 pieces with a colour-mirror invariant, and a probe API.
- **Tooling.** `makhosfish_perft`, `makhosfish_bench`, `makhosfish_datagen`, `makhosfish_tune`.
- **Web board.** Optional local browser UI (`web/`) with a Node bridge that speaks TCI to a child
  engine process.
- **CI.** GitHub Actions matrix: Ubuntu + macOS, release + ASan/UBSan.

[Unreleased]: https://github.com/siravidthongsook/makhosfish/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/siravidthongsook/makhosfish/releases/tag/v0.1.0
