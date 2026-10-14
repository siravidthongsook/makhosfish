#pragma once
#include <string>
#include "core/position.hpp"
#include "core/rules.hpp"

namespace mhf {

// Frozen FEN format (spec §11.0): "<stm>:W<list>:B<list>[:<rule50>]"
//   list = comma-separated squares (1..32), kings prefixed 'K', ranges "a-b".
//   example: W:W24-31:B0-7
bool parse_fen(const std::string& s, Position& p, const RuleSet& r = RuleSet{});
std::string to_fen(const Position& p);

// Compact move notation (spec §11.0), without intermediate landing squares:
// "24-20" quiet, "26x17" capture chain endpoints only. Used for internal logs.
std::string move_to_string(const Move& m);

// Full notation including every landing square of a multi-jump
// (e.g. "32x23x14"), external 1..32 numbering. Requires the position to find
// the chain, so it is only used on the cold protocol/PDN path.
std::string move_to_notation(const Position& p, const RuleSet& r, const Move& m);

// Accepts either the compact form or the full multi-jump form; the multi-jump
// chain must match a legal move exactly.
bool parse_move(const Position& p, const std::string& s, const RuleSet& r, Move& out);

} // namespace mhf
