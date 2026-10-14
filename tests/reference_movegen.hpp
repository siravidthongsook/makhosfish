#pragma once
#include <vector>
#include "core/position.hpp"
#include "core/rules.hpp"
#include "core/move.hpp"

namespace mhf {

// Deliberately naive, independent reference move generator used only to validate
// the fast bitboard generator (spec §14, §20.5). It shares no geometry tables
// with core/movegen and works on a plain 32-cell array.
std::vector<Move> ref_generate_moves(const Position& p, const RuleSet& r);
std::vector<Move> ref_generate_captures(const Position& p, const RuleSet& r);
uint64_t ref_perft(const Position& p, const RuleSet& r, int depth);

} // namespace mhf
