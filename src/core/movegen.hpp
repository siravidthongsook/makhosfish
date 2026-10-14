#pragma once
#include <vector>
#include "core/position.hpp"
#include "core/rules.hpp"
#include "core/move.hpp"

namespace mhf {

// Moves are pseudo-legal == legal (draughts has no check/pins). When any
// capture exists, only captures are generated (compulsory capture).
int generate_moves(const Position& p, const RuleSet& r, Move* out, int cap);
int generate_captures(const Position& p, const RuleSet& r, Move* out, int cap);
bool has_legal_move(const Position& p, const RuleSet& r);

std::vector<Move> legal_moves(const Position& p, const RuleSet& r = RuleSet{});

} // namespace mhf
