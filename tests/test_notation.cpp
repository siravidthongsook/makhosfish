// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Siravid Thongsook and MakHosFish contributors

#include "doctest/doctest.h"
#include <algorithm>
#include <random>
#include <string>
#include "core/movegen.hpp"
#include "core/position.hpp"
#include "protocol/fen.hpp"

using namespace mhf;

namespace {
int count_sep(const std::string& s, char c) {
  return (int)std::count(s.begin(), s.end(), c);
}
} // namespace

TEST_CASE("quiet notation round-trips from the start") {
  RuleSet r = thai_rules();
  Position p = start_position(r);
  for (const Move& m : legal_moves(p, r)) {
    const std::string s = move_to_notation(p, r, m);
    Move back;
    REQUIRE(parse_move(p, s, r, back));
    CHECK(back == m);
    CHECK(count_sep(s, '-') == 1);
  }
}

TEST_CASE("multi-jump lists every landing square") {
  RuleSet r = thai_rules();
  Position p;
  REQUIRE(parse_fen("W:W25:B15,22", p, r));
  const std::string s = move_to_notation(p, r, legal_moves(p, r).front());
  CHECK(s == "25x18x11");
  CHECK(count_sep(s, 'x') == 2);

  Move back;
  REQUIRE(parse_move(p, s, r, back));
  CHECK(back.is_capture());
  CHECK(back.n_captured == 2);
  CHECK(back.from == 24);  // external 25
  CHECK(back.to == 10);    // external 11
}

TEST_CASE("king multi-jump notation round-trips") {
  RuleSet r = thai_rules();
  // Search placements for a white king with a forced >=2 capture chain.
  bool found = false;
  for (int k = 0; k < 32 && !found; ++k)
    for (int a = 0; a < 32 && !found; ++a)
      for (int b = 0; b < 32 && !found; ++b) {
        if (a == b || a == k || b == k) continue;
        Position p;
        p.wk = bit(k); p.bm = bit(a) | bit(b); p.stm = WHITE;
        p.hash = recompute_hash(p);
        for (const Move& m : legal_moves(p, r)) {
          if (!(m.is_capture() && m.n_captured >= 2)) continue;
          const std::string s = move_to_notation(p, r, m);
          CHECK(count_sep(s, 'x') >= 2);
          Move back;
          REQUIRE(parse_move(p, s, r, back));
          CHECK(back == m);
          found = true;
          break;
        }
      }
  CHECK(found);  // a king multi-jump exists on an 8x8 board
}

TEST_CASE("notation round-trips over random games") {
  RuleSet r = thai_rules();
  std::mt19937_64 rng(0xBADC0DEull);
  int multi = 0;
  for (int g = 0; g < 25; ++g) {
    Position p = start_position(r);
    for (int ply = 0; ply < 60; ++ply) {
      const std::vector<Move> ms = legal_moves(p, r);
      if (ms.empty()) break;
      for (const Move& m : ms) {
        const std::string s = move_to_notation(p, r, m);
        Move back;
        REQUIRE(parse_move(p, s, r, back));
        REQUIRE(back == m);
        if (m.n_captured >= 2) ++multi;
      }
      std::uniform_int_distribution<int> d(0, (int)ms.size() - 1);
      do_move(p, ms[d(rng)], r);
    }
  }
  CHECK(multi > 0);  // the sample must exercise multi-jumps
}

TEST_CASE("endpoint-only capture notation is rejected") {
  RuleSet r = thai_rules();
  Position p;
  REQUIRE(parse_fen("W:W25:B15,22", p, r));
  Move m;
  CHECK_FALSE(parse_move(p, "25x11", r, m));  // missing the intermediate landing
}
