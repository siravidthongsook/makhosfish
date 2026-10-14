#pragma once
#include <vector>
#include "core/position.hpp"
#include "core/rules.hpp"
#include "core/move.hpp"

namespace mhf {

// Upper bound on the squares in a move's landing chain (including `from` and
// the final `to`). A 32-square board with forced, non-repeating jumps fits well
// inside this; used to size the optional path buffers below.
inline constexpr int MAX_MOVE_PATH = 20;

// Moves are pseudo-legal == legal (draughts has no check/pins). When any
// capture exists, only captures are generated (compulsory capture).
int generate_moves(const Position& p, const RuleSet& r, Move* out, int cap);
int generate_captures(const Position& p, const RuleSet& r, Move* out, int cap);

// Same as generate_moves, but also records each move's landing chain so the
// full multi-jump notation ("32x23x14") can be produced and parsed. For move i,
// `paths[i*MAX_MOVE_PATH]` holds the chain length L (always >= 2), and
// `paths[i*MAX_MOVE_PATH + 1 .. + L]` the squares (`from`, intermediate
// landings, final `to`), internal 0..31. `paths` may be null (paths ignored).
int generate_moves_with_paths(const Position& p, const RuleSet& r, Move* out,
                              uint8_t* paths, int cap);

bool has_legal_move(const Position& p, const RuleSet& r);

std::vector<Move> legal_moves(const Position& p, const RuleSet& r = RuleSet{});

} // namespace mhf
