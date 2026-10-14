#include "core/movegen.hpp"
#include "core/bitboard.hpp"

#include <algorithm>
#include <cstring>

namespace mhf {
namespace {

struct GenCtx {
  const RuleSet* rules;
  Move* out;
  int   cap;
  int   n = 0;

  // Optional landing-path recording (cold path: notation only). When `pathBuf`
  // is set, `curPath` holds the chain [from, ...landings...] down the recursion.
  uint8_t* pathBuf = nullptr;
  uint8_t  curPath[MAX_MOVE_PATH]{};
  int      curLen = 0;

  GenCtx(const RuleSet* rs, Move* o, int c) : rules(rs), out(o), cap(c) {}

  void push(int sq) { if (curLen < MAX_MOVE_PATH - 1) curPath[curLen++] = uint8_t(sq); }
  void pop() { if (curLen > 0) --curLen; }
  void resetPath(int from) { curPath[0] = uint8_t(from); curLen = 1; }

  void emit(const Move& m) {
    if (n >= cap) return;
    if (pathBuf) {
      uint8_t* dst = pathBuf + size_t(n) * MAX_MOVE_PATH;
      dst[0] = uint8_t(curLen);
      for (int i = 0; i < curLen; ++i) dst[1 + i] = curPath[i];
    }
    out[n++] = m;
  }
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

void emitCapture(GenCtx& g, int orig, int to, uint32_t cap, uint32_t capK, bool promotes) {
  Move m;
  m.from = uint8_t(orig);
  m.to = uint8_t(to);
  m.captured = cap;
  m.capturedKings = capK;
  m.n_captured = uint8_t(popcount(cap));
  m.promotes = promotes;
  g.emit(m);
}

// Apply one jump of `blocker` from `cur`, landing on `land`.
// immediate removal: the blocker leaves the board, which can open a later jump.
// deferred removal (International/Russian/Brazilian): the blocker stays on the
// board until the move ends, so it blocks and cannot be jumped or landed on.
inline void applyJump(const RuleSet& r, uint32_t& occ, uint32_t& enemy,
                      int cur, int blocker, int land) {
  if (r.immediate_removal) occ = (occ & ~(bit(cur) | bit(blocker))) | bit(land);
  else                     occ = (occ & ~bit(cur)) | bit(land);  // blocker remains
  enemy &= ~bit(blocker);
}

void gen_king_caps(GenCtx& g, uint32_t occ, uint32_t enemy, uint32_t enemyKings,
                   int orig, int cur, uint32_t capMask, uint32_t capKingMask,
                   uint32_t visited, bool promoted);

// Man capture chains. `promoted` is always false here (a promoted piece is a
// king), so the move result is a king only if it ends on the promotion rank.
void gen_man_caps(GenCtx& g, uint32_t occ, uint32_t enemy, uint32_t enemyKings,
                  int orig, int cur, uint32_t capMask, uint32_t capKingMask,
                  uint32_t visited, Color us) {
  const BoardTables& T = tables();
  const RuleSet& r = *g.rules;
  bool extended = false;

  int dirs[4];
  forwardDirs(us, dirs);
  int nd = 2;
  if (r.men_capture_backward) {
    dirs[2] = (us == WHITE) ? SW : NW;
    dirs[3] = (us == WHITE) ? SE : NE;
    nd = 4;
  }

  for (int di = 0; di < nd; ++di) {
    int d = dirs[di];
    int mid = T.neighbor[cur][d];
    if (mid < 0 || !(enemy & bit(mid))) continue;
    int land = T.neighbor[mid][d];
    if (land < 0 || (occ & bit(land)) || (visited & bit(land))) continue;

    extended = true;
    const bool midIsKing = (enemyKings & bit(mid)) != 0;
    uint32_t nOcc = occ, nEnemy = enemy;
    applyJump(r, nOcc, nEnemy, cur, mid, land);
    const uint32_t nEnemyKings = enemyKings & ~bit(mid);
    const uint32_t nCap = capMask | bit(mid);
    const uint32_t nCapK = capKingMask | (midIsKing ? bit(mid) : 0u);
    const uint32_t nVis = visited | bit(land);

    g.push(land);
    if (onPromoRank(us, land)) {
      switch (r.midcapture_promo) {
        case MidCapPromo::Stop:
          emitCapture(g, orig, land, nCap, nCapK, true);  // crowned; move ends here
          break;
        case MidCapPromo::ContinueMan:
          gen_man_caps(g, nOcc, nEnemy, nEnemyKings, orig, land, nCap, nCapK, nVis, us);
          break;
        case MidCapPromo::PromoteAndContinue:
          gen_king_caps(g, nOcc, nEnemy, nEnemyKings, orig, land, nCap, nCapK, nVis, true);
          break;
      }
    } else {
      gen_man_caps(g, nOcc, nEnemy, nEnemyKings, orig, land, nCap, nCapK, nVis, us);
    }
    g.pop();
  }
  if (capMask && (!r.force_continue || !extended))
    emitCapture(g, orig, cur, capMask, capKingMask, onPromoRank(us, cur));
}

void gen_king_caps(GenCtx& g, uint32_t occ, uint32_t enemy, uint32_t enemyKings,
                   int orig, int cur, uint32_t capMask, uint32_t capKingMask,
                   uint32_t visited, bool promoted) {
  const BoardTables& T = tables();
  const RuleSet& r = *g.rules;
  const bool flying = r.flying_kings;
  bool extended = false;

  auto captureWith = [&](int blocker, int land) {
    extended = true;
    const bool midIsKing = (enemyKings & bit(blocker)) != 0;
    uint32_t nOcc = occ, nEnemy = enemy;
    applyJump(r, nOcc, nEnemy, cur, blocker, land);
    g.push(land);
    gen_king_caps(g, nOcc, nEnemy, enemyKings & ~bit(blocker),
                  orig, land, capMask | bit(blocker),
                  capKingMask | (midIsKing ? bit(blocker) : 0u),
                  visited | bit(land), promoted);
    g.pop();
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

    if (r.king_must_land_immediately_behind) {
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
  if (capMask && (!r.force_continue || !extended))
    emitCapture(g, orig, cur, capMask, capKingMask, promoted);
}

int gen_captures(GenCtx& g, const Position& p, const RuleSet& r) {
  const Color us = p.side();
  const Color them = opposite(us);
  const uint32_t occ = p.occupied();
  const uint32_t enemy = p.pieces(them);
  const uint32_t enemyKings = p.kings(them);

  uint32_t m = p.men(us);
  while (m) { int s = lsb(m); m &= m - 1;
    g.resetPath(s);
    gen_man_caps(g, occ, enemy, enemyKings, s, s, 0, 0, bit(s), us);
  }
  uint32_t k = p.kings(us);
  while (k) { int s = lsb(k); k &= k - 1;
    g.resetPath(s);
    gen_king_caps(g, occ, enemy, enemyKings, s, s, 0, 0, bit(s), false);
  }

  // Majority capture: keep only the sequences taking the most pieces.
  if (r.max_capture_required && g.n > 0) {
    int best = 0;
    for (int i = 0; i < g.n; ++i) best = std::max(best, int(g.out[i].n_captured));
    int w = 0;
    for (int i = 0; i < g.n; ++i) {
      if (int(g.out[i].n_captured) != best) continue;
      g.out[w] = g.out[i];
      if (g.pathBuf)
        std::memcpy(g.pathBuf + size_t(w) * MAX_MOVE_PATH,
                    g.pathBuf + size_t(i) * MAX_MOVE_PATH, MAX_MOVE_PATH);
      ++w;
    }
    g.n = w;
  }
  return g.n;
}

int gen_quiets(GenCtx& g, const Position& p, const RuleSet& r) {
  const BoardTables& T = tables();
  const Color us = p.side();
  const uint32_t occ = p.occupied();
  const uint32_t empty = ~occ & 0xFFFFFFFFu;

  auto emitQ = [&](int from, int to, bool promo) {
    g.curPath[0] = uint8_t(from);
    g.curPath[1] = uint8_t(to);
    g.curLen = 2;
    Move mv; mv.from = uint8_t(from); mv.to = uint8_t(to); mv.promotes = promo;
    g.emit(mv);
  };

  uint32_t m = p.men(us);
  while (m) { int s = lsb(m); m &= m - 1;
    int dirs[2]; forwardDirs(us, dirs);
    for (int di = 0; di < 2; ++di) {
      int d = dirs[di];
      uint32_t sm = T.stepMask[s][d];
      if (sm && (empty & sm)) emitQ(s, lsb(sm), onPromoRank(us, lsb(sm)));
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
          emitQ(s, nx, false);
          p2 = nx;
        }
      } else {
        uint32_t sm = T.stepMask[s][d];
        if (sm && (empty & sm)) emitQ(s, lsb(sm), false);
      }
    }
  }
  return g.n;
}

} // namespace

int generate_captures(const Position& p, const RuleSet& r, Move* out, int cap) {
  GenCtx g{&r, out, cap};
  return gen_captures(g, p, r);
}

int generate_moves(const Position& p, const RuleSet& r, Move* out, int cap) {
  GenCtx g{&r, out, cap};
  int n = gen_captures(g, p, r);
  if (n > 0) return n;
  return gen_quiets(g, p, r);
}

int generate_moves_with_paths(const Position& p, const RuleSet& r, Move* out,
                              uint8_t* paths, int cap) {
  GenCtx g{&r, out, cap};
  g.pathBuf = paths;
  int n = gen_captures(g, p, r);
  if (n > 0) return n;
  return gen_quiets(g, p, r);
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
