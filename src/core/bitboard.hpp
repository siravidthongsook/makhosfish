#pragma once
#include "core/types.hpp"

namespace mhf {

// Diagonal directions, fixed order: 0=NW 1=NE 2=SW 3=SE
// (dr,dc): NW=(-1,-1) NE=(-1,+1) SW=(+1,-1) SE=(+1,+1)
enum Dir { NW = 0, NE = 1, SW = 2, SE = 3 };

struct BoardTables {
  int      sqRow[NB_SQUARES];
  int      sqCol[NB_SQUARES];
  int      neighbor[NB_SQUARES][4];  // target square or -1
  uint32_t stepMask[NB_SQUARES][4];  // single-step neighbor as a bitmask (0 if none)
  uint32_t rayMask[NB_SQUARES][4];   // all squares along the diagonal (excludes start)

  int rcToIdx(int r, int c) const;   // -1 if off-board / light square
  int rowOf(int sq) const { return sqRow[sq]; }
  int colOf(int sq) const { return sqCol[sq]; }
};

const BoardTables& tables();

inline bool isDark(int r, int c) { return ((r + c) & 1) == 1; }

} // namespace mhf
