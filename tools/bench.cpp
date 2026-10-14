#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include "core/position.hpp"
#include "protocol/fen.hpp"
#include "search/search.hpp"

using namespace mhf;

// Fixed legal midgame FEN (16 pieces, kings + men) for reproducible NPS.
static const char* BENCH_FEN = "W:W21-24,26,28,30,K32:B1-6,8,10,K14";

int main(int argc, char** argv) {
  const int depth = (argc >= 2) ? std::atoi(argv[1]) : 10;

  RuleSet r = thai_rules();
  Position p;
  if (!parse_fen(BENCH_FEN, p, r)) { std::fprintf(stderr, "bad bench fen\n"); return 2; }

  Searcher s(32);
  SearchLimits lim;
  lim.depth = depth;

  auto t0 = std::chrono::steady_clock::now();
  SearchInfo info = s.go(p, r, lim);
  auto t1 = std::chrono::steady_clock::now();
  double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

  double nps = ms > 0 ? double(info.nodes) / (ms / 1000.0) : 0.0;

  std::printf("fen %s\n", to_fen(p).c_str());
  std::printf("depth %d nodes %llu nps %.0f bestmove %s pv",
              info.depth, (unsigned long long)info.nodes, nps,
              info.pv.empty() ? "(none)" : move_to_string(info.pv[0]).c_str());
  for (const Move& m : info.pv) std::printf(" %s", move_to_string(m).c_str());
  std::printf("\n");
  return 0;
}
