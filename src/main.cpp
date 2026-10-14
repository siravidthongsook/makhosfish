#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>
#include "core/movegen.hpp"
#include "core/perft.hpp"
#include "core/position.hpp"
#include "protocol/fen.hpp"
#include "search/search.hpp"

using namespace mhf;

namespace {

bool load_position(const std::string& tok, const RuleSet& r, Position& p) {
  if (tok == "startpos") { p = start_position(r); return true; }
  return parse_fen(tok, p, r);
}

int cmd_perft(int argc, char** argv, int base) {
  RuleSet r = thai_rules();
  Position p;
  const std::string pos = (argc > base) ? argv[base] : "startpos";
  if (!load_position(pos, r, p)) { std::fprintf(stderr, "bad position\n"); return 2; }
  int depth = (argc > base + 1) ? std::atoi(argv[base + 1]) : 1;
  if (depth < 0) depth = 0;

  std::printf("MakHosFish perft\n");
  std::printf("fen   %s\n", to_fen(p).c_str());
  for (int d = 1; d <= depth; ++d)
    std::printf("perft(%d) = %llu\n", d, (unsigned long long)perft(p, r, d));
  return 0;
}

int cmd_go(int argc, char** argv) {
  RuleSet r = thai_rules();
  Position p;
  const std::string pos = (argc > 2) ? argv[2] : "startpos";
  if (!load_position(pos, r, p)) { std::fprintf(stderr, "bad position\n"); return 2; }

  SearchLimits lim;
  if (argc > 3) {
    std::string t = argv[3];
    if (t.rfind("movetime:", 0) == 0) lim.movetime_ms = std::atoll(t.c_str() + 9);
    else lim.depth = std::atoi(t.c_str());
  } else {
    lim.depth = 8;
  }

  Searcher s(64);
  SearchInfo info = s.go(p, r, lim);

  std::printf("info depth %d seldepth %d score cp %d nodes %llu time %lld pv",
              info.depth, info.seldepth, info.score_cp,
              (unsigned long long)info.nodes, (long long)info.time_ms);
  for (const Move& m : info.pv) std::printf(" %s", move_to_string(m).c_str());
  std::printf("\n");
  std::printf("bestmove %s\n",
              info.pv.empty() ? "(none)" : move_to_string(info.pv[0]).c_str());
  return 0;
}

int cmd_bench(int argc, char** argv) {
  const int depth = (argc > 2) ? std::atoi(argv[2]) : 10;
  RuleSet r = thai_rules();
  Position p;
  if (!parse_fen("W:W21-24,26,28,30,K32:B1-6,8,10,K14", p, r)) {
    std::fprintf(stderr, "bad fen\n");
    return 2;
  }

  Searcher s(32);
  SearchLimits lim;
  lim.depth = depth;
  SearchInfo info = s.go(p, r, lim);
  const double nps = info.time_ms > 0
                         ? double(info.nodes) / (double(info.time_ms) / 1000.0)
                         : 0.0;
  std::printf("depth %d nodes %llu nps %.0f bestmove %s pv",
              info.depth, (unsigned long long)info.nodes, nps,
              info.pv.empty() ? "(none)" : move_to_string(info.pv[0]).c_str());
  for (const Move& m : info.pv) std::printf(" %s", move_to_string(m).c_str());
  std::printf("\n");
  return 0;
}

int cmd_selfplay(int argc, char** argv) {
  const int games = (argc > 2) ? std::atoi(argv[2]) : 20;
  const int depth = (argc > 3) ? std::atoi(argv[3]) : 4;
  RuleSet r = thai_rules();
  std::mt19937_64 rng(20240607);

  int wins = 0, losses = 0, draws = 0;
  for (int g = 0; g < games; ++g) {
    Position p = start_position(r);
    const Color engine = (g % 2 == 0) ? WHITE : BLACK;
    std::vector<uint64_t> hist;
    Searcher searcher(16);
    int result = 0;  // +1 engine win, -1 engine loss, 0 draw

    for (int ply = 0; ply < 400; ++ply) {
      Move buf[256];
      const int n = generate_moves(p, r, buf, 256);
      if (n == 0) { result = (p.stm == engine) ? -1 : 1; break; }
      if (r.draw_no_progress_plies && p.rule50 >= r.draw_no_progress_plies) { result = 0; break; }

      Move m;
      if (p.stm == engine) {
        SearchLimits lim;
        lim.depth = depth;
        SearchInfo info = searcher.go(p, r, lim, hist);
        m = info.pv.empty() ? buf[0] : info.pv[0];
      } else {
        std::uniform_int_distribution<int> d(0, n - 1);
        m = buf[d(rng)];
      }
      hist.push_back(p.hash);
      do_move(p, m, r);
    }
    if (result > 0) ++wins;
    else if (result < 0) ++losses;
    else ++draws;
  }

  std::printf("selfplay %d games depth %d: W %d L %d D %d (%.1f%%)\n",
              games, depth, wins, losses, draws, games ? 100.0 * wins / games : 0.0);
  return 0;
}

} // namespace

int main(int argc, char** argv) {
  const std::string cmd = (argc >= 2) ? argv[1] : "";
  if (cmd == "perft") return cmd_perft(argc, argv, 2);
  if (cmd == "go") return cmd_go(argc, argv);
  if (cmd == "bench") return cmd_bench(argc, argv);
  if (cmd == "selfplay") return cmd_selfplay(argc, argv);
  return cmd_perft(argc, argv, 1);  // legacy: makhosfish [<pos>] [<depth>]
}
