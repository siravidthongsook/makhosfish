#pragma once
#include "core/move.hpp"
#include "core/position.hpp"
#include "core/rules.hpp"

namespace mhf {

// Stage E0 eval: material + piece-square tables + tempo (spec §8).
inline constexpr int MAN_VALUE = 100;
inline constexpr int KING_VALUE = 300;
inline constexpr int TEMPO = 10;

// Centipawns from the side-to-move's perspective (spec §7/§8).
int evaluate(const Position& p, const RuleSet& r);

// Piece-square value for color `c` on square `sq` (white-relative geometry).
int pst_of(Color c, bool king, int sq);

// PST change for the moving piece only; cheap quiet-move ordering signal.
int move_pst_delta(const Position& p, const Move& m, const RuleSet& r);

} // namespace mhf
