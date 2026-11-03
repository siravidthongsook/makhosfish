// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Siravid Thongsook and MakHosFish contributors

#include "reference_movegen.hpp"

#include <algorithm>
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

// `dead` marks squares holding a piece already jumped this turn; with deferred
// removal such a piece stays on the board (blocking, not capturable, not landable).
void ref_king_caps(Board bd, uint32_t dead, int cur, int orig, uint32_t cap, uint32_t capK,
                   uint32_t visited, int color, const RuleSet& rules, bool promoted,
                   std::vector<Move>& out);

void ref_man_caps(Board bd, uint32_t dead, int cur, int orig, uint32_t cap, uint32_t capK,
                  uint32_t visited, int color, const RuleSet& rules,
                  std::vector<Move>& out) {
  bool ext = false;
  const int fwd = (color == WHITE) ? -1 : +1;
  const int nDir = rules.men_capture_backward ? 4 : 2;
  const int drs[4] = {fwd, fwd, -fwd, -fwd};
  const int dcs[4] = {-1, +1, -1, +1};
  int row, col; idxToRc(cur, row, col);

  for (int s = 0; s < nDir; ++s) {
    int mid = rcToIdx(row + drs[s], col + dcs[s]);
    if (mid < 0 || bd[mid] == EMPTY || !isEnemy(bd[mid], color) || (dead & bit(mid))) continue;
    int land = rcToIdx(row + 2 * drs[s], col + 2 * dcs[s]);
    if (land < 0 || bd[land] != EMPTY || (visited & bit(land))) continue;

    ext = true;
    Board nb = bd;
    nb[cur] = EMPTY;
    nb[land] = manOf(color);
    if (rules.immediate_removal) nb[mid] = EMPTY;   // else mid stays as a blocker
    const uint32_t ndead = dead | bit(mid);
    const bool mk = isKingPc(bd[mid]);
    const uint32_t ncap = cap | bit(mid);
    const uint32_t ncapK = capK | (mk ? bit(mid) : 0u);
    const uint32_t nvis = visited | bit(land);

    if (onPromo(color, land)) {
      switch (rules.midcapture_promo) {
        case MidCapPromo::Stop: {
          Move m; m.from = uint8_t(orig); m.to = uint8_t(land);
          m.captured = ncap; m.capturedKings = ncapK;
          m.n_captured = uint8_t(popcount(ncap)); m.promotes = true;
          out.push_back(m);
          break;
        }
        case MidCapPromo::ContinueMan:
          ref_man_caps(nb, ndead, land, orig, ncap, ncapK, nvis, color, rules, out);
          break;
        case MidCapPromo::PromoteAndContinue:
          ref_king_caps(nb, ndead, land, orig, ncap, ncapK, nvis, color, rules, true, out);
          break;
      }
    } else {
      ref_man_caps(nb, ndead, land, orig, ncap, ncapK, nvis, color, rules, out);
    }
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

void ref_king_caps(Board bd, uint32_t dead, int cur, int orig, uint32_t cap, uint32_t capK,
                   uint32_t visited, int color, const RuleSet& rules, bool promoted,
                   std::vector<Move>& out) {
  bool ext = false;
  auto capWith = [&](int blocker, int land) {
    ext = true;
    Board nb = bd;
    nb[cur] = EMPTY;
    nb[land] = kingOf(color);
    if (rules.immediate_removal) nb[blocker] = EMPTY;  // else blocker stays
    const uint32_t ndead = dead | bit(blocker);
    const bool mk = isKingPc(bd[blocker]);
    ref_king_caps(nb, ndead, land, orig, cap | bit(blocker),
                  capK | (mk ? bit(blocker) : 0u), visited | bit(land), color, rules,
                  promoted, out);
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
    if (blocker < 0 || bd[blocker] == EMPTY || !isEnemy(bd[blocker], color)) continue;
    if (dead & bit(blocker)) continue;   // already jumped: blocks, cannot be taken again
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
    m.promotes = promoted;
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
      ref_king_caps(bd, 0, sq, sq, 0, 0, bit(sq), color, r, false, out);
    else
      ref_man_caps(bd, 0, sq, sq, 0, 0, bit(sq), color, r, out);
  }
  // Match the fast generator's majority-capture filter.
  if (r.max_capture_required && !out.empty()) {
    int best = 0;
    for (const Move& m : out) best = std::max(best, int(m.n_captured));
    std::vector<Move> keep;
    keep.reserve(out.size());
    for (const Move& m : out)
      if (int(m.n_captured) == best) keep.push_back(m);
    out.swap(keep);
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
