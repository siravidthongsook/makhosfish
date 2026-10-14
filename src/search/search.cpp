#include "search/search.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
#include "core/movegen.hpp"
#include "core/types.hpp"
#include "eval/eval.hpp"

namespace mhf {

namespace {
int64_t now_ms() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}
} // namespace

Searcher::Searcher(size_t tt_mb) { tt_.resize_mb(tt_mb); }

void Searcher::clear() {
  tt_.clear();
  new_game();
  info_ = SearchInfo{};
}

void Searcher::new_game() {
  for (int i = 0; i < MAX_PLY; ++i) {
    killers_[i][0] = Move{};
    killers_[i][1] = Move{};
  }
  std::memset(history_, 0, sizeof(history_));
}

bool Searcher::insufficient(const Position& p) const {
  return p.men(WHITE) == 0 && p.men(BLACK) == 0 && popcount(p.occupied()) <= 2;
}

bool Searcher::is_repetition(uint64_t h, int ply) const {
  const int limit = rules_->draw_no_progress_plies;
  int seen = 0;
  for (int i = ply - 2, k = 0; i >= 0 && k < limit; i -= 2, ++k)
    if (path_[i] == h && ++seen >= 2) return true;
  const int gh = int(game_hist_.size());
  const int start = gh > limit ? gh - limit : 0;
  for (int i = gh - 1; i >= start; --i)
    if (game_hist_[i] == h && ++seen >= 2) return true;
  return false;
}

int64_t Searcher::elapsed_ms() const { return now_ms() - start_ms_; }

bool Searcher::stop_check() {
  if (stopped_) return true;
  if (external_stop_.load(std::memory_order_relaxed)) { stopped_ = true; return true; }
  if ((nodes_ & 511) == 0) {
    if (limits_.nodes_limit && nodes_ >= limits_.nodes_limit) { stopped_ = true; return true; }
    if (hard_ms_ > 0 && elapsed_ms() >= hard_ms_) { stopped_ = true; return true; }
  }
  return false;
}

int Searcher::score_move(const Position& p, const Move& m, const Move& ttMove, int ply) const {
  if (!ttMove.is_null() && m == ttMove) return 1 << 20;
  if (m.is_capture()) {
    int gain = popcount(m.captured & ~m.capturedKings) * MAN_VALUE +
               popcount(m.capturedKings) * KING_VALUE;
    if (m.promotes) gain += KING_VALUE - MAN_VALUE;
    return (1 << 19) + gain;
  }
  if (killers_[ply][0] == m) return (1 << 18) + 1;
  if (killers_[ply][1] == m) return (1 << 18);
  return (1 << 10) + history_[p.stm][m.from][m.to] + move_pst_delta(p, m, *rules_);
}

void Searcher::order_moves(const Position& p, Move* moves, int n, const Move& ttMove, int ply) {
  int scores[256];
  for (int i = 0; i < n; ++i) scores[i] = score_move(p, moves[i], ttMove, ply);
  for (int i = 1; i < n; ++i) {
    const Move m = moves[i];
    const int s = scores[i];
    int j = i - 1;
    while (j >= 0 && scores[j] < s) { moves[j + 1] = moves[j]; scores[j + 1] = scores[j]; --j; }
    moves[j + 1] = m;
    scores[j + 1] = s;
  }
}

