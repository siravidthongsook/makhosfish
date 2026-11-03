// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Siravid Thongsook and MakHosFish contributors

#include "doctest/doctest.h"
#include "test_util.hpp"
#include "reference_movegen.hpp"
#include "core/perft.hpp"
#include "protocol/fen.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

using namespace mhf;

#ifndef MAKHOS_SOURCE_DIR
#define MAKHOS_SOURCE_DIR "."
#endif

namespace {

std::string trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  size_t b = s.find_last_not_of(" \t\r\n");
  if (a == std::string::npos) return "";
  return s.substr(a, b - a + 1);
}

RuleSet rules_for(const std::string& name) {
  if (name == "english")   return english_rules();
  if (name == "russian")   return russian_rules();
  if (name == "brazilian") return brazilian_rules();
  return thai_rules();
}

// 12 men on the 3 nearest rows (§11.0); the start position is colour-symmetric,
// so the side to move does not change the counts.
constexpr const char* VARIANT_START_FEN = "W:W21-32:B1-12";

Position variant_start(const std::string& name) {
  Position p;
  parse_fen(VARIANT_START_FEN, p, rules_for(name));
  return p;
}

// Same opt-in gate as the Thai fixtures: default runs stay fast.
int depth_cap() {
  const char* e = std::getenv("MAKHOS_SLOW");
  return (e && e[0] == '1') ? 99 : 6;
}

void agree(const Position& p, const RuleSet& r, int depth) {
  CHECK(testutil::same_moves(legal_moves(p, r), ref_generate_moves(p, r)));
  for (int d = 1; d <= depth; ++d)
    CHECK(perft(p, r, d) == ref_perft(p, r, d));
}

} // namespace

TEST_CASE("variant perft fixtures match stored counts") {
  const int cap = depth_cap();
  std::ifstream in(std::string(MAKHOS_SOURCE_DIR) + "/tests/perft_variants.txt");
  REQUIRE_MESSAGE(in.good(), "cannot open perft_variants.txt");

  std::string line;
  int checks = 0;
  while (std::getline(in, line)) {
    std::string t = trim(line);
    if (t.empty() || t[0] == '#') continue;

    std::stringstream ss(t);
    std::string variant, fen, tok;
    std::getline(ss, variant, ';'); variant = trim(variant);
    std::getline(ss, fen, ';');     fen = trim(fen);

    const RuleSet r = rules_for(variant);
    Position p;
    REQUIRE_MESSAGE(parse_fen(fen, p, r), "bad fixture FEN: ", fen);

    while (std::getline(ss, tok, ';')) {
      tok = trim(tok);
      if (tok.empty()) continue;
      int depth = 0;
      unsigned long long expect = 0;
      if (std::sscanf(tok.c_str(), "D%d %llu", &depth, &expect) != 2) continue;
      if (depth > cap) continue;
      CHECK_MESSAGE(perft(p, r, depth) == expect, variant, " ", fen, " ", tok);
      ++checks;
    }
  }
  CHECK(checks > 0);
}

TEST_CASE("fast == reference: variant start positions") {
  for (const char* name : {"english", "russian", "brazilian"})
    agree(variant_start(name), rules_for(name), 4);
}

// Direct checks of the individual rule flags (the external counts above only pin
// whole rule sets from the start position).

TEST_CASE("men_capture_backward: English men cannot jump backward") {
  Position p;
  REQUIRE(parse_fen("W:W22:B25", p, english_rules()));
  auto eng = legal_moves(p, english_rules());
  CHECK(eng.size() == 2);                       // two quiet steps
  for (const Move& m : eng) CHECK_FALSE(m.is_capture());

  RuleSet b = english_rules();
  b.men_capture_backward = true;                // backward capture switches it on
  auto mv = legal_moves(p, b);
  REQUIRE(mv.size() == 1);
  CHECK(mv[0].from == 21);
  CHECK(mv[0].to == 28);
  CHECK(mv[0].captured == bit(24));
}

TEST_CASE("max_capture_required keeps only the longest capture") {
  RuleSet r = thai_rules();
  r.king_must_land_immediately_behind = false;  // free king landing
  Position p;
  REQUIRE(parse_fen("W:WK19:B15,23,24", p, r));

  auto free = legal_moves(p, r);
  CHECK(free.size() == 8);
  bool sawShort = false;
  for (const Move& m : free) if (m.n_captured == 1) sawShort = true;
  CHECK(sawShort);

  r.max_capture_required = true;
  auto filt = legal_moves(p, r);
  CHECK(filt.size() == 6);
  for (const Move& m : filt) CHECK(m.n_captured == 2);
}

TEST_CASE("immediate_removal=false: captured pieces block until the move ends") {
  RuleSet r = thai_rules();
  Position p;
  REQUIRE(parse_fen("W:WK19:B15,24", p, r));
  bool immLong = false;
  for (const Move& m : legal_moves(p, r)) if (m.n_captured == 2) immLong = true;
  CHECK(immLong);   // the vacated piece opens the diagonal

  r.immediate_removal = false;
  auto mv = legal_moves(p, r);
  CHECK_FALSE(mv.empty());
  for (const Move& m : mv) CHECK(m.n_captured == 1);  // the jumped piece now blocks
}

TEST_CASE("midcapture promotion modes") {
  Position p;
  REQUIRE(parse_fen("W:W11:B6,7", p, english_rules()));
  auto eng = legal_moves(p, english_rules());
  REQUIRE(eng.size() == 1);
  CHECK(eng[0].to == 1);
  CHECK(eng[0].n_captured == 1);
  CHECK(eng[0].promotes);          // Stop: crowned, the move ends

  Position q;
  REQUIRE(parse_fen("W:W11:B6,7", q, russian_rules()));
  auto rus = legal_moves(q, russian_rules());
  CHECK(rus.size() == 2);          // PromoteAndContinue: continues on as a flying king
  for (const Move& m : rus) { CHECK(m.n_captured == 2); CHECK(m.promotes); }

  Position s;
  REQUIRE(parse_fen("W:W11:B6,7", s, brazilian_rules()));
  auto bra = legal_moves(s, brazilian_rules());
  REQUIRE(bra.size() == 1);        // ContinueMan: still a man, so a single landing
  CHECK(bra[0].to == 8);
  CHECK(bra[0].n_captured == 2);
  CHECK_FALSE(bra[0].promotes);    // ends off the crown row, so never crowned
}

// The external counts only pin the start position. Random midgames exercise the
// flags the start position cannot: backward captures, deferred removal, flying
// kings, mid-capture promotion and majority capture.
TEST_CASE("fast == reference: midgame playouts for every variant") {
  struct V { const char* name; uint64_t seed; };
  const V vs[] = {{"thai", 11}, {"english", 22}, {"russian", 33}, {"brazilian", 44}};
  for (const V& v : vs) {
    const RuleSet r = rules_for(v.name);
    const Position start = (std::string(v.name) == "thai") ? start_position(r) : variant_start(v.name);
    std::mt19937_64 rng(v.seed);
    for (int g = 0; g < 50; ++g) {
      Position p = testutil::random_playout(start, r, rng, int(rng() % 40));
      agree(p, r, 3);
    }
  }
}
