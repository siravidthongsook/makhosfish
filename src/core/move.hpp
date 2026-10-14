#pragma once
#include "core/types.hpp"

namespace mhf {

// A complete (possibly multi-jump) move. For Thai rules a capture move is a
// full, forced capture sequence (§3.4); there is no early stop.
struct Move {
  uint8_t  from = 0xFF;   // origin square 0..31 (0xFF = null move)
  uint8_t  to = 0xFF;     // final landing square
  uint8_t  n_captured = 0;
  bool     promotes = false;
  uint32_t captured = 0;        // mask of all removed squares
  uint32_t capturedKings = 0;   // subset of `captured` that were kings

  bool is_null() const { return from == 0xFF; }
  bool is_capture() const { return n_captured != 0; }

  friend bool operator==(const Move& a, const Move& b) {
    return a.from == b.from && a.to == b.to &&
           a.captured == b.captured && a.capturedKings == b.capturedKings &&
           a.promotes == b.promotes && a.n_captured == b.n_captured;
  }
};

} // namespace mhf
