#include "doctest/doctest.h"
#include "test_util.hpp"
#include "reference_movegen.hpp"
#include "core/perft.hpp"
#include "protocol/fen.hpp"

#include <random>
#include <string>

using namespace mhf;

static void agree_fen(const std::string& fen, const RuleSet& r, int depth) {
  Position p;
  REQUIRE_MESSAGE(parse_fen(fen, p, r), "bad fixture FEN: ", fen);
  CHECK(testutil::same_moves(legal_moves(p, r), ref_generate_moves(p, r)));
  for (int d = 1; d <= depth; ++d)
    CHECK(perft(p, r, d) == ref_perft(p, r, d));
}

static void agree(const Position& p, const RuleSet& r, int depth) {
  CHECK(testutil::same_moves(legal_moves(p, r), ref_generate_moves(p, r)));
  for (int d = 1; d <= depth; ++d)
    CHECK(perft(p, r, d) == ref_perft(p, r, d));
}

TEST_CASE("start position: fast == reference (moves + perft up to 5)") {
  RuleSet r = thai_rules();
  agree(start_position(r), r, 5);
}

TEST_CASE("fixture positions: fast == reference") {
  RuleSet r = thai_rules();
  agree_fen("W:WK28,21:B9,K22", r, 4);
  agree_fen("W:WK29,26:B6,K22", r, 4);
  agree_fen("W:W25,27:B23", r, 2);
  agree_fen("W:WK29:B22", r, 2);
  agree_fen("B:W27:B23", r, 2);
}

TEST_CASE("random playouts: fast == reference") {
  RuleSet r = thai_rules();
  std::mt19937_64 rng(20240);
  for (int g = 0; g < 120; ++g) {
    Position p = start_position(r);
    int plies = int(rng() % 48);
    p = testutil::random_playout(p, r, rng, plies);
    agree(p, r, 3);
  }
}
