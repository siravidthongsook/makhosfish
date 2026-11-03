// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Siravid Thongsook and MakHosFish contributors

#include "doctest/doctest.h"
#include "test_util.hpp"
#include "core/movegen.hpp"
#include "core/position.hpp"
#include "core/zobrist.hpp"

#include <random>
#include <vector>

using namespace mhf;

TEST_CASE("incremental hash matches recompute; undo restores exact state") {
  RuleSet r = thai_rules();
  std::mt19937_64 rng(777);

  for (int game = 0; game < 80; ++game) {
    Position p = start_position(r);
    std::vector<Position> saved;
    std::vector<Move> moves;

    for (int i = 0; i < 60; ++i) {
      CHECK(p.hash == recompute_hash(p));
      auto mv = legal_moves(p, r);
      if (mv.empty()) break;
      std::uniform_int_distribution<size_t> d(0, mv.size() - 1);
      Move m = mv[d(rng)];
      saved.push_back(p);
      moves.push_back(m);
      do_move(p, m, r);
    }

    for (size_t k = moves.size(); k-- > 0;) {
      undo_move(p, moves[k], saved[k].rule50, r);
      CHECK(p.wm == saved[k].wm);
      CHECK(p.wk == saved[k].wk);
      CHECK(p.bm == saved[k].bm);
      CHECK(p.bk == saved[k].bk);
      CHECK(p.stm == saved[k].stm);
      CHECK(p.game_ply == saved[k].game_ply);
      CHECK(p.hash == saved[k].hash);
      CHECK(p.hash == recompute_hash(p));
    }
  }
}
