// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Siravid Thongsook and MakHosFish contributors

#pragma once
#include "core/types.hpp"

namespace mhf {

enum class MidCapPromo : uint8_t {
  Stop,                 // turn ends on reaching the back rank
  ContinueMan,          // continue the chain as a man; promote only if it ends there (Thai)
  PromoteAndContinue,   // become a king immediately and continue
};

// Rule configuration. Thai defaults are the members' initializers.
struct RuleSet {
  uint8_t  board_size = 8;
  uint8_t  rows_of_pieces = 2;         // each side fills this many nearest rows

  bool men_capture_backward = false;   // Thai: forward only
  bool flying_kings = true;            // Thai: yes
  bool king_must_land_immediately_behind = true;  // Thai: yes
  bool immediate_removal = true;       // Thai: pieces removed during the sequence
  bool max_capture_required = false;   // Thai: free choice
  bool force_continue = true;          // must keep capturing while possible

  MidCapPromo midcapture_promo = MidCapPromo::ContinueMan;

  Color first_move = WHITE;

  // Draw rules (engine-side)
  bool     draw_repetition = true;
  uint16_t draw_no_progress_plies = 50;  // 25 moves each side, no man move/capture
};

inline RuleSet thai_rules() { return RuleSet{}; }

// English/American checkers, 8x8, 12 men on 3 rows.
// Men capture FORWARD ONLY (a king gains backward capture); kings are "short"
// (one diagonal step). Free choice among capture lines. Reaching the crown row
// ends the move at once. Captured pieces are removed immediately.
// Sources: WCDF/ACF official rules; Rein Halbersma's perft table
// (damforum.nl/viewtopic.php?t=2822), cross-checked by Ed Gilbert (Kingsrow).
inline RuleSet english_rules() {
  RuleSet r;
  r.rows_of_pieces = 3;
  r.men_capture_backward = false;   // men jump forward only
  r.flying_kings = false;           // short kings: one step each diagonal
  r.king_must_land_immediately_behind = true;
  r.immediate_removal = true;       // jumped piece leaves the board at once
  r.max_capture_required = false;   // no majority rule
  r.force_continue = true;          // a started chain must be completed
  r.midcapture_promo = MidCapPromo::Stop;  // crowning ends the move
  r.first_move = WHITE;
  return r;
}

// Russian draughts / shashki, 8x8, 12 men on 3 rows.
// Flying kings with free landing, men capture backward, NO majority rule, and a
// captured piece stays on the board (as a blocker) until the move ends. A man
// crowned mid-chain becomes a king at once and keeps capturing as a king.
// Source: FMJD "Official Rules of the game in draughts-64" §4.13-4.15.
inline RuleSet russian_rules() {
  RuleSet r;
  r.rows_of_pieces = 3;
  r.men_capture_backward = true;    // men capture forward and backward
  r.flying_kings = true;            // flying kings, free landing
  r.king_must_land_immediately_behind = false;
  r.immediate_removal = false;      // deferred removal (dead pieces block)
  r.max_capture_required = false;   // §4.13: free choice, no majority rule
  r.force_continue = true;
  r.midcapture_promo = MidCapPromo::PromoteAndContinue;  // §4.14: crowned, continues as king
  r.first_move = WHITE;
  return r;
}

// Brazilian draughts: international rules compressed to 8x8, 12 men on 3 rows.
// Flying kings with free landing, backward man captures, MAJORITY capture
// required, deferred removal. A man crossing the last row mid-chain stays a man
// and is crowned only if the move ends there.
// Source: FMJD draughts-64 rules §5 (Brazilian differences); Wikipedia.
inline RuleSet brazilian_rules() {
  RuleSet r;
  r.rows_of_pieces = 3;
  r.men_capture_backward = true;    // men capture forward and backward
  r.flying_kings = true;            // flying kings, free landing
  r.king_must_land_immediately_behind = false;
  r.immediate_removal = false;      // deferred removal (dead pieces block)
  r.max_capture_required = true;    // §5.1: majority capture is obligatory
  r.force_continue = true;
  r.midcapture_promo = MidCapPromo::ContinueMan;  // §5.2: stays a man unless it stops
  r.first_move = WHITE;
  return r;
}

} // namespace mhf
