#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include "core/move.hpp"
#include "core/types.hpp"

namespace mhf {

// Shared search bounds (used by tt + search).
inline constexpr int MAX_PLY = 128;
inline constexpr int MATE_SCORE = 30000;
inline constexpr int INF_SCORE = 32000;
inline constexpr int MATE_IN_MAX = MATE_SCORE - MAX_PLY;
inline constexpr int DRAW_SCORE = 0;

enum TTFlag : uint8_t { TT_NONE = 0, TT_EXACT = 1, TT_LOWER = 2, TT_UPPER = 3 };

struct TTEntry {
  uint16_t key16 = 0;         // verify against the high bits of the key
  int16_t  score = 0;
  uint8_t  depth = 0;
  uint8_t  flag = TT_NONE;
  uint8_t  gen = 0;
  uint8_t  pad = 0;
  Move     move{};            // best move
  Move     move2{};           // secondary (previous best / alternate)
};

// Cluster-of-4 bucketed transposition table with generation aging (spec §7).
class TTable {
 public:
  static constexpr int CLUSTER = 4;

  void resize_mb(size_t mb);
  void clear();
  void new_generation();
  void set_ply(int ply) { ply_ = ply; }

  // Sets `move` when the key matches; returns true only on an alpha/beta cutoff.
  bool probe(uint64_t key, int depth, int alpha, int beta, int& score, Move& move);
  void store(uint64_t key, int depth, int score, uint8_t flag, const Move& move,
             const Move& move2 = Move{});

  size_t bytes() const { return table_.size() * sizeof(Cluster); }

 private:
  struct Cluster { TTEntry e[CLUSTER]; };

  std::vector<Cluster> table_;
  size_t index_mask_ = 0;
  uint8_t gen_ = 0;
  int ply_ = 0;
};

} // namespace mhf
