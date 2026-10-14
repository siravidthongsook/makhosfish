#include "doctest/doctest.h"
#include "core/perft.hpp"
#include "protocol/fen.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
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

// Deep fixtures (perft(9) is ~114M nodes) are opt-in: set MAKHOS_SLOW=1.
// The default unit run stays fast; use tools/makhosfish_perft for deep benches.
int depth_cap() {
  const char* e = std::getenv("MAKHOS_SLOW");
  return (e && e[0] == '1') ? 99 : 6;
}
} // namespace

TEST_CASE("perft fixtures match stored counts") {
  RuleSet r = thai_rules();
  const int cap = depth_cap();
  std::ifstream in(std::string(MAKHOS_SOURCE_DIR) + "/tests/perft_fixtures.txt");
  REQUIRE_MESSAGE(in.good(), "cannot open perft_fixtures.txt");

  std::string line;
  int checks = 0;
  while (std::getline(in, line)) {
    std::string t = trim(line);
    if (t.empty() || t[0] == '#') continue;

    std::stringstream ss(t);
    std::string fen, tok;
    std::getline(ss, fen, ';');
    fen = trim(fen);

    Position p;
    REQUIRE_MESSAGE(parse_fen(fen, p, r), "bad fixture FEN: ", fen);

    while (std::getline(ss, tok, ';')) {
      tok = trim(tok);
      if (tok.empty()) continue;
      int depth = 0;
      unsigned long long expect = 0;
      if (std::sscanf(tok.c_str(), "D%d %llu", &depth, &expect) != 2) continue;
      if (depth > cap) continue;
      CHECK_MESSAGE(perft(p, r, depth) == expect, fen, "  ", tok);
      ++checks;
    }
  }
  CHECK(checks > 0);
}
