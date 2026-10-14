#include "reference_movegen.hpp"

#include <array>
#include <cstdint>

namespace mhf {
namespace {

enum : int { EMPTY = 0, WMAN = 1, WKING = 2, BMAN = 3, BKING = 4 };
constexpr int DR[4] = {-1, -1, 1, 1};  // NW NE SW SE
constexpr int DC[4] = {-1, 1, -1, 1};

int rcToIdx(int row, int col) {
  if (row < 0 || row > 7 || col < 0 || col > 7) return -1;
  if (((row + col) & 1) != 1) return -1;
  int p = (row % 2 == 0) ? (col - 1) / 2 : col / 2;
  return row * 4 + p;
}
void idxToRc(int sq, int& row, int& col) {
  row = sq / 4;
  int p = sq % 4;
  col = (row % 2 == 0) ? 2 * p + 1 : 2 * p;
}
int neighbor(int sq, int d) {
  int row, col; idxToRc(sq, row, col);
  return rcToIdx(row + DR[d], col + DC[d]);
}

bool isWhite(int pc) { return pc == WMAN || pc == WKING; }
bool isBlack(int pc) { return pc == BMAN || pc == BKING; }
bool isEnemy(int pc, int color) { return color == WHITE ? isBlack(pc) : isWhite(pc); }
bool isKingPc(int pc) { return pc == WKING || pc == BKING; }
int manOf(int color) { return color == WHITE ? WMAN : BMAN; }
int kingOf(int color) { return color == WHITE ? WKING : BKING; }
bool onPromo(int color, int sq) { return color == BLACK ? (sq / 4 == 7) : (sq / 4 == 0); }

using Board = std::array<int, 32>;

Board to_board(const Position& p) {
  Board b{};
  for (int sq = 0; sq < NB_SQUARES; ++sq) {
    if (p.wm & bit(sq)) b[sq] = WMAN;
    else if (p.wk & bit(sq)) b[sq] = WKING;
    else if (p.bm & bit(sq)) b[sq] = BMAN;
    else if (p.bk & bit(sq)) b[sq] = BKING;
  }
  return b;
}

void ref_man_caps(Board bd, int cur, int orig, uint32_t cap, uint32_t capK,
                  uint32_t visited, int color, const RuleSet& rules,
                  std::vector<Move>& out) {
  bool ext = false;
  const int fwd = (color == WHITE) ? -1 : +1;
  for (int s = 0; s < 2; ++s) {
    int row, col; idxToRc(cur, row, col);
    int dr = fwd, dc = (s == 0 ? -1 : +1);
    int mid = rcToIdx(row + dr, col + dc);
    if (mid < 0 || !isEnemy(bd[mid], color)) continue;
    int land = rcToIdx(row + 2 * dr, col + 2 * dc);
    if (land < 0 || bd[land] != EMPTY) continue;
    if (visited & bit(land)) continue;

    ext = true;
    Board nb = bd;
    nb[cur] = EMPTY;
    nb[mid] = EMPTY;
    nb[land] = manOf(color);
    bool mk = isKingPc(bd[mid]);
    ref_man_caps(nb, land, orig, cap | bit(mid),
                 capK | (mk ? bit(mid) : 0u), visited | bit(land), color, rules, out);
  }
  if (cap && (!rules.force_continue || !ext)) {
    Move m;
    m.from = uint8_t(orig); m.to = uint8_t(cur);
    m.captured = cap; m.capturedKings = capK;
    m.n_captured = uint8_t(popcount(cap));
    m.promotes = onPromo(color, cur);
    out.push_back(m);
  }
}

void ref_king_caps(Board bd, int cur, int orig, uint32_t cap, uint32_t capK,
                   uint32_t visited, int color, const RuleSet& rules,
                   std::vector<Move>& out) {
  bool ext = false;
  auto capWith = [&](int blocker, int land) {
    ext = true;
    Board nb = bd;
    nb[cur] = EMPTY;
    nb[blocker] = EMPTY;
    nb[land] = kingOf(color);
    bool mk = isKingPc(bd[blocker]);
    ref_king_caps(nb, land, orig, cap | bit(blocker),
                  capK | (mk ? bit(blocker) : 0u), visited | bit(land), color, rules, out);
  };
  for (int d = 0; d < 4; ++d) {
    int row, col; idxToRc(cur, row, col);
    int pr = row, pc = col, blocker = -1;
    for (;;) {
      pr += DR[d]; pc += DC[d];
      int nx = rcToIdx(pr, pc);
      if (nx < 0) break;
      if (bd[nx] != EMPTY) { blocker = nx; break; }
    }
    if (blocker < 0 || !isEnemy(bd[blocker], color)) continue;
    if (!rules.flying_kings && neighbor(cur, d) != blocker) continue;
    int br, bc; idxToRc(blocker, br, bc);
    if (rules.king_must_land_immediately_behind) {
      int land = rcToIdx(br + DR[d], bc + DC[d]);
      if (land >= 0 && bd[land] == EMPTY && !(visited & bit(land))) capWith(blocker, land);
    } else {
      int lr = br, lc = bc;
      for (;;) {
        lr += DR[d]; lc += DC[d];
        int land = rcToIdx(lr, lc);
        if (land < 0 || bd[land] != EMPTY) break;
        if (!(visited & bit(land))) capWith(blocker, land);
      }
    }
  }
  if (cap && (!rules.force_continue || !ext)) {
    Move m;
    m.from = uint8_t(orig); m.to = uint8_t(cur);
    m.captured = cap; m.capturedKings = capK;
    m.n_captured = uint8_t(popcount(cap));
    m.promotes = false;
    out.push_back(m);
  }
}

void ref_quiets(Board bd, int color, const RuleSet& rules, std::vector<Move>& out) {
  for (int sq = 0; sq < NB_SQUARES; ++sq) {
    int pc = bd[sq];
    if (pc == EMPTY) continue;
    if (color == WHITE && !isWhite(pc)) continue;
    if (color == BLACK && !isBlack(pc)) continue;

    if (!isKingPc(pc)) {
      int row, col; idxToRc(sq, row, col);
      int fwd = (color == WHITE) ? -1 : +1;
      for (int s = 0; s < 2; ++s) {
        int land = rcToIdx(row + fwd, col + (s == 0 ? -1 : +1));
        if (land < 0 || bd[land] != EMPTY) continue;
        Move m; m.from = uint8_t(sq); m.to = uint8_t(land);
        m.promotes = onPromo(color, land);
        out.push_back(m);
      }
    } else {
      for (int d = 0; d < 4; ++d) {
        if (rules.flying_kings) {
          int p2 = sq;
          for (;;) {
            int nx = neighbor(p2, d);
            if (nx < 0 || bd[nx] != EMPTY) break;
            Move m; m.from = uint8_t(sq); m.to = uint8_t(nx);
            out.push_back(m);
            p2 = nx;
          }
        } else {
          int nx = neighbor(sq, d);
          if (nx >= 0 && bd[nx] == EMPTY) {
            Move m; m.from = uint8_t(sq); m.to = uint8_t(nx);
            out.push_back(m);
          }
        }
      }
    }
  }
}

} // namespace

std::vector<Move> ref_generate_captures(const Position& p, const RuleSet& r) {
  Board bd = to_board(p);
  std::vector<Move> out;
  const int color = p.stm;
  for (int sq = 0; sq < NB_SQUARES; ++sq) {
    int pc = bd[sq];
    if (pc == EMPTY) continue;
    if (color == WHITE && !isWhite(pc)) continue;
    if (color == BLACK && !isBlack(pc)) continue;
    if (isKingPc(pc))
      ref_king_caps(bd, sq, sq, 0, 0, bit(sq), color, r, out);
    else
      ref_man_caps(bd, sq, sq, 0, 0, bit(sq), color, r, out);
  }
  return out;
}

std::vector<Move> ref_generate_moves(const Position& p, const RuleSet& r) {
  std::vector<Move> caps = ref_generate_captures(p, r);
  if (!caps.empty()) return caps;
  std::vector<Move> out;
  ref_quiets(to_board(p), p.stm, r, out);
  return out;
}

uint64_t ref_perft(const Position& p, const RuleSet& r, int depth) {
  if (depth == 0) return 1;
  auto moves = ref_generate_moves(p, r);
  if (depth == 1) return moves.size();
  uint64_t total = 0;
  for (const Move& m : moves) {
    Position c = p;
    do_move(c, m, r);
    total += ref_perft(c, r, depth - 1);
  }
  return total;
}

} // namespace mhf
