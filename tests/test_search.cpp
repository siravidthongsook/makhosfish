// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Siravid Thongsook and MakHosFish contributors

#include "doctest/doctest.h"
#include <algorithm>
#include <chrono>
#include <random>
#include <thread>
#include <vector>
#include "core/movegen.hpp"
#include "core/position.hpp"
#include "core/types.hpp"
#include "core/zobrist.hpp"
#include "protocol/fen.hpp"
#include "search/search.hpp"
#include "search/tt.hpp"

using namespace mhf;

namespace {
bool is_legal(const std::vector<Move>& legal, const Move& m) {
  for (const Move& x : legal) if (x == m) return true;
  return false;
}
} // namespace

TEST_CASE("info callback streams one event per iterative-deepening iteration") {
  RuleSet r = thai_rules();
  Searcher s(16);
  int iterations = 0;
  int max_depth = 0;
  s.set_info_callback([&](const SearchInfo& info) {
    ++iterations;
    max_depth = std::max(max_depth, info.depth);
  });
  Position p = start_position(r);
  SearchLimits lim;
  lim.depth = 6;
  const SearchInfo info = s.go(p, r, lim);
  CHECK(info.depth == 6);
  CHECK(iterations == 6);
  CHECK(max_depth == 6);
}

TEST_CASE("request_stop from another thread ends the search") {
  RuleSet r = thai_rules();
  Searcher s(16);
  Position p = start_position(r);
  SearchLimits lim;
  lim.depth = 40;
  std::thread stopper([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    s.request_stop();
  });
  const SearchInfo info = s.go(p, r, lim);
  stopper.join();
  CHECK(info.depth < 40);  // stopped well before the depth cap
}

TEST_CASE("best move is legal across several FENs") {
  RuleSet r = thai_rules();
  const std::vector<std::string> fens = {
      "W:W25-32:B1-8",
      "W:WK28,21:B9,K22",
      "W:WK29,26:B6,K22",
      "B:W27:B23",
  };
  for (const std::string& f : fens) {
    Position p;
    REQUIRE(parse_fen(f, p, r));
    auto legal = legal_moves(p, r);
    REQUIRE_FALSE(legal.empty());

    Searcher s(8);
    SearchLimits lim;
    lim.depth = 4;
    SearchInfo info = s.go(p, r, lim);
    REQUIRE_FALSE(info.pv.empty());
    CHECK_MESSAGE(is_legal(legal, info.pv[0]), f);
  }
}

TEST_CASE("mate in 1 forced win is found") {
  RuleSet r = thai_rules();
  Position p;
  REQUIRE(parse_fen("W:W27:B23", p, r));  // White captures the lone black man
  Searcher s(8);
  SearchLimits lim;
  lim.depth = 3;
  SearchInfo info = s.go(p, r, lim);
  REQUIRE_FALSE(info.pv.empty());
  CHECK(info.pv[0].is_capture());
  CHECK(info.score_cp >= MATE_SCORE - 6);
}

TEST_CASE("iterative deepening completes and yields a legal pv") {
  RuleSet r = thai_rules();
  Position p;
  REQUIRE(parse_fen("W:WK28,21:B9,K22", p, r));
  auto legal = legal_moves(p, r);
  for (int d = 1; d <= 5; ++d) {
    Searcher s(8);
    SearchLimits lim;
    lim.depth = d;
    SearchInfo info = s.go(p, r, lim);
    CHECK(info.depth == d);
    REQUIRE_FALSE(info.pv.empty());
    CHECK(is_legal(legal, info.pv[0]));
  }
}

TEST_CASE("tt store/probe round trip, depth gate and clear") {
  TTable tt;
  tt.resize_mb(1);
  tt.clear();
  tt.set_ply(0);

  Move m;
  m.from = 10; m.to = 19; m.n_captured = 1; m.captured = bit(14);
  const uint64_t key = 0x1234567890ABCDEFull;
  tt.store(key, 5, 42, TT_EXACT, m);

  Move got; int sc = 0;
  CHECK(tt.probe(key, 5, -100, 100, sc, got));
  CHECK(sc == 42);
  CHECK(got == m);

  Move got2; int sc2 = 0;
  CHECK_FALSE(tt.probe(key, 6, -100, 100, sc2, got2));  // shallower than stored
  CHECK(got2 == m);                                     // move still served for ordering

  tt.clear();
  Move got3; int sc3 = 0;
  CHECK_FALSE(tt.probe(key, 5, -100, 100, sc3, got3));
}

