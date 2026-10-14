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

namespace {
uint32_t rot180(uint32_t x) {
  uint32_t out = 0;
  while (x) { const int s = lsb(x); x &= x - 1; out |= bit(31 - s); }
  return out;
}
// 180-degree rotation with the colors swapped: White<->Black geometry mirrors.
Position mirror(const Position& p) {
  Position q;
  q.wm = rot180(p.bm); q.wk = rot180(p.bk);
  q.bm = rot180(p.wm); q.bk = rot180(p.wk);
  q.stm = (p.stm == WHITE) ? BLACK : WHITE;
  q.hash = recompute_hash(q);
  return q;
}
} // namespace

TEST_CASE("E1 features are empty and symmetric at the start position") {
  RuleSet r = thai_rules();
  Position p = start_position(r);
  EvalDetail d = eval_detail(p, r, EvalMode::E1);
  CHECK(d.mobility == 0);
  CHECK(d.captures == 0);
  CHECK(d.structure == 0);
  CHECK(d.first_king == 0);
  CHECK(d.precrown == 0);
  CHECK(d.white_minus_black == 0);
  CHECK(evaluate(p, r, EvalMode::E1) == TEMPO);
}

TEST_CASE("E1 is antisymmetric under color-mirroring") {
  RuleSet r = thai_rules();
  const char* fens[] = {
      "W:W25,27,30:B22,19",
      "B:WK28,21:B9,K22",
      "W:WK5,13,18:B24,26,30,K31",
  };
  for (const char* f : fens) {
    Position p;
    REQUIRE(parse_fen(f, p, r));
    const int a = eval_detail(p, r, EvalMode::E1).white_minus_black;
    const int b = eval_detail(mirror(p), r, EvalMode::E1).white_minus_black;
    CHECK(a == -b);
  }
}

TEST_CASE("E0 mode carries no hand features, E1 shares its material+PST") {
  RuleSet r = thai_rules();
  Position p;
  REQUIRE(parse_fen("W:WK5,13,18:B24,26,30,K31", p, r));
  const EvalDetail e0 = eval_detail(p, r, EvalMode::E0);
  const EvalDetail e1 = eval_detail(p, r, EvalMode::E1);
  CHECK(e0.mobility == 0);
  CHECK(e0.captures == 0);
  CHECK(e0.structure == 0);
  CHECK(e0.first_king == 0);
  CHECK(e0.precrown == 0);
  CHECK(e0.material == e1.material);
  CHECK(e0.pst == e1.pst);
  CHECK(e0.white_minus_black == e0.material + e0.pst);
}

TEST_CASE("E1 first-king bonus favours the side that owns a king") {
  RuleSet r = thai_rules();
  Position p;
  REQUIRE(parse_fen("W:WK18:B21", p, r));  // White king vs lone black man
  CHECK(eval_detail(p, r, EvalMode::E1).first_king == FIRST_KING);

  Position q;
  REQUIRE(parse_fen("W:W18:BK21", q, r));  // Black king vs lone white man
  CHECK(eval_detail(q, r, EvalMode::E1).first_king == -FIRST_KING);

  Position s;
  REQUIRE(parse_fen("W:WK18:BK21", s, r));  // both crowned: no edge
  CHECK(eval_detail(s, r, EvalMode::E1).first_king == 0);
}

TEST_CASE("E1 mobility rewards king centralisation over a corner") {
  RuleSet r = thai_rules();
  Position central;
  REQUIRE(parse_fen("W:WK19:BK1", central, r));  // White king mid-board, black in a corner
  CHECK(eval_detail(central, r, EvalMode::E1).mobility > 0);

  Position corner;
  REQUIRE(parse_fen("W:WK1:BK19", corner, r));  // colors of squares swapped
  CHECK(eval_detail(corner, r, EvalMode::E1).mobility < 0);
}

TEST_CASE("E1 structure credits a man supported from behind") {
  RuleSet r = thai_rules();
  Position p;
  REQUIRE(parse_fen("W:W21,25:B3", p, r));  // 25 supports 21 on the SE diagonal
  CHECK(eval_detail(p, r, EvalMode::E1).structure >= DEFENDED);
}

TEST_CASE("E1 precrown credits a man one step from the crown row") {
  RuleSet r = thai_rules();
  Position p;
  REQUIRE(parse_fen("W:W5:B3", p, r));  // White man on row 1 (one from promotion)
  CHECK(eval_detail(p, r, EvalMode::E1).precrown == PRECROWN);
}

TEST_CASE("E1 breakdown sums to white-minus-black; evaluate adds tempo") {
  RuleSet r = thai_rules();
  Position p;
  REQUIRE(parse_fen("W:WK5,13,18:B24,26,30,K31", p, r));
  const EvalDetail d = eval_detail(p, r, EvalMode::E1);
  const int sum = d.material + d.pst + d.mobility + d.captures + d.structure +
                  d.first_king + d.precrown;
  CHECK(d.white_minus_black == sum);
  const int expected = (p.stm == WHITE ? d.white_minus_black : -d.white_minus_black) + TEMPO;
  CHECK(evaluate(p, r, EvalMode::E1) == expected);
}
