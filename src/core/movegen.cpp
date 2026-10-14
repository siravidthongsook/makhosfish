#include "core/movegen.hpp"
#include "core/bitboard.hpp"

namespace mhf {
namespace {

struct GenCtx {
  const RuleSet* rules;
  Move* out;
  int   cap;
  int   n = 0;
  void emit(const Move& m) { if (n < cap) out[n++] = m; }
};

inline bool onPromoRank(Color us, int sq) {
  int row = tables().rowOf(sq);
  return us == BLACK ? row == 7 : row == 0;
}

// Forward directions for a man of color `us` (NW,NE for White; SW,SE for Black).
inline void forwardDirs(Color us, int dirs[2]) {
  if (us == WHITE) { dirs[0] = NW; dirs[1] = NE; }
  else             { dirs[0] = SW; dirs[1] = SE; }
}

void gen_man_caps(GenCtx& g, uint32_t occ, uint32_t enemy, uint32_t enemyKings,
                  int orig, int cur, uint32_t capMask, uint32_t capKingMask,
                  uint32_t visited, Color us) {
  const BoardTables& T = tables();
  bool extended = false;
  int dirs[2];
  forwardDirs(us, dirs);
  for (int di = 0; di < 2; ++di) {
    int d = dirs[di];
    int mid = T.neighbor[cur][d];
    if (mid < 0 || !(enemy & bit(mid))) continue;
    int land = T.neighbor[mid][d];
    if (land < 0 || (occ & bit(land)) || (visited & bit(land))) continue;

    extended = true;
    const bool midIsKing = (enemyKings & bit(mid)) != 0;
    uint32_t nOcc = (occ & ~(bit(cur) | bit(mid))) | bit(land);
    gen_man_caps(g, nOcc, enemy & ~bit(mid), enemyKings & ~bit(mid),
                 orig, land, capMask | bit(mid),
                 capKingMask | (midIsKing ? bit(mid) : 0u),
                 visited | bit(land), us);
  }
  if (capMask && (!g.rules->force_continue || !extended)) {
    Move m;
    m.from = uint8_t(orig);
    m.to = uint8_t(cur);
    m.captured = capMask;
    m.capturedKings = capKingMask;
    m.n_captured = uint8_t(popcount(capMask));
    m.promotes = onPromoRank(us, cur);
    g.emit(m);
  }
}

void gen_king_caps(GenCtx& g, uint32_t occ, uint32_t enemy, uint32_t enemyKings,
                   int orig, int cur, uint32_t capMask, uint32_t capKingMask,
                   uint32_t visited) {
  const BoardTables& T = tables();
  const bool flying = g.rules->flying_kings;
  bool extended = false;

  auto captureWith = [&](int blocker, int land) {
    extended = true;
    const bool midIsKing = (enemyKings & bit(blocker)) != 0;
    uint32_t nOcc = (occ & ~(bit(cur) | bit(blocker))) | bit(land);
    gen_king_caps(g, nOcc, enemy & ~bit(blocker), enemyKings & ~bit(blocker),
                  orig, land, capMask | bit(blocker),
                  capKingMask | (midIsKing ? bit(blocker) : 0u),
                  visited | bit(land));
  };

  for (int d = 0; d < 4; ++d) {
    int p = cur, blocker = -1;
    for (;;) {
      int nx = T.neighbor[p][d];
      if (nx < 0) break;
      if (occ & bit(nx)) { blocker = nx; break; }
      p = nx;
    }
    if (blocker < 0 || !(enemy & bit(blocker))) continue;
    if (!flying && T.neighbor[cur][d] != blocker) continue;  // non-flying: adjacent only

    if (g.rules->king_must_land_immediately_behind) {
      int land = T.neighbor[blocker][d];
      if (land >= 0 && !(occ & bit(land)) && !(visited & bit(land)))
        captureWith(blocker, land);
    } else {
      // Free landing: any empty square along the ray beyond the captured piece.
      int p2 = blocker;
      for (;;) {
        int land = T.neighbor[p2][d];
        if (land < 0 || (occ & bit(land))) break;
        if (!(visited & bit(land))) captureWith(blocker, land);
        p2 = land;
      }
    }
  }
  if (capMask && (!g.rules->force_continue || !extended)) {
    Move m;
    m.from = uint8_t(orig);
    m.to = uint8_t(cur);
    m.captured = capMask;
    m.capturedKings = capKingMask;
    m.n_captured = uint8_t(popcount(capMask));
    m.promotes = false;   // kings never promote
    g.emit(m);
  }
}

int gen_all_captures(const Position& p, const RuleSet& r, Move* out, int cap) {
  GenCtx g{&r, out, cap};
  const Color us = p.side();
  const Color them = opposite(us);
  const uint32_t occ = p.occupied();
  const uint32_t enemy = p.pieces(them);
  const uint32_t enemyKings = p.kings(them);

  uint32_t m = p.men(us);
  while (m) { int s = lsb(m); m &= m - 1;
    gen_man_caps(g, occ, enemy, enemyKings, s, s, 0, 0, bit(s), us);
  }
  uint32_t k = p.kings(us);
  while (k) { int s = lsb(k); k &= k - 1;
    gen_king_caps(g, occ, enemy, enemyKings, s, s, 0, 0, bit(s));
  }
  return g.n;
}

int gen_all_quiets(const Position& p, const RuleSet& r, Move* out, int cap) {
  GenCtx g{&r, out, cap};
  const BoardTables& T = tables();
  const Color us = p.side();
  const uint32_t occ = p.occupied();
  const uint32_t empty = ~occ & 0xFFFFFFFFu;

  uint32_t m = p.men(us);
  while (m) { int s = lsb(m); m &= m - 1;
    int dirs[2]; forwardDirs(us, dirs);
    for (int di = 0; di < 2; ++di) {
      int d = dirs[di];
      uint32_t sm = T.stepMask[s][d];
      if (sm && (empty & sm)) {
        Move mv; mv.from = uint8_t(s); mv.to = uint8_t(lsb(sm));
        mv.promotes = onPromoRank(us, mv.to);
        g.emit(mv);
      }
    }
  }
  uint32_t k = p.kings(us);
  while (k) { int s = lsb(k); k &= k - 1;
    for (int d = 0; d < 4; ++d) {
      if (r.flying_kings) {
        int p2 = s;
        for (;;) {
          int nx = T.neighbor[p2][d];
          if (nx < 0 || !(empty & bit(nx))) break;
          Move mv; mv.from = uint8_t(s); mv.to = uint8_t(nx);
          g.emit(mv);
          p2 = nx;
        }
      } else {
        uint32_t sm = T.stepMask[s][d];
        if (sm && (empty & sm)) {
          Move mv; mv.from = uint8_t(s); mv.to = uint8_t(lsb(sm));
          g.emit(mv);
        }
      }
    }
  }
  return g.n;
}

} // namespace

int generate_captures(const Position& p, const RuleSet& r, Move* out, int cap) {
  return gen_all_captures(p, r, out, cap);
}

int generate_moves(const Position& p, const RuleSet& r, Move* out, int cap) {
  int n = gen_all_captures(p, r, out, cap);
  if (n > 0) return n;
  return gen_all_quiets(p, r, out, cap);
}

bool has_legal_move(const Position& p, const RuleSet& r) {
  Move buf[64];
  return generate_moves(p, r, buf, 64) > 0;
}

std::vector<Move> legal_moves(const Position& p, const RuleSet& r) {
  Move buf[256];
  int n = generate_moves(p, r, buf, 256);
  return std::vector<Move>(buf, buf + n);
}

} // namespace mhf
