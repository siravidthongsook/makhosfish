#include "doctest/doctest.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include "core/position.hpp"
#include "core/zobrist.hpp"
#include "eval/eval.hpp"
#include "eval/pattern.hpp"
#include "protocol/fen.hpp"

using namespace mhf;

namespace {
uint32_t rot180(uint32_t x) {
  uint32_t out = 0;
  while (x) { const int s = lsb(x); x &= x - 1; out |= bit(31 - s); }
  return out;
}
Position mirror(const Position& p) {
  Position q;
  q.wm = rot180(p.bm); q.wk = rot180(p.bk);
  q.bm = rot180(p.wm); q.bk = rot180(p.wk);
  q.stm = (p.stm == WHITE) ? BLACK : WHITE;
  q.hash = recompute_hash(q);
  return q;
}
} // namespace

TEST_CASE("pattern windows form rot180 mirror pairs") {
  const int pairs[3][2] = {{0, 5}, {1, 4}, {2, 3}};
  for (const auto& pr : pairs)
    for (int i = 0; i < PatternEval::CELLS; ++i)
      CHECK(PatternEval::WINDOWS[pr[1]][i] == 31 - PatternEval::WINDOWS[pr[0]][i]);
}

TEST_CASE("pattern codes are computed from the scoring side's perspective") {
  RuleSet r = thai_rules();
  Position p = start_position(r);
  PatternEval pe;
  int white[PatternEval::NWINDOWS], black[PatternEval::NWINDOWS];
  pe.codes(p, pe.phase_of(p), white, black);
  // Window 0 cells {0,1,4,5,8,9,12,13}: squares 0,1,4,5 hold black men on the
  // start position; the rest are empty. White sees them as opp men (3), black
  // as own men (1): code = 3*(1+5+25+125) and 1*(1+5+25+125).
  CHECK(white[0] == 468);
  CHECK(black[0] == 156);
}

TEST_CASE("phase splits at eight pieces") {
  RuleSet r = thai_rules();
  PatternEval pe;
  Position start = start_position(r);
  CHECK(pe.phase_of(start) == 0);  // 16 pieces -> midgame
  Position endg;
  REQUIRE(parse_fen("W:WK1:BK32", endg, r));
  CHECK(pe.phase_of(endg) == 1);  // 2 pieces -> endgame
}

TEST_CASE("zero weights make E2 identical to E1") {
  RuleSet r = thai_rules();
  Position p;
  REQUIRE(parse_fen("W:WK5,13,18:B24,26,30,K31", p, r));
  const EvalDetail e1 = eval_detail(p, r, EvalMode::E1);
  const EvalDetail e2 = eval_detail(p, r, EvalMode::E2);
  CHECK(e2.pattern == 0);
  CHECK(e2.white_minus_black == e1.white_minus_black);
  CHECK(evaluate(p, r, EvalMode::E2) == evaluate(p, r, EvalMode::E1));
}

TEST_CASE("pattern score is antisymmetric under the colour mirror") {
  RuleSet r = thai_rules();
  const char* fens[] = {
      "W:W25,27,30:B22,19",
      "B:WK28,21:B9,K22",
      "W:WK5,13,18:B24,26,30,K31",
      "W:W21,25:B3",
  };
  uint32_t seed = 0x12345678u;
  auto next = [&]() { seed = seed * 1664525u + 1013904223u; return int((seed >> 9) % 999) + 1; };
  for (const char* f : fens) {
    Position p;
    REQUIRE(parse_fen(f, p, r));
    PatternEval pe;  // fresh table per position
    const int ph = pe.phase_of(p);
    int white[PatternEval::NWINDOWS], black[PatternEval::NWINDOWS];
    pe.codes(p, ph, white, black);
    // Stamp the codes this position actually uses so the reads are non-zero;
    // antisymmetry must hold for these arbitrary weights (see pattern.hpp).
    for (int w = 0; w < PatternEval::NWINDOWS; ++w) {
      pe.set(w, ph, white[w], int16_t(next()));
      pe.set(PatternEval::PAIR[w], ph, black[w], int16_t(next()));
    }
    CHECK(pe.score(p) != 0);
    CHECK(pe.score(mirror(p)) == -pe.score(p));
  }
}

TEST_CASE("score matches the documented two-sided formula") {
  RuleSet r = thai_rules();
  const char* fens[] = {
      "W:W25,27,30:B22,19",
      "W:WK5,13,18:B24,26,30,K31",
      "W:W21,25:B3",
  };
  uint32_t seed = 0xABCDEF01u;
  auto next = [&]() { seed = seed * 1664525u + 1013904223u; return int((seed >> 9) % 199) - 99; };
  for (const char* f : fens) {
    Position p;
    REQUIRE(parse_fen(f, p, r));
    PatternEval pe;
    const int ph = pe.phase_of(p);
    int white[PatternEval::NWINDOWS], black[PatternEval::NWINDOWS];
    pe.codes(p, ph, white, black);
    for (int w = 0; w < PatternEval::NWINDOWS; ++w) {
      pe.set(w, ph, white[w], int16_t(next()));
      pe.set(PatternEval::PAIR[w], ph, black[w], int16_t(next()));
    }
    int expected = 0;
    for (int w = 0; w < PatternEval::NWINDOWS; ++w)
      expected += pe.get(w, ph, white[w]) - pe.get(PatternEval::PAIR[w], ph, black[w]);
    CHECK(pe.score(p) == expected);
  }
}

#ifdef MAKHOS_SOURCE_DIR
TEST_CASE("shipped E2 weights load and stay antisymmetric") {
  const std::string path = std::string(MAKHOS_SOURCE_DIR) + "/data/patterns/thai_e2.mhpw";
  std::ifstream f(path, std::ios::binary);
  if (!f) return;  // weights are optional; skip when absent
  PatternEval pe;
  REQUIRE(pe.load(path));
  RuleSet r = thai_rules();
  Position p;
  REQUIRE(parse_fen("W:WK5,13,18:B24,26,30,K31", p, r));
  CHECK(pe.score(p) != 0);
  CHECK(pe.score(mirror(p)) == -pe.score(p));
}
#endif

TEST_CASE("pattern weights round-trip through .mhpw") {
  PatternEval a;
  a.set(0, 0, 0, 123);
  a.set(2, 1, PatternEval::TABLE - 1, -456);
  a.set(5, 0, 789, 7);
  const std::string path = "/tmp/mhf_pattern_test.mhpw";
  REQUIRE(a.save(path));

  PatternEval b;
  REQUIRE(b.load(path));
  CHECK(b.get(0, 0, 0) == 123);
  CHECK(b.get(2, 1, PatternEval::TABLE - 1) == -456);
  CHECK(b.get(5, 0, 789) == 7);
  std::remove(path.c_str());
}
