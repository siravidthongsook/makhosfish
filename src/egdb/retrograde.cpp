#include "egdb/retrograde.hpp"

#include <deque>
#include "core/bitboard.hpp"
#include "core/movegen.hpp"

namespace mhf {
namespace {

// Forward directions of a man of color `us` (NW,NE for White; SW,SE for Black).
void forward_dirs(Color us, int dirs[2]) {
  if (us == WHITE) { dirs[0] = NW; dirs[1] = NE; }
  else             { dirs[0] = SW; dirs[1] = SE; }
}

bool on_promo_rank(Color us, int sq) {
  const int row = tables().rowOf(sq);
  return us == BLACK ? row == 7 : row == 0;
}

// Predecessors of `q` under QUIET (non-capturing) moves, i.e. the parents p with
// a legal quiet move p -> q. Same-piece-count edges only: captures reduce the
// piece count and are handled forward against the already-solved lower tables.
//
// A quiet move is either a man step (crowning or not) or a king slide, so the
// reverse is: for each mover piece sitting on square `to` in q, walk back to the
// squares it could legally have come from. The move must also be legal in p,
// which for draughts means p must have NO capture available (compulsory capture);
// that is checked per candidate before it is kept.
int generate_predecessors(const Position& q, const RuleSet& r, Position* out, int cap) {
  const BoardTables& T = tables();
  const Color mover = opposite(q.side());
  const uint32_t empty = q.empty();
  int n = 0;

  auto try_add = [&](int from, int to, bool king_in_p) {
    if (n >= cap) return;
    Position p = q;
    if (mover == WHITE) { p.wm &= ~bit(to); p.wk &= ~bit(to); }
    else                { p.bm &= ~bit(to); p.bk &= ~bit(to); }
    if (mover == WHITE) { if (king_in_p) p.wk |= bit(from); else p.wm |= bit(from); }
    else                { if (king_in_p) p.bk |= bit(from); else p.bm |= bit(from); }
    p.stm = uint8_t(mover);
    p.hash = 0;
    Move capbuf[256];
    if (generate_captures(p, r, capbuf, 256) != 0) return;  // quiet move illegal if a capture exists
    out[n++] = p;
  };

  int backward[2];
  forward_dirs(opposite(mover), backward);  // mover's backward dirs

  // Kings in q: a king slid in from any square along a diagonal, or a man
  // stepped onto the crown row and crowned here.
  uint32_t kk = (mover == WHITE ? q.wk : q.bk);
  while (kk) {
    const int to = lsb(kk);
    kk &= kk - 1;
    for (int d = 0; d < 4; ++d) {
      int s = T.neighbor[to][d];
      while (s >= 0 && (empty & bit(s))) {
        try_add(s, to, true);
        s = T.neighbor[s][d];
      }
    }
    if (on_promo_rank(mover, to)) {
      for (int i = 0; i < 2; ++i) {
        const int s = T.neighbor[to][backward[i]];
        if (s >= 0 && (empty & bit(s))) try_add(s, to, false);
      }
    }
  }

  // Men in q: a man stepped forward one square (and did not crown).
  uint32_t mm = (mover == WHITE ? q.wm : q.bm);
  while (mm) {
    const int to = lsb(mm);
    mm &= mm - 1;
    if (on_promo_rank(mover, to)) continue;  // would have crowned, not stayed a man
    for (int i = 0; i < 2; ++i) {
      const int s = T.neighbor[to][backward[i]];
      if (s >= 0 && (empty & bit(s))) try_add(s, to, false);
    }
  }
  return n;
}

void solve_k(Egdb& e, int k, const RuleSet& r) {
  const uint64_t N = e.layout.k_size[size_t(k)];
  auto& val = e.values[size_t(k)];
  val.assign(size_t(N), Wdl::Unknown);
  std::vector<uint16_t> remaining(size_t(N), 0);
  std::deque<uint32_t> queue;

  // Forward sweep: terminal positions lose; a capture into a lost lower position
  // wins; otherwise count the children that are not yet known to win.
  for (uint64_t i = 0; i < N; ++i) {
    const Position p = e.layout.decode(k, i);
    Move buf[256];
    const int n = generate_moves(p, r, buf, 256);
    if (n == 0) {
      val[size_t(i)] = Wdl::Loss;
      queue.push_back(uint32_t(i));
      continue;
    }
    uint32_t cnt = 0;
    bool win = false;
    for (int j = 0; j < n; ++j) {
      Position q = p;
      do_move(q, buf[j], r);
      int kk = 0;
      const int64_t c = e.layout.encode(q, kk);
      if (kk == k) {
        ++cnt;  // same-level edge, counted until it resolves (via reverse movegen)
      } else if (kk >= 1) {
        const Wdl v = e.values[size_t(kk)][size_t(c)];
        if (v == Wdl::Loss) win = true;
        else if (v != Wdl::Win) ++cnt;
      }
    }
    if (win) {
      val[size_t(i)] = Wdl::Win;
      queue.push_back(uint32_t(i));
    } else if (cnt == 0) {
      val[size_t(i)] = Wdl::Loss;  // every child is already a win for the opponent
      queue.push_back(uint32_t(i));
    } else {
      remaining[size_t(i)] = uint16_t(cnt);
    }
  }

  // Backward induction: propagate resolved values to their quiet predecessors.
  while (!queue.empty()) {
    const uint32_t i = queue.front();
    queue.pop_front();
    const Wdl v = val[i];
    if (v == Wdl::Draw || v == Wdl::Unknown) continue;
    const Position q = e.layout.decode(k, i);
    Position parents[256];
    const int np = generate_predecessors(q, r, parents, 256);
    for (int j = 0; j < np; ++j) {
      int kk = 0;
      const int64_t c64 = e.layout.encode(parents[j], kk);
      if (kk != k || c64 < 0) continue;
      const uint32_t c = uint32_t(c64);
      if (val[c] != Wdl::Unknown) continue;
      if (v == Wdl::Loss) {
        val[c] = Wdl::Win;
        queue.push_back(c);
      } else {  // v == Win
        if (--remaining[c] == 0) {
          val[c] = Wdl::Loss;
          queue.push_back(c);
        }
      }
    }
  }

  for (uint64_t i = 0; i < N; ++i)
    if (val[size_t(i)] == Wdl::Unknown) val[size_t(i)] = Wdl::Draw;
}

} // namespace

Wdl Egdb::probe(const Position& p) const {
  int k = 0;
  const int64_t slot = layout.encode(p, k);
  if (slot < 0 || size_t(k) >= values.size()) return Wdl::Unknown;
  const auto& table = values[size_t(k)];
  if (table.empty() || size_t(slot) >= table.size()) return Wdl::Unknown;
  return table[size_t(slot)];
}

Egdb solve_upto(int kmax, const RuleSet& r) {
  Egdb e;
  e.layout.build(kmax);
  e.values.assign(size_t(kmax + 1), {});
  for (int k = 1; k <= kmax; ++k) solve_k(e, k, r);
  return e;
}

} // namespace mhf
