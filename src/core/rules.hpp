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

} // namespace mhf
