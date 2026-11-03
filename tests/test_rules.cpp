// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Siravid Thongsook and MakHosFish contributors

#include "doctest/doctest.h"
#include "core/movegen.hpp"
#include "core/position.hpp"

using namespace mhf;

TEST_CASE("first_move configurable") {
  RuleSet w; w.first_move = WHITE;
  RuleSet b; b.first_move = BLACK;
  CHECK(start_position(w).stm == WHITE);
  CHECK(start_position(b).stm == BLACK);
}

TEST_CASE("start_position respects rows_of_pieces") {
  const Position t = start_position(thai_rules());
  CHECK(popcount(t.wm) == 8);
  CHECK(popcount(t.bm) == 8);
  CHECK(t.bm == 0x000000FFu);   // rows 0-1
  CHECK(t.wm == 0xFF000000u);   // rows 6-7

  const RuleSet e = english_rules();
  const Position pe = start_position(e);
  CHECK(popcount(pe.wm) == 12);  // 3 rows
  CHECK(popcount(pe.bm) == 12);
  CHECK(pe.bm == 0x00000FFFu);
  CHECK(pe.wm == 0xFFF00000u);
  CHECK(legal_moves(pe, e).size() == 7);  // published English 8x8 start count
}

TEST_CASE("force_continue=false permits stopping a capture early") {
  RuleSet r = thai_rules();
  r.force_continue = false;
  Position p; p.stm = WHITE;
  p.wk = bit(27);
  p.bm = bit(23) | bit(9);
  p.hash = recompute_hash(p);

  auto mv = legal_moves(p, r);
  bool full = false, partial = false;
  for (const auto& m : mv) {
    if (m.to == 5 && m.n_captured == 2) full = true;
    if (m.to == 18 && m.n_captured == 1) partial = true;
  }
  CHECK(full);      // forced-continue OFF: the full chain is a legal choice
  CHECK(partial);   // ... and so is stopping after one capture
}

TEST_CASE("no legal move loses: blocked side has zero moves") {
  RuleSet r = thai_rules();
  Position p; p.stm = BLACK;
  p.bm = bit(0);                          // black man row0,col1
  p.wm = bit(4) | bit(5) | bit(9);        // blocks forward; no capture (SW off-board, SE land occupied)
  p.hash = recompute_hash(p);
  CHECK(legal_moves(p, r).empty());
}
