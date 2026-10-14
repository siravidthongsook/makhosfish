#include "eval/eval.hpp"
#include "core/bitboard.hpp"
#include "core/types.hpp"
#include "eval/pattern.hpp"

namespace mhf {
namespace {

// Indexed internally 0..31 = row*4 + file, row 0 = White's promotion rank.
// Black uses the 180-degree rotation `31 - sq` (colors are mirror images).
constexpr int MAN_PST[32] = {
  // row 0 (promotion rank, transient for a man): best
  44, 48, 48, 44,
  // row 1
  34, 38, 38, 34,
  // row 2
  26, 30, 30, 26,
  // row 3
  20, 24, 24, 20,
  // row 4
  14, 18, 18, 14,
  // row 5
  10, 12, 12, 10,
  // row 6 (White's starting rows)
  6, 8, 8, 6,
  // row 7
  2, 2, 2, 2,
};

constexpr int KING_PST[32] = {
  -8, -4, -4, -8,
  -2,  4,  4, -2,
   2,  8,  8,  2,
   4, 10, 10,  4,
   4, 10, 10,  4,
   2,  8,  8,  2,
  -2,  4,  4, -2,
  -8, -4, -4, -8,
};

inline int pst(Color c, bool king, int sq) {
  int whiteSq = (c == WHITE) ? sq : (31 - sq);
  return king ? KING_PST[whiteSq] : MAN_PST[whiteSq];
}

// Empty squares a flying king on `sq` can slide to before the first blocker.
// Ray squares toward NW/NE have a lower index than `sq`; toward SW/SE higher.
inline uint32_t king_reach(int sq, uint32_t occ, uint32_t empty) {
  const BoardTables& T = tables();
  uint32_t reach = 0;
  for (int d = 0; d < 4; ++d) {
    const uint32_t ray = T.rayMask[sq][d];
    const uint32_t blockers = ray & occ;
    if (!blockers) { reach |= ray & empty; continue; }
    if (d == NW || d == NE) {
      const int near = msb(blockers);                 // closest blocker = highest index
      reach |= ray & empty & ~((bit(near) << 1) - 1u); // keep squares above it
    } else {
      const int near = lsb(blockers);                 // closest blocker = lowest index
      reach |= ray & empty & (bit(near) - 1u);        // keep squares below it
    }
  }
  return reach;
}

// Per-color E1 feature counts (no weights applied yet).
struct SideFeatures {
  int men_mob = 0;     // empty forward squares available to men
  int king_reach = 0;  // empty squares reachable by kings
  int cap_threat = 0;  // enemy men capturable right now
  int defended = 0;    // men supported from behind
  int precrown = 0;    // men one step from the crown row
};

SideFeatures side_features(const Position& p, Color c) {
  const BoardTables& T = tables();
  const uint32_t occ = p.occupied();
  const uint32_t empty = ~occ & 0xFFFFFFFFu;
  const uint32_t them = p.pieces(opposite(c));
  const uint32_t ourMen = p.men(c);

  int fwd[2], back[2];
  if (c == WHITE) { fwd[0] = NW; fwd[1] = NE; back[0] = SW; back[1] = SE; }
  else            { fwd[0] = SW; fwd[1] = SE; back[0] = NW; back[1] = NE; }
  const int crownRow = (c == WHITE) ? 1 : 6;  // one step below the promotion rank

  SideFeatures s;
  uint32_t m = p.men(c);
  while (m) {
    const int sq = lsb(m); m &= m - 1;
    for (int i = 0; i < 2; ++i) {
      const uint32_t step = T.stepMask[sq][fwd[i]];
      if (step && (empty & step)) ++s.men_mob;
      const int mid = T.neighbor[sq][fwd[i]];
      if (mid >= 0 && (them & bit(mid))) {
        const int land = T.neighbor[mid][fwd[i]];
        if (land >= 0 && (empty & bit(land))) ++s.cap_threat;
      }
    }
    // Supported from the diagonal behind. Kept out of the loop above so a
    // defender never suppresses the mobility/capture counts of the other side.
    if ((T.neighbor[sq][back[0]] >= 0 && (ourMen & bit(T.neighbor[sq][back[0]]))) ||
        (T.neighbor[sq][back[1]] >= 0 && (ourMen & bit(T.neighbor[sq][back[1]]))))
      ++s.defended;
    if (T.rowOf(sq) == crownRow) ++s.precrown;
  }

  uint32_t k = p.kings(c);
  while (k) { const int sq = lsb(k); k &= k - 1; s.king_reach += popcount(king_reach(sq, occ, empty)); }
  return s;
}

} // namespace

int pst_of(Color c, bool king, int sq) { return pst(c, king, sq); }

EvalDetail eval_detail(const Position& p, const RuleSet& r, EvalMode mode) {
  (void)r;
  EvalDetail d;

  // Stage E0: material + piece-square tables.
  uint32_t b;
  b = p.wm; while (b) { const int s = lsb(b); b &= b - 1; d.material += MAN_VALUE;  d.pst += MAN_PST[s]; }
  b = p.wk; while (b) { const int s = lsb(b); b &= b - 1; d.material += KING_VALUE; d.pst += KING_PST[s]; }
  b = p.bm; while (b) { const int s = lsb(b); b &= b - 1; d.material -= MAN_VALUE;  d.pst -= MAN_PST[31 - s]; }
  b = p.bk; while (b) { const int s = lsb(b); b &= b - 1; d.material -= KING_VALUE; d.pst -= KING_PST[31 - s]; }

  if (mode == EvalMode::E0) {
    d.white_minus_black = d.material + d.pst;
    return d;
  }

  // Stage E1: hand features.
  const SideFeatures w = side_features(p, WHITE);
  const SideFeatures bl = side_features(p, BLACK);
  d.mobility = MOB_MAN * (w.men_mob - bl.men_mob) + MOB_KING * (w.king_reach - bl.king_reach);
  d.captures = CAP_THREAT * (w.cap_threat - bl.cap_threat);
  d.structure = DEFENDED * (w.defended - bl.defended);
  d.precrown = PRECROWN * (w.precrown - bl.precrown);

  const bool wHasKing = p.wk != 0, bHasKing = p.bk != 0;
  if (wHasKing != bHasKing) d.first_king = wHasKing ? FIRST_KING : -FIRST_KING;

  // Stage E2: add the learned pattern-window sum (zero when no weights loaded,
  // in which case E2 is bit-identical to E1).
  if (mode == EvalMode::E2) d.pattern = active_pattern().score(p);

  d.white_minus_black = d.material + d.pst + d.mobility + d.captures +
                        d.structure + d.first_king + d.precrown + d.pattern;
  return d;
}

int evaluate(const Position& p, const RuleSet& r, EvalMode mode) {
  const int rel = eval_detail(p, r, mode).white_minus_black;
  // tempo always favors the side to move
  return (p.stm == WHITE ? rel : -rel) + TEMPO;
}

int move_pst_delta(const Position& p, const Move& m, const RuleSet& r) {
  (void)r;
  const Color us = p.side();
  const bool wasKing = (p.kings(us) & bit(m.from)) != 0;
  const bool nowKing = wasKing || m.promotes;
  return pst(us, nowKing, m.to) - pst(us, wasKing, m.from);
}

} // namespace mhf
