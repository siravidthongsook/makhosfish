#include "doctest/doctest.h"
#include <cstdlib>
#include "core/movegen.hpp"
#include "core/position.hpp"
#include "core/zobrist.hpp"
#include "eval/eval.hpp"
#include "protocol/fen.hpp"

using namespace mhf;

TEST_CASE("eval is near zero at start and mirrors with side to move") {
  RuleSet r = thai_rules();
  Position p = start_position(r);
  const int e = evaluate(p, r);
  CHECK(std::abs(e) <= TEMPO + 1);

  Position q = p;
  q.stm = (p.stm == WHITE) ? BLACK : WHITE;
  q.hash = recompute_hash(q);
  const int e2 = evaluate(q, r);
  // Side-to-move perspective plus a mover tempo: the two values sum to 2*TEMPO.
  CHECK(e + e2 == 2 * TEMPO);
}

TEST_CASE("material dominates the sign of the evaluation") {
  RuleSet r = thai_rules();
  Position p;
  REQUIRE(parse_fen("W:WK28,21,K27:B9,K22", p, r));  // White has an extra king
  CHECK(evaluate(p, r) > 0);

  Position q = p;
  q.stm = BLACK;
  q.hash = recompute_hash(q);
  CHECK(evaluate(q, r) < 0);
}

TEST_CASE("a lone extra man flips the sign") {
  RuleSet r = thai_rules();
  Position p;
  REQUIRE(parse_fen("W:W25,27:B22", p, r));  // White up a man
  CHECK(evaluate(p, r) > 0);
}