TEST_CASE("tt mate scores are ply-relative") {
  TTable tt;
  tt.resize_mb(1);
  tt.clear();

  Move m; m.from = 3; m.to = 12;
  const uint64_t key = 0xA5A5A5A5DEADBEEFull;
  const int mateIn10 = MATE_SCORE - 10;

  tt.set_ply(2);
  tt.store(key, 4, mateIn10, TT_EXACT, m);
  tt.set_ply(2);
  Move got; int sc = 0;
  CHECK(tt.probe(key, 4, -INF_SCORE, INF_SCORE, sc, got));
  CHECK(sc == mateIn10);

  // Reading the same entry 3 plies deeper shortens the stored distance.
  tt.set_ply(5);
  Move got2; int sc2 = 0;
  CHECK(tt.probe(key, 4, -INF_SCORE, INF_SCORE, sc2, got2));
  CHECK(sc2 == mateIn10 - 3);
}

TEST_CASE("tt collisions never return another key's score") {
  TTable tt;
  tt.resize_mb(1);
  tt.clear();
  tt.set_ply(0);

  // All keys share the low 48 bits -> same bucket; key16 values are unique.
  for (int i = 0; i < 64; ++i) {
    Move m; m.from = uint8_t(i % 32); m.to = uint8_t((i + 1) % 32);
    tt.store(uint64_t(i + 1) << 48, 3, i, TT_EXACT, m);
  }
  int hits = 0;
  for (int i = 0; i < 64; ++i) {
    Move m; int sc = 0;
    if (tt.probe(uint64_t(i + 1) << 48, 3, -INF_SCORE, INF_SCORE, sc, m)) {
      CHECK(sc == i);
      ++hits;
    }
  }
  CHECK(hits > 0);
  CHECK(hits <= TTable::CLUSTER);
}

TEST_CASE("node limit is respected") {
  RuleSet r = thai_rules();
  Position p;
  REQUIRE(parse_fen("W:W25-32:B1-8", p, r));
  Searcher s(8);
  SearchLimits lim;
  lim.depth = 64;
  lim.nodes_limit = 5000;
  SearchInfo info = s.go(p, r, lim);
  CHECK(info.nodes >= 1);
  CHECK(info.nodes <= 5000 + 4096);
}

TEST_CASE("search is deterministic") {
  RuleSet r = thai_rules();
  Position p;
  REQUIRE(parse_fen("W:WK28,21:B9,K22", p, r));
  Searcher s1(16), s2(16);
  SearchLimits lim;
  lim.depth = 5;
  SearchInfo i1 = s1.go(p, r, lim);
  SearchInfo i2 = s2.go(p, r, lim);
  REQUIRE_FALSE(i1.pv.empty());
  REQUIRE_FALSE(i2.pv.empty());
  CHECK(i1.pv[0] == i2.pv[0]);
  CHECK(i1.score_cp == i2.score_cp);
}

TEST_CASE("strength gate: engine vs random mover, depth 4") {
  RuleSet r = thai_rules();
  std::mt19937_64 rng(999);
  const int games = 20;
  const int depth = 4;

  auto t0 = std::chrono::steady_clock::now();
  int wins = 0;
  for (int g = 0; g < games; ++g) {
    Position p = start_position(r);
    const Color engine = (g % 2 == 0) ? WHITE : BLACK;
    std::vector<uint64_t> hist;
    Searcher s(8);
    int result = 0;
    for (int ply = 0; ply < 300; ++ply) {
      Move buf[256];
      const int n = generate_moves(p, r, buf, 256);
      if (n == 0) { result = (p.stm == engine) ? -1 : 1; break; }
      if (r.draw_no_progress_plies && p.rule50 >= r.draw_no_progress_plies) { result = 0; break; }

      Move m;
      if (p.stm == engine) {
        SearchLimits lim;
        lim.depth = depth;
        SearchInfo info = s.go(p, r, lim, hist);
        m = info.pv.empty() ? buf[0] : info.pv[0];
      } else {
        std::uniform_int_distribution<int> d(0, n - 1);
        m = buf[d(rng)];
      }
      hist.push_back(p.hash);
      do_move(p, m, r);
    }
    if (result > 0) ++wins;
  }
  auto t1 = std::chrono::steady_clock::now();
  const double sec = std::chrono::duration<double>(t1 - t0).count();

  CHECK_MESSAGE(wins >= 18, "wins=", wins, " of ", games);
  CHECK(sec < 5.0);
}
