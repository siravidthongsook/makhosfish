#include "doctest/doctest.h"
#include "core/position.hpp"
#include "protocol/fen.hpp"

using namespace mhf;

TEST_CASE("start FEN round-trips") {
  RuleSet r = thai_rules();
  Position p = start_position(r);
  CHECK(to_fen(p) == "W:W25-32:B1-8");

  Position q;
  REQUIRE(parse_fen(to_fen(p), q, r));
  CHECK(q.wm == p.wm);
  CHECK(q.bm == p.bm);
  CHECK(q.stm == p.stm);
  CHECK(q.hash == recompute_hash(q));
}

TEST_CASE("kings, ranges and side to move parse") {
  RuleSet r = thai_rules();
  Position p;
  REQUIRE(parse_fen("B:WK9,3-7:B24-30,K31", p, r));
  CHECK((p.wk & bit(8)));    // K9  -> internal 8
  CHECK((p.wm & bit(2)));    // 3   -> internal 2
  CHECK((p.wm & bit(6)));    // 7   -> internal 6
  CHECK((p.bm & bit(23)));   // 24  -> internal 23
  CHECK((p.bk & bit(30)));   // K31 -> internal 30
  CHECK(p.stm == BLACK);
}

TEST_CASE("rule50 field round-trips") {
  RuleSet r = thai_rules();
  Position p;
  REQUIRE(parse_fen("W:W25-32:B1-8:12", p, r));
  CHECK(p.rule50 == 12);
  CHECK(to_fen(p) == "W:W25-32:B1-8:12");
}

TEST_CASE("bad FEN rejected") {
  RuleSet r = thai_rules();
  Position p;
  CHECK_FALSE(parse_fen("X:W25-32:B1-8", p, r));        // bad side to move
  CHECK_FALSE(parse_fen("W:W25-32", p, r));             // missing black list
  CHECK_FALSE(parse_fen("W:WK28,B21:B9,K22", p, r));    // stray color letter mid-list
  CHECK_FALSE(parse_fen("W:W25-32:B1-40", p, r));       // square out of range
  CHECK_FALSE(parse_fen("W:W25-32:B1,,8", p, r));       // empty item
}
