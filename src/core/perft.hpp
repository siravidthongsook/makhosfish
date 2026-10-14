#pragma once
#include <cstdint>
#include "core/movegen.hpp"

namespace mhf {

// Node/leaf count over legal move sequences to `depth`. Copy-make: Position is
// tiny, so copying per node is cheaper and safer than undo bookkeeping here.
inline uint64_t perft(const Position& p, const RuleSet& r, int depth) {
  if (depth == 0) return 1;
  Move buf[256];
  int n = generate_moves(p, r, buf, 256);
  if (depth == 1) return uint64_t(n);
  uint64_t total = 0;
  for (int i = 0; i < n; ++i) {
    Position c = p;
    do_move(c, buf[i], r);
    total += perft(c, r, depth - 1);
  }
  return total;
}

} // namespace mhf
