#include "protocol/tci.hpp"
#include "core/perft.hpp"
#include "eval/pattern.hpp"
#include "protocol/fen.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <istream>
#include <ostream>
#include <sstream>

namespace mhf {
namespace {

std::vector<std::string> tokenize(const std::string& line) {
  std::vector<std::string> out;
  std::istringstream ss(line);
  std::string t;
  while (ss >> t) out.push_back(t);
  return out;
}

int64_t to_int(const std::string& s) { return std::atoll(s.c_str()); }

} // namespace

TciSession::TciSession() { pos_ = start_position(rules_); }

TciSession::~TciSession() {
  if (searching_.load()) { searcher_.request_stop(); }
  if (worker_.joinable()) worker_.join();
}

void TciSession::out_line(const std::string& s) {
  std::lock_guard<std::mutex> lock(mu_);
  if (out_) { (*out_) << s << '\n'; out_->flush(); }
}

void TciSession::join_search() {
  if (worker_.joinable()) worker_.join();
  searching_.store(false);
}

void TciSession::run(std::istream& in, std::ostream& out) {
  out_ = &out;
  std::string line;
  while (std::getline(in, line)) {
    // Strip a trailing CR (Windows line endings) and any stray NUL.
    while (!line.empty() && (line.back() == '\r' || line.back() == '\0')) line.pop_back();
    if (!line.empty()) handle(line);
    if (quit_) break;
  }
  if (searching_.load()) searcher_.request_stop();
  join_search();
  out_ = nullptr;
}

void TciSession::handle(const std::string& line) {
  const std::vector<std::string> tok = tokenize(line);
  if (tok.empty()) return;
  const std::string& cmd = tok[0];

  // A running search owns pos_/rules_; join before any command that would
  // touch them (stop/quit/isready stay non-blocking).
  if (searching_.load() && cmd != "stop" && cmd != "quit" && cmd != "isready")
    join_search();

  if (cmd == "tci" || cmd == "uci") {
    out_line("id name MakHosFish 0.1");
    out_line("id author MakHosFish");
    out_line("option name Hash type spin default 128 min 1 max 32768");
    out_line("option name Eval type combo default e2 var e0 var e1 var e2");
    out_line("option name PatternFile type string default <empty>");
    out_line("option name RuleSet type combo default thai var thai var english var russian var brazilian");
    out_line(cmd == "uci" ? "uciok" : "tciok");
    return;
  }

  if (cmd == "isready") { out_line("readyok"); return; }

  if (cmd == "tcinewgame" || cmd == "ucinewgame") {
    searcher_.clear();
    history_.clear();
    pos_ = start_position(rules_);
    return;
  }

  if (cmd == "setoption") {
    // setoption name <X> value <V>
    std::string name, value;
    for (size_t i = 1; i < tok.size(); ++i) {
      if (tok[i] == "name") { while (i + 1 < tok.size() && tok[i + 1] != "value") name += (name.empty() ? "" : " ") + tok[++i]; }
      else if (tok[i] == "value") { while (i + 1 < tok.size()) value += (value.empty() ? "" : " ") + tok[++i]; }
    }
    if (name == "Hash") {
      int mb = std::max(1, std::min(32768, (int)to_int(value)));
      searcher_.resize_hash(size_t(mb));
      searcher_.clear();
    } else if (name == "Eval") {
      if (value == "e0") searcher_.set_eval_mode(EvalMode::E0);
      else if (value == "e2") searcher_.set_eval_mode(EvalMode::E2);
      else searcher_.set_eval_mode(EvalMode::E1);
    } else if (name == "PatternFile") {
      if (!value.empty() && !active_pattern().load(value))
        out_line("info string cannot load pattern file: " + value);
    } else if (name == "RuleSet") {
      if (value == "english") rules_ = english_rules();
      else if (value == "russian") rules_ = russian_rules();
      else if (value == "brazilian") rules_ = brazilian_rules();
      else rules_ = thai_rules();
      pos_ = start_position(rules_);
      history_.clear();
    }
    // Threads (and unknown options) are accepted and ignored.
    return;
  }

  if (cmd == "position") { cmd_position(tok); return; }
  if (cmd == "go") { cmd_go(tok); return; }
  if (cmd == "stop") { searcher_.request_stop(); return; }
  if (cmd == "quit") { searcher_.request_stop(); quit_ = true; return; }

  if (cmd == "perft") {
    int depth = (tok.size() > 1) ? (int)to_int(tok[1]) : 1;
    for (int d = 1; d <= depth; ++d)
      out_line("perft " + std::to_string(d) + " " + std::to_string(perft(pos_, rules_, d)));
    return;
  }
  // Unknown command: ignore, per UCI convention.
}

void TciSession::cmd_position(const std::vector<std::string>& tok) {
  size_t i = 1;
  history_.clear();

  if (i < tok.size() && tok[i] == "startpos") {
    pos_ = start_position(rules_);
    ++i;
  } else if (i < tok.size() && tok[i] == "fen") {
    ++i;
    std::string fen;
    while (i < tok.size() && tok[i] != "moves") { fen += tok[i++]; }  // frozen FEN has no spaces
    if (!parse_fen(fen, pos_, rules_)) {
      out_line("info string bad fen: " + fen);
      pos_ = start_position(rules_);
      return;
    }
  } else {
    return;  // unrecognized
  }

  if (i < tok.size() && tok[i] == "moves") {
    ++i;
    for (; i < tok.size(); ++i) {
      Move m;
      if (!parse_move(pos_, tok[i], rules_, m)) {
        out_line("info string illegal move: " + tok[i]);
        break;
      }
      history_.push_back(pos_.hash);
      do_move(pos_, m, rules_);
    }
  }
}

void TciSession::cmd_go(const std::vector<std::string>& tok) {
  SearchLimits lim;
  lim.depth = MAX_PLY - 1;
  bool have_time = false;
  for (size_t i = 1; i < tok.size(); ++i) {
    const std::string& k = tok[i];
    auto val = [&](int64_t& dst) { if (i + 1 < tok.size()) dst = to_int(tok[++i]); };
    if (k == "depth") { int64_t v = 0; val(v); lim.depth = (int)std::max<int64_t>(1, std::min<int64_t>(v, MAX_PLY - 1)); }
    else if (k == "nodes") { int64_t v = 0; val(v); lim.nodes_limit = (uint64_t)std::max<int64_t>(0, v); }
    else if (k == "movetime") { val(lim.movetime_ms); have_time = true; }
    else if (k == "wtime") { val(lim.wtime_ms); have_time = true; }
    else if (k == "btime") { val(lim.btime_ms); have_time = true; }
    else if (k == "winc") val(lim.winc_ms);
    else if (k == "binc") val(lim.binc_ms);
    else if (k == "infinite") lim.infinite = true;
    else if (k == "movestogo") { int64_t v = 0; val(v); (void)v; }
  }
  // `infinite` has no natural bound here; cap it so the engine always responds.
  if (lim.infinite && !have_time) lim.depth = std::min(lim.depth, 32);
  if (!have_time && lim.nodes_limit == 0 && lim.depth == MAX_PLY - 1 && !lim.infinite)
    lim.depth = 12;  // bare `go`

  start_search(lim);
}

void TciSession::start_search(const SearchLimits& lim) {
  join_search();
  Position root = pos_;
  const RuleSet rules = rules_;
  const std::vector<uint64_t> hist = history_;

  searcher_.set_info_callback([this, root, rules](const SearchInfo& inf) {
    std::string s = "info depth " + std::to_string(inf.depth) +
                    " seldepth " + std::to_string(inf.seldepth) +
                    " score cp " + std::to_string(inf.score_cp) +
                    " nodes " + std::to_string(inf.nodes) +
                    " time " + std::to_string(inf.time_ms) + " pv";
    for (const Move& m : inf.pv) s += " " + move_to_notation(root, rules, m);
    out_line(s);
  });

  searcher_.clear_stop();  // clear before the worker can observe a racing `stop`
  searching_.store(true);
  worker_ = std::thread([this, lim, root, rules, hist]() mutable {
    SearchInfo inf = searcher_.go(root, rules, lim, hist);
    const std::string bm =
        inf.pv.empty() ? "(none)" : move_to_notation(root, rules, inf.pv[0]);
    out_line("bestmove " + bm);
    searching_.store(false);
  });
}

} // namespace mhf
