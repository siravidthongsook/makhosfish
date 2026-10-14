#include "eval/eval.hpp"
#include "core/types.hpp"

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

} // namespace

int pst_of(Color c, bool king, int sq) { return pst(c, king, sq); }

int evaluate(const Position& p, const RuleSet& r) {
  (void)r;
  int white = 0, black = 0;
  uint32_t b;
  b = p.wm; while (b) { int s = lsb(b); b &= b - 1; white += MAN_VALUE + MAN_PST[s]; }
  b = p.wk; while (b) { int s = lsb(b); b &= b - 1; white += KING_VALUE + KING_PST[s]; }
  b = p.bm; while (b) { int s = lsb(b); b &= b - 1; black += MAN_VALUE + MAN_PST[31 - s]; }
  b = p.bk; while (b) { int s = lsb(b); b &= b - 1; black += KING_VALUE + KING_PST[31 - s]; }
  int rel = white - black;
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
