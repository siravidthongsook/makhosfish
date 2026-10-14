#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include "core/movegen.hpp"
#include "core/perft.hpp"
#include "core/position.hpp"
#include "eval/eval.hpp"
#include "protocol/fen.hpp"
#include "protocol/pdn.hpp"
#include "protocol/tci.hpp"
#include "search/search.hpp"

using namespace mhf;

namespace {

bool load_position(const std::string& tok, const RuleSet& r, Position& p) {
  if (tok == "startpos") { p = start_position(r); return true; }
  return parse_fen(tok, p, r);
}

// Simple 8x8 board for the `play`/`pdn` commands. Uppercase = king.
std::string render_board(const Position& p) {
  std::string s;
  s += "    1  2  3  4  5  6  7  8\n";
  for (int r = 0; r < 8; ++r) {
    s += "  ";
    s += char('a' + r);
    for (int c = 0; c < 8; ++c) {
      s += ' ';
      if (((r + c) & 1) == 0) { s += '.'; s += ' '; continue; }
      const int f = (r & 1) ? c / 2 : (c - 1) / 2;
      const int sq = r * 4 + f;
      char ch = '.';
      if (p.wm & bit(sq)) ch = 'w';
      else if (p.wk & bit(sq)) ch = 'W';
      else if (p.bm & bit(sq)) ch = 'b';
      else if (p.bk & bit(sq)) ch = 'B';
      s += ch; s += ' ';
    }
    s += '\n';
  }
  s += (p.stm == WHITE) ? "  White (w) to move\n" : "  Black (b) to move\n";
  return s;
}

EvalMode parse_eval(const std::string& s) {
  return (s == "e0" || s == "E0") ? EvalMode::E0 : EvalMode::E1;
}
const char* eval_name(EvalMode m) { return m == EvalMode::E0 ? "e0" : "e1"; }

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
  if (argc > 4) s.set_eval_mode(parse_eval(argv[4]));
  SearchInfo info = s.go(p, r, lim);

  std::printf("info depth %d seldepth %d score cp %d nodes %llu time %lld pv",
              info.depth, info.seldepth, info.score_cp,
              (unsigned long long)info.nodes, (long long)info.time_ms);
  for (const Move& m : info.pv) std::printf(" %s", move_to_notation(p, r, m).c_str());
  std::printf("\n");
  std::printf("bestmove %s\n",
              info.pv.empty() ? "(none)" : move_to_notation(p, r, info.pv[0]).c_str());
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
  if (argc > 3) s.set_eval_mode(parse_eval(argv[3]));
  SearchLimits lim;
  lim.depth = depth;
  SearchInfo info = s.go(p, r, lim);
  const double nps = info.time_ms > 0
                         ? double(info.nodes) / (double(info.time_ms) / 1000.0)
                         : 0.0;
  std::printf("depth %d nodes %llu nps %.0f bestmove %s pv",
              info.depth, (unsigned long long)info.nodes, nps,
              info.pv.empty() ? "(none)" : move_to_notation(p, r, info.pv[0]).c_str());
  for (const Move& m : info.pv) std::printf(" %s", move_to_notation(p, r, m).c_str());
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

// Play engine A (modeA) against engine B (modeB), colors alternating, random
// openings for diversity. Reports score and a logistic Elo delta -- the M2
// exit gate "E1 beats E0" (spec §16).
int cmd_gauntlet(int argc, char** argv) {
  const int games = (argc > 2) ? std::atoi(argv[2]) : 50;
  const int depth = (argc > 3) ? std::atoi(argv[3]) : 6;
  const EvalMode modeA = (argc > 4) ? parse_eval(argv[4]) : EvalMode::E1;
  const EvalMode modeB = (argc > 5) ? parse_eval(argv[5]) : EvalMode::E0;
  const int opening = (argc > 6) ? std::atoi(argv[6]) : 4;
  RuleSet r = thai_rules();
  std::mt19937_64 rng(0xC0FFEEull);

  int aWins = 0, bWins = 0, draws = 0;
  const auto t0 = std::chrono::steady_clock::now();

  for (int g = 0; g < games; ++g) {
    Position p = start_position(r);
    const Color aColor = (g % 2 == 0) ? WHITE : BLACK;
    Searcher sa(32), sb(32);
    sa.set_eval_mode(modeA);
    sb.set_eval_mode(modeB);
    std::vector<uint64_t> hist;
    std::unordered_map<uint64_t, int> seen;
    int result = 0;  // +1 A win, -1 B win, 0 draw

    for (int ply = 0; ply < 400; ++ply) {
      Move buf[256];
      const int n = generate_moves(p, r, buf, 256);
      if (n == 0) { result = (p.stm == aColor) ? -1 : 1; break; }
      if (r.draw_no_progress_plies && p.rule50 >= r.draw_no_progress_plies) { result = 0; break; }
      if (++seen[p.hash] >= 3) { result = 0; break; }

      Move m;
      if (ply < opening) {
        std::uniform_int_distribution<int> d(0, n - 1);
        m = buf[d(rng)];
      } else {
        SearchLimits lim;
        lim.depth = depth;
        Searcher& s = (p.stm == aColor) ? sa : sb;
        SearchInfo info = s.go(p, r, lim, hist);
        m = info.pv.empty() ? buf[0] : info.pv[0];
      }
      hist.push_back(p.hash);
      do_move(p, m, r);
    }

    if (result > 0) ++aWins;
    else if (result < 0) ++bWins;
    else ++draws;
  }

  const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  const double score = games ? (aWins + 0.5 * draws) / games : 0.0;
  double elo = 0.0;
  if (score >= 1.0) elo = 999.0;
  else if (score <= 0.0) elo = -999.0;
  else elo = -400.0 * std::log10(1.0 / score - 1.0);

  std::printf("gauntlet %d games depth %d A=%s B=%s opening %d: A %d B %d D %d  "
              "score %.3f  ~%+.0f Elo  (%.1fs)\n",
              games, depth, eval_name(modeA), eval_name(modeB), opening,
              aWins, bWins, draws, score, elo, sec);
  return 0;
}

// TCI v1 protocol server on stdin/stdout (spec §11.1) -- the GUI entry point.
int cmd_tci() {
  TciSession session;
  session.run(std::cin, std::cout);
  return 0;
}

// Load a PDN game, replay it against the rules, and report. `--fens` prints the
// position after every ply.
int cmd_pdn(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: makhosfish pdn <file.pdn> [--fens]\n");
    return 2;
  }
  std::ifstream f(argv[2]);
  if (!f) { std::fprintf(stderr, "cannot open %s\n", argv[2]); return 2; }
  std::stringstream ss;
  ss << f.rdbuf();

