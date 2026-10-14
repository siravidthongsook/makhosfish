#pragma once
#include <cstdint>
#include <vector>
#include "core/position.hpp"

namespace mhf {

// Outcome of a position from the side-to-move's perspective (the standard
// bitbase convention). `Unknown` only appears mid-computation.
enum class Wdl : uint8_t { Unknown = 0, Win = 1, Loss = 2, Draw = 3 };

const char* wdl_name(Wdl v);

// Combinatorial indexing of Thai positions by piece class. The four classes are
// ordered (WM, WK, BM, BK); positions with exactly k pieces are packed into a
// dense per-k table with one slot per (placement, side to move). encode/decode
// are inverse bijections over the whole legal placement space (including
// positions unreachable from the start, which the solver still evaluates).
//
// Slot = comp.offset + placement_index*2 + (stm == Black). The placement index
// is a mixed-radix number: class t contributes a colex-rank digit in
// [0, C(remaining, n_t)) computed over the squares not yet used by earlier
// classes, which is exactly the count of distinct placements.
struct EgdbLayout {
  struct Comp {
    uint8_t  n[4] = {0, 0, 0, 0};
    uint64_t offset = 0;
    uint64_t placements = 0;   // placement count (excluding the x2 side-to-move)
  };

  int max_pieces = 0;
  std::vector<std::vector<Comp>> comps;  // comps[k]: compositions with k pieces
  std::vector<uint64_t> k_size;          // k_size[k] = slots in the k-table

  void build(int kmax);

  // Flat slot of `p`, or -1 if p is empty or has more than max_pieces pieces.
  // `k_out` always receives p's piece count.
  int64_t encode(const Position& p, int& k_out) const;

  // Inverse of encode. `hash` is left 0 (callers that need it recompute).
  Position decode(int k, uint64_t slot) const;
};

} // namespace mhf