int Searcher::negamax(Position& p, int depth, int alpha, int beta, int ply, bool pvNode) {
  pv_len_[ply] = 0;
  if (ply >= MAX_PLY - 1) return evaluate(p, *rules_, eval_mode_);
  if (ply > seldepth_) seldepth_ = ply;
  ++nodes_;
  if (stop_check()) { stopped_ = true; return 0; }

  const bool root = (ply == 0);

  Move moves[256];
  const int n = generate_moves(p, *rules_, moves, 256);
  if (n == 0) return -MATE_SCORE + ply;  // side to move has no legal move => it loses

  if (!root) {
    if (rules_->draw_no_progress_plies && p.rule50 >= rules_->draw_no_progress_plies)
      return DRAW_SCORE;
    // Kings capture kings, so a bare K-vs-K is only a draw while no capture is
    // available right now; otherwise the side to move wins on the spot.
    if (insufficient(p) && !moves[0].is_capture()) return DRAW_SCORE;
    if (is_repetition(p.hash, ply)) return DRAW_SCORE;
  }

  if (depth <= 0) return evaluate(p, *rules_, eval_mode_);

  // Mate-distance pruning keeps scores inside the current ply window.
  alpha = std::max(alpha, -MATE_SCORE + ply);
  beta = std::min(beta, MATE_SCORE - ply - 1);
  if (alpha >= beta) return alpha;

  Move ttMove;
  int ttScore = 0;
  tt_.set_ply(ply);
  const bool ttHit = tt_.probe(p.hash, depth, alpha, beta, ttScore, ttMove);
  if (ttHit && !pvNode) return ttScore;

  // Internal iterative deepening: seed a move when the table has none.
  if (depth >= 4 && ttMove.is_null()) {
    (void)negamax(p, depth - 2, alpha, beta, ply, pvNode);
    if (stopped_) return 0;
    if (pv_len_[ply] > 0) ttMove = pv_table_[ply][0];
  }

  order_moves(p, moves, n, ttMove, ply);

  const Color us = p.side();
  const int origAlpha = alpha;
  int best = -INF_SCORE;
  Move bestMove;
  int moveCount = 0;

  for (int i = 0; i < n; ++i) {
    const Move m = moves[i];
    const bool isCap = m.is_capture();
    const uint16_t prev50 = p.rule50;

    do_move(p, m, *rules_);
    path_[ply + 1] = p.hash;
    const int newDepth = depth - 1;
    int score;

    if (moveCount == 0) {
      score = -negamax(p, newDepth, -beta, -alpha, ply + 1, pvNode);
    } else {
      int r = 0;
      if (depth >= 3 && !isCap && moveCount >= 3) {
        r = 1;
        if (depth >= 6 && moveCount >= 6) r = 2;
      }
      score = -negamax(p, newDepth - r, -alpha - 1, -alpha, ply + 1, false);
      if (!stopped_ && score > alpha && r > 0)
        score = -negamax(p, newDepth, -alpha - 1, -alpha, ply + 1, false);
      if (!stopped_ && score > alpha && score < beta)
        score = -negamax(p, newDepth, -beta, -alpha, ply + 1, pvNode);
    }
    undo_move(p, m, prev50, *rules_);
    if (stopped_) return 0;

    ++moveCount;
    if (score > best) {
      best = score;
      bestMove = m;
      if (score > alpha) {
        alpha = score;
        pv_table_[ply][0] = m;
        for (int k = 0; k < pv_len_[ply + 1]; ++k)
          pv_table_[ply][k + 1] = pv_table_[ply + 1][k];
        pv_len_[ply] = pv_len_[ply + 1] + 1;
      }
    }

    if (alpha >= beta) {
      if (!isCap) {
        if (!(killers_[ply][0] == m)) {
          killers_[ply][1] = killers_[ply][0];
          killers_[ply][0] = m;
        }
        history_[us][m.from][m.to] += depth * depth;
      }
      break;
    }
  }

  const uint8_t flag = (best <= origAlpha) ? TT_UPPER : (best >= beta ? TT_LOWER : TT_EXACT);
  tt_.set_ply(ply);
  tt_.store(p.hash, depth, best, flag, bestMove);
  return best;
}

SearchInfo Searcher::go(Position& p, const RuleSet& r, const SearchLimits& limits,
                        const std::vector<uint64_t>& game_hashes) {
  rules_ = &r;
  limits_ = limits;
  nodes_ = 0;
  stopped_ = false;
  seldepth_ = 0;
  info_ = SearchInfo{};
  new_game();
  tt_.new_generation();
  game_hist_.assign(game_hashes.begin(), game_hashes.end());
  start_ms_ = now_ms();
  path_[0] = p.hash;

  if (limits.movetime_ms > 0) {
    hard_ms_ = soft_ms_ = limits.movetime_ms;
  } else if (limits.wtime_ms || limits.btime_ms) {
    const int64_t t = (p.stm == WHITE) ? limits.wtime_ms : limits.btime_ms;
    const int64_t inc = (p.stm == WHITE) ? limits.winc_ms : limits.binc_ms;
    int64_t alloc = t / 20 + inc / 2;
    if (alloc > t / 2) alloc = t / 2;
    if (alloc < 1) alloc = 1;
    soft_ms_ = alloc;
    hard_ms_ = alloc * 3;
    if (hard_ms_ > t - 50) hard_ms_ = t - 50;
    if (hard_ms_ < 1) hard_ms_ = 1;
  } else {
    soft_ms_ = hard_ms_ = 0;  // depth/node limited or infinite
  }

  int maxDepth = std::min(limits.depth, MAX_PLY - 1);
  if (maxDepth < 1) maxDepth = 1;

  int score = 0;
  int delta = 25;
  bool haveBest = false;

  for (int d = 1; d <= maxDepth; ++d) {
    int a = (d == 1) ? -INF_SCORE : std::max(-INF_SCORE, score - delta);
    int b = (d == 1) ? INF_SCORE : std::min(INF_SCORE, score + delta);
    for (;;) {
      score = negamax(p, d, a, b, 0, true);
      if (stopped_) break;
      if (score <= a) {
        a = std::max(-INF_SCORE, score - delta);
        delta += delta / 3;
      } else if (score >= b) {
        b = std::min(INF_SCORE, score + delta);
        delta += delta / 3;
      } else {
        break;
      }
    }
    if (stopped_) break;

    haveBest = true;
    info_.depth = d;
    info_.score_cp = score;
    info_.seldepth = seldepth_;
    info_.pv.assign(pv_table_[0], pv_table_[0] + pv_len_[0]);
    info_.nodes = nodes_;
    info_.time_ms = elapsed_ms();
    delta = 25;

    if (info_cb_) info_cb_(info_);

    if (soft_ms_ > 0 && elapsed_ms() >= soft_ms_) break;
    if (limits_.nodes_limit && nodes_ >= limits_.nodes_limit) break;
  }

  if (!haveBest || info_.pv.empty()) {
    Move buf[256];
    const int n = generate_moves(p, r, buf, 256);
    info_.pv.clear();
    if (n > 0) info_.pv.push_back(buf[0]);
  }

  info_.nodes = nodes_;
  info_.time_ms = elapsed_ms();
  return info_;
}

} // namespace mhf
