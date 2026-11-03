// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Siravid Thongsook and MakHosFish contributors

#pragma once
#include "core/types.hpp"
#include "core/move.hpp"
#include "core/rules.hpp"

namespace mhf {

struct Position {
  uint32_t wm = 0, wk = 0, bm = 0, bk = 0;
  uint8_t  stm = WHITE;
  uint16_t game_ply = 0;
  uint16_t rule50 = 0;
  uint64_t hash = 0;

  uint32_t men(Color c) const { return c == WHITE ? wm : bm; }
  uint32_t kings(Color c) const { return c == WHITE ? wk : bk; }
  uint32_t pieces(Color c) const { return men(c) | kings(c); }
  uint32_t occupied() const { return wm | wk | bm | bk; }
  uint32_t empty() const { return ~occupied() & 0xFFFFFFFFu; }
  Color    side() const { return Color(stm); }

  int piece_count() const { return popcount(occupied()); }
};

// Full hash from scratch (used to initialize and to validate incremental builds).
uint64_t recompute_hash(const Position& p);

// Starting position: 8 men per side on the two nearest rows (§3.1).
Position start_position(const RuleSet& r = RuleSet{});

// Apply / revert a move. do_move updates hash, counters and side to move.
// undo_move restores bitboards, side, game_ply and hash exactly; the caller
// supplies the previous rule50 (the search keeps it on its own stack).
void do_move(Position& p, const Move& m, const RuleSet& r);
void undo_move(Position& p, const Move& m, uint16_t prev_rule50, const RuleSet& r);

} // namespace mhf
