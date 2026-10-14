#include "doctest/doctest.h"
#include <random>
#include <string>
#include "core/movegen.hpp"
#include "core/position.hpp"
#include "protocol/fen.hpp"
#include "protocol/pdn.hpp"

using namespace mhf;

TEST_CASE("PDN parser reads tags and moves, skips noise") {
  const std::string text =
      "[Event \"t\"]\n[Site \"local\"]\n[Result \"1-0\"]\n"
      "[FEN \"W:W25-32:B1-8\"]\n\n"
      "1. 25-22 8-11\n"
      "2. 27-23 3-8 {developing} (2... 11-15 {alt})\n"
      "3. 22-18 11-15\n"
      "4. 18x11 ; line comment\n"
      "1-0\n";
  const PdnGame g = parse_pdn(text);
  CHECK(g.tag("Event") == "t");
  CHECK(g.tag("FEN") == "W:W25-32:B1-8");
  CHECK(g.result == "1-0");
  REQUIRE(g.moves.size() == 7);
  CHECK(g.moves[0] == "25-22");
  CHECK(g.moves[6] == "18x11");

  const PdnReplay rep = replay_pdn(g);
  CHECK(rep.ok);
  CHECK(rep.positions.size() == 8);  // start + 7 moves
}

TEST_CASE("random game survives format -> parse -> replay") {
  RuleSet r = thai_rules();
  std::mt19937_64 rng(0x51EEDull);
  for (int g = 0; g < 12; ++g) {
    Position p = start_position(r);
    PdnGame game;
    game.tags.push_back({"FEN", to_fen(p)});
    for (int ply = 0; ply < 40; ++ply) {
      const std::vector<Move> ms = legal_moves(p, r);
      if (ms.empty()) break;
      std::uniform_int_distribution<int> d(0, (int)ms.size() - 1);
      const Move m = ms[d(rng)];
      game.moves.push_back(move_to_notation(p, r, m));
      do_move(p, m, r);
    }
    game.result = "*";

    const std::string text = format_pdn(game);
    const PdnGame back = parse_pdn(text);
    CHECK(back.moves == game.moves);

    const PdnReplay rep = replay_pdn(back, r);
    REQUIRE(rep.ok);
    CHECK(rep.final.hash == p.hash);
  }
}

TEST_CASE("illegal move fails replay") {
  PdnGame g;
  g.tags.push_back({"FEN", "W:W25-32:B1-8"});
  g.moves = {"24-20"};  // 24 is empty at the start
  const PdnReplay rep = replay_pdn(g);
  CHECK_FALSE(rep.ok);
  CHECK(rep.error.find("illegal move") != std::string::npos);
}

TEST_CASE("missing FEN tag defaults to startpos") {
  PdnGame g;
  g.moves = {"25-22", "8-11"};
  const PdnReplay rep = replay_pdn(g);
  REQUIRE(rep.ok);
  CHECK(rep.positions.front().hash == start_position(thai_rules()).hash);
}
