#pragma once
#include <cstdint>
#include <vector>
#include "core/move.hpp"
#include "core/position.hpp"
#include "core/rules.hpp"
#include "eval/eval.hpp"
#include "search/tt.hpp"

namespace mhf {

struct SearchLimits {
  int depth = 64;
  int64_t movetime_ms = 0;
  int64_t wtime_ms = 0, btime_ms = 0, winc_ms = 0, binc_ms = 0;
  uint64_t nodes_limit = 0;
  bool infinite = false;
};

struct SearchInfo {
  int depth = 0, seldepth = 0;
  int score_cp = 0;
  uint64_t nodes = 0;
  int64_t time_ms = 0;
  std::vector<Move> pv;
};

// PVS + iterative deepening + aspiration windows + TT (spec §7).
class Searcher {
 public:
  explicit Searcher(size_t tt_mb = 64);

  void clear();
  void new_game();

  // Selects the evaluation stage (E1 default). Lets a gauntlet pit E1 vs E0.
  void set_eval_mode(EvalMode m) { eval_mode_ = m; }
  EvalMode eval_mode() const { return eval_mode_; }

  // `game_hashes` = hashes of the game history *before* the root position.
  SearchInfo go(Position& p, const RuleSet& r, const SearchLimits& limits,
                const std::vector<uint64_t>& game_hashes = {});

  const SearchInfo& info() const { return info_; }

 private:
  int negamax(Position& p, int depth, int alpha, int beta, int ply, bool pvNode);
  void order_moves(const Position& p, Move* moves, int n, const Move& ttMove, int ply);
  int score_move(const Position& p, const Move& m, const Move& ttMove, int ply) const;
  bool is_repetition(uint64_t h, int ply) const;
  bool insufficient(const Position& p) const;
  bool stop_check();
  int64_t elapsed_ms() const;

  TTable tt_;
  const RuleSet* rules_ = nullptr;
  SearchInfo info_;
  SearchLimits limits_;
  EvalMode eval_mode_ = EvalMode::E1;

  uint64_t nodes_ = 0;
  bool stopped_ = false;
  int64_t start_ms_ = 0;
  int64_t soft_ms_ = 0;
  int64_t hard_ms_ = 0;
  int seldepth_ = 0;

  int pv_len_[MAX_PLY];
  Move pv_table_[MAX_PLY][MAX_PLY];
  Move killers_[MAX_PLY][2];
  int history_[2][32][32];
  uint64_t path_[MAX_PLY];
  std::vector<uint64_t> game_hist_;
};

} // namespace mhf