  const RuleSet r = thai_rules();
  const PdnGame g = parse_pdn(ss.str());
  std::printf("pdn: %zu tags, %zu moves, result %s\n", g.tags.size(), g.moves.size(),
              g.result.empty() ? "'*'" : g.result.c_str());
  for (const PdnTag& t : g.tags) std::printf("  [%s \"%s\"]\n", t.name.c_str(), t.value.c_str());

  const PdnReplay rep = replay_pdn(g, r);
  if (!rep.ok) { std::printf("replay FAILED: %s\n", rep.error.c_str()); return 1; }
  std::printf("replay ok; final fen %s\n", to_fen(rep.final).c_str());

  if (argc > 3 && std::string(argv[3]) == "--fens")
    for (size_t i = 0; i < rep.positions.size(); ++i)
      std::printf("  ply %zu %s\n", i, to_fen(rep.positions[i]).c_str());
  return 0;
}

// Minimal interactive session: you play White, the engine plays Black.
int cmd_play(int argc, char** argv) {
  const int depth = (argc > 2) ? std::atoi(argv[2]) : 8;
  const EvalMode mode = (argc > 3) ? parse_eval(argv[3]) : EvalMode::E1;
  RuleSet r = thai_rules();
  Position p = start_position(r);
  Searcher s(64);
  s.set_eval_mode(mode);
  std::vector<uint64_t> hist;

  std::printf("%s\n", render_board(p).c_str());
  std::printf("You are White (w). Commands: <move> (e.g. 24-20, 26x17x10), "
              "'hint', 'fen', 'quit'.\n");

  std::string line;
  for (;;) {
    Move buf[256];
    const int n = generate_moves(p, r, buf, 256);
    if (n == 0) {
      std::printf("%s has no legal move -> %s wins.\n",
                  p.stm == WHITE ? "White" : "Black",
                  p.stm == WHITE ? "Black" : "White");
      break;
    }

    if (p.stm == BLACK) {
      SearchLimits lim; lim.depth = depth;
      const SearchInfo info = s.go(p, r, lim, hist);
      const Move m = info.pv.empty() ? buf[0] : info.pv[0];
      std::printf("engine: %s  (depth %d, score cp %d)\n",
                  move_to_notation(p, r, m).c_str(), info.depth, info.score_cp);
      hist.push_back(p.hash);
      do_move(p, m, r);
      std::printf("%s\n", render_board(p).c_str());
      continue;
    }

    std::printf("you> ");
    std::fflush(stdout);
    if (!std::getline(std::cin, line)) break;
    std::string t;
    for (char c : line) if (!std::isspace((unsigned char)c)) t.push_back(c);
    if (t == "quit" || t == "exit") break;
    if (t == "fen") { std::printf("%s\n", to_fen(p).c_str()); continue; }
    if (t == "hint") {
      SearchLimits lim; lim.depth = depth;
      const SearchInfo info = s.go(p, r, lim, hist);
      if (!info.pv.empty()) std::printf("hint: %s\n", move_to_notation(p, r, info.pv[0]).c_str());
      continue;
    }
    Move m;
    if (!parse_move(p, t, r, m)) { std::printf("illegal move: %s\n", line.c_str()); continue; }
    hist.push_back(p.hash);
    do_move(p, m, r);
    std::printf("%s\n", render_board(p).c_str());
  }
  return 0;
}

} // namespace

int main(int argc, char** argv) {
  const std::string cmd = (argc >= 2) ? argv[1] : "";
  if (cmd == "tci") return cmd_tci();
  if (cmd == "pdn") return cmd_pdn(argc, argv);
  if (cmd == "play") return cmd_play(argc, argv);
  if (cmd == "perft") return cmd_perft(argc, argv, 2);
  if (cmd == "go") return cmd_go(argc, argv);
  if (cmd == "bench") return cmd_bench(argc, argv);
  if (cmd == "selfplay") return cmd_selfplay(argc, argv);
  if (cmd == "gauntlet") return cmd_gauntlet(argc, argv);
  return cmd_perft(argc, argv, 1);  // legacy: makhosfish [<pos>] [<depth>]
}
