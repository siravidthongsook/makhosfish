#pragma once
#include <cstdint>
#include "core/types.hpp"

namespace mhf {

// Zobrist keys for Thai checkers. Four piece types indexed by
// {white-man, white-king, black-man, black-king}.
struct Zobrist {
  uint64_t piece[4][NB_SQUARES];
  uint64_t stm;
};

// Piece-type index used with Zobrist::piece.
enum PieceType : int { WM = 0, WK = 1, BM = 2, BK = 3 };

const Zobrist& zobrist();

} // namespace mhf
