// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Siravid Thongsook and MakHosFish contributors

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>
#include "core/movegen.hpp"
#include "core/position.hpp"
#include "core/rules.hpp"
#include "protocol/fen.hpp"
#include "search/search.hpp"

using namespace mhf;

// Self-play data generator for E2 pattern-weight tuning (spec §8 stage E4).
//
// Each game starts from a short random opening record (for diversity), then both
// sides search at a fixed depth with a chosen eval. Every position reached is
// written as `<fen> <result>`, where <result> is the final game result from the
// side-to-move's perspective (1 = win, 0.5 = draw, 0 = loss) -- the Texel-style
// training signal consumed by `tune`.
//
// usage: makhosfish_datagen <games> <depth> <opening> <out.txt>
//                           [seed] [e0|e1] [max_plies]
int main(int argc, char** argv) {
  const int games = (argc > 1) ? std::atoi(argv[1]) : 200;
  const int depth = (argc > 2) ? std::atoi(argv[2]) : 6;
  const int opening = (argc > 3) ? std::atoi(argv[3]) : 6;
  const std::string out = (argc > 4) ? argv[4] : "/tmp/mhf_data.txt";
  const uint64_t seed = (argc > 5) ? std::strtoull(argv[5], nullptr, 10) : 20240607ull;
  const EvalMode mode = (argc > 6 && std::strcmp(argv[6], "e0") == 0) ? EvalMode::E0 : EvalMode::E1;
  const int max_plies = (argc > 7) ? std::atoi(argv[7]) : 400;

  std::ofstream os(out);
  if (!os) { std::fprintf(stderr, "cannot open %s\n", out.c_str()); return 2; }

  RuleSet r = thai_rules();
  std::mt19937_64 rng(seed);
  Searcher s(64);
  s.set_eval_mode(mode);

  long long positions = 0;
  int whiteWins = 0, blackWins = 0, draws = 0;

  for (int g = 0; g < games; ++g) {
    Position p = start_position(r);
    std::vector<uint64_t> hist;
    std::vector<std::string> fens;
    std::vector<uint8_t> stms;
    std::unordered_map<uint64_t, int> seen;
    s.clear();

    double resultWhite = -1.0;  // sentinel: game did not terminate
    for (int ply = 0; ply < max_plies; ++ply) {
      Move buf[256];
      const int n = generate_moves(p, r, buf, 256);
      if (n == 0) { resultWhite = (p.stm == WHITE) ? 0.0 : 1.0; break; }
      if (p.rule50 >= r.draw_no_progress_plies) { resultWhite = 0.5; break; }
      if (++seen[p.hash] >= 3) { resultWhite = 0.5; break; }

      fens.push_back(to_fen(p));
      stms.push_back(p.stm);

      Move m;
      if (ply < opening) {
        std::uniform_int_distribution<int> d(0, n - 1);
        m = buf[d(rng)];
      } else {
        SearchLimits lim;
        lim.depth = depth;
        const SearchInfo info = s.go(p, r, lim, hist);
        m = info.pv.empty() ? buf[0] : info.pv[0];
      }
      hist.push_back(p.hash);
      do_move(p, m, r);
    }
    if (resultWhite < 0.0) resultWhite = 0.5;  // hit ply cap -> draw

    if ((g + 1) % 100 == 0)
      std::fprintf(stderr, "  [%d/%d games, %lld positions]\n", g + 1, games, positions);
    std::fflush(stderr);

    if (resultWhite == 0.5) ++draws;
    else if (resultWhite == 1.0) ++whiteWins;
    else ++blackWins;

    for (size_t i = 0; i < fens.size(); ++i) {
      const double resStm = (stms[i] == WHITE) ? resultWhite : 1.0 - resultWhite;
      os << fens[i] << ' ' << resStm << '\n';
      ++positions;
    }
  }

  std::printf("datagen: %d games depth %d opening %d -> %s\n",
              games, depth, opening, out.c_str());
  std::printf("  positions %lld  W %d B %d D %d\n", positions, whiteWins, blackWins, draws);
  return 0;
}
