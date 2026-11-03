// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Siravid Thongsook and MakHosFish contributors

#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "core/position.hpp"
#include "core/rules.hpp"

namespace mhf {

// E2 pattern evaluation (spec §8, stage E2).
//
// The 32 dark squares are covered by six overlapping windows; each window is a
// 4-row x 2-file region of the internal (row, file) lattice, i.e. a 4-board-row
// by 4-board-column block of dark squares. A window's 8 cells are encoded
// base-5 (empty / own man / own king / opp man / opp king) *from the scoring
// side's point of view*:
//
//     code = sum_i state(cell_i) * 5^i        (cell 0 = least significant)
//
// A learned int16 weight per (window, phase, code) is summed two-sided. The two
// perspectives are read from *paired* windows (see PAIR) so the score is exactly
// antisymmetric under the colour mirror for *any* weights:
//
//     pattern_white_minus_black =
//         sum_w T[w][ph][code(p, w, WHITE)] - T[PAIR[w]][ph][code(p, w, BLACK)]
//
// The windows come in rot180 mirror pairs (0<->5, 1<->4, 2<->3) whose cell
// orderings are aligned so code(mirror(p), w, BLACK) == code(p, PAIR[w], WHITE)
// digit-for-digit. Substituting w -> PAIR[w] in the black term then flips the
// whole sum, so an engine cannot acquire a colour bias from an untrained or
// asymmetric table, and the tuner needs no symmetry constraints.
class PatternEval {
 public:
  static constexpr int NWINDOWS = 6;
  static constexpr int CELLS = 8;        // 4 rows x 2 files
  static constexpr int CLASSES = 5;      // empty, own man, own king, opp man, opp king
  static constexpr int PHASES = 2;       // 0 = midgame, 1 = endgame
  static constexpr int TABLE = 390625;   // CLASSES^CELLS = 5^8
  static constexpr int PHASE_END_PIECES = 8;

  // WINDOWS[w][i] = internal square (0..31) of cell i of window w. Mirror pairs
  // (0,5), (1,4), (2,3) have WINDOWS[b][i] == 31 - WINDOWS[a][i].
  static constexpr int WINDOWS[NWINDOWS][CELLS] = {
      // 0  rows 0..3, files 0..1
      {0, 1, 4, 5, 8, 9, 12, 13},
      // 1  rows 0..3, files 2..3
      {2, 3, 6, 7, 10, 11, 14, 15},
      // 2  rows 2..5, files 0..1
      {8, 9, 12, 13, 16, 17, 20, 21},
      // 3  = rot180 mirror of 2 (rows 2..5, files 2..3, reversed order)
      {23, 22, 19, 18, 15, 14, 11, 10},
      // 4  = rot180 mirror of 1 (rows 4..7, files 2..3, reversed order)
      {29, 28, 25, 24, 21, 20, 17, 16},
      // 5  = rot180 mirror of 0 (rows 4..7, files 0..1, reversed order)
      {31, 30, 27, 26, 23, 22, 19, 18},
  };

  // PAIR[w] is the rot180 mirror of window w; PAIR is an involution.
  static constexpr int PAIR[NWINDOWS] = {5, 4, 3, 2, 1, 0};

  PatternEval() : w_(size_t(NWINDOWS) * PHASES * TABLE, 0) {}

  bool load(const std::string& path);
  bool save(const std::string& path) const;

  int phase_of(const Position& p) const {
    return p.piece_count() > PHASE_END_PIECES ? 0 : 1;
  }

  // white-minus-black pattern contribution (spec: E2 term).
  int score(const Position& p) const;

  // Per-window base-5 codes for both perspectives (tests + tuner). `phase` is
  // provided so callers can reuse the position's phase.
  void codes(const Position& p, int phase, int white[NWINDOWS], int black[NWINDOWS]) const;

  int16_t get(int w, int phase, int code) const { return w_[idx(w, phase, code)]; }
  void set(int w, int phase, int code, int16_t v) { w_[idx(w, phase, code)] = v; }

  int16_t* data() { return w_.data(); }
  const int16_t* data() const { return w_.data(); }
  size_t size() const { return w_.size(); }

  static int idx(int w, int phase, int code) {
    return (w * PHASES + phase) * TABLE + code;
  }

 private:
  std::vector<int16_t> w_;
};

// Process-wide weights used by `evaluate(..., EvalMode::E2)`. Zero by default,
// so an unloaded E2 is bit-identical to E1. Loaded once (CLI `--pattern` /
// TCI `setoption name PatternFile`); read-only in the search hot path.
PatternEval& active_pattern();

} // namespace mhf
