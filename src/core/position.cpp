// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Siravid Thongsook and MakHosFish contributors

#include "core/position.hpp"
#include "core/zobrist.hpp"

namespace mhf {

static inline int manType(Color c) { return c == WHITE ? WM : BM; }
static inline int kingType(Color c) { return c == WHITE ? WK : BK; }

uint64_t recompute_hash(const Position& p) {
  const Zobrist& z = zobrist();
  uint64_t h = 0;
  uint32_t b;
  b = p.wm; while (b) { h ^= z.piece[WM][lsb(b)]; b &= b - 1; }
  b = p.wk; while (b) { h ^= z.piece[WK][lsb(b)]; b &= b - 1; }
  b = p.bm; while (b) { h ^= z.piece[BM][lsb(b)]; b &= b - 1; }
  b = p.bk; while (b) { h ^= z.piece[BK][lsb(b)]; b &= b - 1; }
  if (p.stm == BLACK) h ^= z.stm;
  return h;
}

Position start_position(const RuleSet& r) {
  Position p;
  // Each side fills its nearest `rows_of_pieces` rows, 4 dark squares per row
  // (internal row*4..row*4+3). Thai (2 rows) => 8 men; variants (3 rows) => 12.
  const int rows = r.rows_of_pieces;
  const uint32_t low = (rows >= 8) ? 0xFFFFFFFFu : ((1u << (4 * rows)) - 1u);
  p.bm = low;                                        // black back rows (0..rows-1)
  p.wm = low << (32 - 4 * rows);                     // white back rows (8-rows..7)
  p.stm = r.first_move;
  p.game_ply = 0;
  p.rule50 = 0;
  p.hash = recompute_hash(p);
  return p;
}

void do_move(Position& p, const Move& m, const RuleSet& r) {
  (void)r;  // Thai immediate removal: captured mask fully describes removals
  const Zobrist& z = zobrist();
  Color us = p.side();
  Color them = opposite(us);

  const uint32_t fromBit = bit(m.from), toBit = bit(m.to);
  const uint32_t wasKing = p.kings(us) & fromBit;

  // remove mover from origin
  if (us == WHITE) { p.wm &= ~fromBit; p.wk &= ~fromBit; }
  else             { p.bm &= ~fromBit; p.bk &= ~fromBit; }
  p.hash ^= z.piece[wasKing ? kingType(us) : manType(us)][m.from];

  // remove captured enemies
  const uint32_t capMen = m.captured & ~m.capturedKings;
  const uint32_t capKl  = m.captured &  m.capturedKings;
  if (them == WHITE) { p.wm &= ~capMen; p.wk &= ~capKl; }
  else               { p.bm &= ~capMen; p.bk &= ~capKl; }
  {
    uint32_t b = capMen;
    while (b) { int s = lsb(b); p.hash ^= z.piece[manType(them)][s]; b &= b - 1; }
    b = capKl;
    while (b) { int s = lsb(b); p.hash ^= z.piece[kingType(them)][s]; b &= b - 1; }
  }

  // place mover at destination
  const bool becomesKing = (wasKing != 0) || m.promotes;
  if (us == WHITE) { if (becomesKing) p.wk |= toBit; else p.wm |= toBit; }
  else             { if (becomesKing) p.bk |= toBit; else p.bm |= toBit; }
  p.hash ^= z.piece[becomesKing ? kingType(us) : manType(us)][m.to];

  // progress counter: a king quiet move does not reset it
  const bool manMove = (wasKing == 0);
  if (m.is_capture() || manMove) p.rule50 = 0;
  else p.rule50++;

  p.stm = them;
  p.hash ^= z.stm;
  p.game_ply++;
}

void undo_move(Position& p, const Move& m, uint16_t prev_rule50, const RuleSet& r) {
  (void)r;
  const Zobrist& z = zobrist();
  Color mover = opposite(p.side());   // side that made the move
  Color enemy = opposite(mover);

  const uint32_t fromBit = bit(m.from), toBit = bit(m.to);
  bool becomesKingNow = (p.kings(mover) & toBit) != 0;
  bool originalKing = (!m.promotes) && becomesKingNow;

  // remove mover from `to`
  if (mover == WHITE) { p.wm &= ~toBit; p.wk &= ~toBit; }
  else                { p.bm &= ~toBit; p.bk &= ~toBit; }
  p.hash ^= z.piece[becomesKingNow ? kingType(mover) : manType(mover)][m.to];

  // restore mover at `from`
  if (mover == WHITE) { if (originalKing) p.wk |= fromBit; else p.wm |= fromBit; }
  else                { if (originalKing) p.bk |= fromBit; else p.bm |= fromBit; }
  p.hash ^= z.piece[originalKing ? kingType(mover) : manType(mover)][m.from];

  // restore captured enemies
  const uint32_t capMen = m.captured & ~m.capturedKings;
  const uint32_t capKl  = m.captured &  m.capturedKings;
  if (enemy == WHITE) { p.wm |= capMen; p.wk |= capKl; }
  else                { p.bm |= capMen; p.bk |= capKl; }
  {
    uint32_t b = capMen;
    while (b) { int s = lsb(b); p.hash ^= z.piece[manType(enemy)][s]; b &= b - 1; }
    b = capKl;
    while (b) { int s = lsb(b); p.hash ^= z.piece[kingType(enemy)][s]; b &= b - 1; }
  }

  p.stm = mover;
  p.hash ^= z.stm;
  p.rule50 = prev_rule50;
  p.game_ply--;
}

} // namespace mhf
