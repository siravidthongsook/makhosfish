#include "doctest/doctest.h"
#include "test_util.hpp"
#include "core/movegen.hpp"
#include "core/position.hpp"

using namespace mhf;

TEST_CASE("start position has 7 legal moves") {
  RuleSet r = thai_rules();
  CHECK(legal_moves(start_position(r), r).size() == 7);
}

TEST_CASE("captures are compulsory") {
  RuleSet r = thai_rules();
  Position p; p.stm = WHITE;
  p.wm = bit(24) | bit(26);
  p.bm = bit(22);
  p.hash = recompute_hash(p);

  auto mv = legal_moves(p, r);
  REQUIRE(mv.size() == 1);
  CHECK(mv[0].from == 26);
  CHECK(mv[0].to == 17);
  CHECK(mv[0].captured == bit(22));
  CHECK_FALSE(mv[0].promotes);
  CHECK(mv[0].is_capture());
}

TEST_CASE("king must land immediately behind the captured piece") {
  RuleSet r = thai_rules();
  Position p; p.stm = WHITE;
  p.wk = bit(28);   // row7,col0
  p.bm = bit(21);   // first blocker along NE
  p.hash = recompute_hash(p);

  auto mv = legal_moves(p, r);
  REQUIRE(mv.size() == 1);
  CHECK(mv[0].from == 28);
  CHECK(mv[0].to == 17);   // square immediately behind 21
  CHECK(mv[0].captured == bit(21));
  for (const auto& m : mv) { CHECK(m.to != 14); CHECK(m.to != 9); }
}

TEST_CASE("multi-capture chain with direction change") {
  RuleSet r = thai_rules();
  Position p; p.stm = WHITE;
  p.wk = bit(27);            // row6,col7
  p.bm = bit(23) | bit(9);   // 23 blocked NW, then 9 further along NW
  p.hash = recompute_hash(p);

  auto mv = legal_moves(p, r);
  REQUIRE(mv.size() == 1);
  CHECK(mv[0].from == 27);
  CHECK(mv[0].to == 5);
  CHECK(mv[0].n_captured == 2);
  CHECK(mv[0].captured == (bit(23) | bit(9)));
}

TEST_CASE("capture onto the back rank promotes") {
  RuleSet r = thai_rules();
  Position p; p.stm = BLACK;
  p.bm = bit(22);   // row5,col4
  p.wm = bit(26);   // SE neighbor, land row7
  p.hash = recompute_hash(p);

  auto mv = legal_moves(p, r);
  REQUIRE(mv.size() == 1);
  CHECK(mv[0].from == 22);
  CHECK(mv[0].to == 31);
  CHECK(mv[0].promotes);
}

TEST_CASE("free-landing variants allow landing farther beyond the victim") {
  RuleSet r = thai_rules();
  r.king_must_land_immediately_behind = false;
  Position p; p.stm = WHITE;
  p.wk = bit(28);
  p.bm = bit(21);
  p.hash = recompute_hash(p);

  // With free landing the king may land on any empty square beyond 21 along NE:
  // 17 (immediate) and 14 and 9 and 5 and 0 are all reachable when empty.
  auto mv = legal_moves(p, r);
  CHECK(mv.size() >= 2);
  bool sawFar = false;
  for (const auto& m : mv) if (m.to != 17 && m.captured == bit(21)) sawFar = true;
  CHECK(sawFar);
}
