#include "core/bitboard.hpp"

namespace mhf {

static constexpr int DR[4] = {-1, -1, 1, 1};
static constexpr int DC[4] = {-1, 1, -1, 1};

int BoardTables::rcToIdx(int r, int c) const {
  if (r < 0 || r > 7 || c < 0 || c > 7) return -1;
  if (!isDark(r, c)) return -1;
  int p = (r & 1) == 0 ? (c - 1) / 2 : c / 2;
  return r * 4 + p;
}

static BoardTables make_tables() {
  BoardTables t{};
  for (int sq = 0; sq < NB_SQUARES; ++sq) {
    int r = sq / 4;
    int p = sq % 4;
    int c = (r & 1) == 0 ? 2 * p + 1 : 2 * p;
    t.sqRow[sq] = r;
    t.sqCol[sq] = c;

    for (int d = 0; d < 4; ++d) {
      t.neighbor[sq][d] = -1;
      t.stepMask[sq][d] = 0;
      t.rayMask[sq][d] = 0;
    }
  }
  for (int sq = 0; sq < NB_SQUARES; ++sq) {
    int r = t.sqRow[sq], c = t.sqCol[sq];
    for (int d = 0; d < 4; ++d) {
      int cr = r + DR[d], cc = c + DC[d];
      int nb = t.rcToIdx(cr, cc);
      if (nb >= 0) {
        t.neighbor[sq][d] = nb;
        t.stepMask[sq][d] = bit(nb);
      }
      uint32_t ray = 0;
      while (nb >= 0) {
        ray |= bit(nb);
        cr += DR[d];
        cc += DC[d];
        nb = t.rcToIdx(cr, cc);
      }
      t.rayMask[sq][d] = ray;
    }
  }
  return t;
}

const BoardTables& tables() {
  static const BoardTables t = make_tables();
  return t;
}

} // namespace mhf
