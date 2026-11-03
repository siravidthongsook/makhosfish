// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Siravid Thongsook and MakHosFish contributors

#include "doctest/doctest.h"
#include <sstream>
#include <string>
#include "core/position.hpp"
#include "protocol/fen.hpp"
#include "protocol/tci.hpp"

using namespace mhf;

namespace {
std::string run_tci(const std::string& script) {
  std::istringstream in(script);
  std::ostringstream out;
  TciSession s;
  s.run(in, out);
  return out.str();
}

// First output line beginning with `prefix` (up to the next newline).
std::string line_with(const std::string& output, const std::string& prefix) {
  size_t p = 0;
  while (p < output.size()) {
    size_t e = output.find('\n', p);
    if (e == std::string::npos) e = output.size();
    if (output.compare(p, prefix.size(), prefix) == 0) return output.substr(p, e - p);
    p = e + 1;
  }
  return "";
}

std::string bestmove_of(const std::string& output) {
  const std::string key = "bestmove ";
  const size_t p = output.rfind(key);
  if (p == std::string::npos) return "";
  size_t a = p + key.size();
  size_t b = a;
  while (b < output.size() && !std::isspace((unsigned char)output[b])) ++b;
  return output.substr(a, b - a);
}
} // namespace

TEST_CASE("tci handshake and isready") {
  const std::string o = run_tci("tci\nisready\nquit\n");
  CHECK(o.find("id name MakHosFish") != std::string::npos);
  CHECK(o.find("option name Hash") != std::string::npos);
  CHECK(o.find("tciok") != std::string::npos);
  CHECK(o.find("readyok") != std::string::npos);
}

TEST_CASE("go yields a legal bestmove") {
  // NB: a piped `quit` stops the search at once, so bestmove is what we assert
  // here; per-iteration `info` streaming is covered by test_search.
  const std::string o = run_tci("tci\nposition startpos\ngo depth 4\nquit\n");

  const std::string bm = bestmove_of(o);
  REQUIRE(!bm.empty());
  CHECK(bm != "(none)");

  RuleSet r = thai_rules();
  Position p = start_position(r);
  Move m;
  CHECK(parse_move(p, bm, r, m));  // the announced move is legal
}

TEST_CASE("position fen + moves drives a search") {
  RuleSet r = thai_rules();
  Position p;
  REQUIRE(parse_fen("W:W25:B15,22", p, r));  // forced double jump
  const std::string script =
      "tci\nposition fen W:W25:B15,22\ngo depth 3\nquit\n";
  const std::string o = run_tci(script);
  const std::string bm = bestmove_of(o);
  CHECK(bm == "25x18x11");  // only legal move, full chain reported
  Move m;
  CHECK(parse_move(p, bm, r, m));
}

TEST_CASE("setoption is accepted and search still works") {
  const std::string o = run_tci(
      "tci\nsetoption name Hash value 1\nsetoption name Eval value e0\n"
      "position startpos\ngo nodes 2000\nquit\n");
  const std::string bm = bestmove_of(o);
  REQUIRE(!bm.empty());
  CHECK(bm != "(none)");
}

TEST_CASE("newgame resets to startpos") {
  const std::string o = run_tci("tci\nposition startpos moves 25-22\ntcinewgame\nperft 1\nquit\n");
  // startpos has 7 legal first moves; the after-25-22 position differs.
  CHECK(o.find("perft 1 7") != std::string::npos);
}

TEST_CASE("state returns fen, legal moves and side to move") {
  const std::string o = run_tci("tci\nposition startpos\nstate\nquit\n");
  CHECK(line_with(o, "fen ") == "fen W:W25-32:B1-8");
  CHECK(line_with(o, "stm ") == "stm W");

  // Seven legal first moves, in full notation, space-separated after `moves`.
  const std::string mv = line_with(o, "moves ");
  REQUIRE(mv.rfind("moves ", 0) == 0);
  size_t count = 0;
  for (size_t i = 0; i < mv.size();) {
    size_t e = mv.find(' ', i);
    if (e == std::string::npos) break;
    ++count;
    i = e + 1;
  }
  CHECK(count == 7);

  // A forced double jump is reported as a single full-chain move.
  const std::string o2 = run_tci("tci\nposition fen W:W25:B15,22\nstate\nquit\n");
  CHECK(line_with(o2, "moves ") == "moves 25x18x11");
}

TEST_CASE("state reports an empty move list when the side to move loses") {
  const std::string o = run_tci("tci\nposition fen W:W1:B\nstate\nquit\n");
  CHECK(line_with(o, "fen ") == "fen W:W1:B");
  CHECK(line_with(o, "moves") == "moves");  // no legal move -> terminal loss
  CHECK(line_with(o, "stm ") == "stm W");
}
