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

// Move notation (spec §11.0): "24-20" quiet, "26x17" capture.
// NOTE(M3): multi-jump landing paths ("32x23x14") and full-chain PDN output are
// deferred until the Move carries its intermediate landing squares.
std::string move_to_string(const Move& m);
bool parse_move(const Position& p, const std::string& s, const RuleSet& r, Move& out);

} // namespace mhf
