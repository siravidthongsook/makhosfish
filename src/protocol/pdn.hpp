#pragma once
#include <string>
#include <vector>
#include "core/position.hpp"
#include "core/rules.hpp"

namespace mhf {

// Portable Draughts Notation (spec §11.0). Move tokens use the frozen notation
// ("24-20", "26x17", "32x23x14"). The first seven standard tags are preserved
// verbatim; a `[FEN "..."]` tag (frozen format, no spaces) overrides startpos.
struct PdnTag { std::string name, value; };

struct PdnGame {
  std::vector<PdnTag> tags;
  std::vector<std::string> moves;  // move tokens, in order, both colors
  std::string result;              // "1-0" | "0-1" | "1/2-1/2" | "*" | ""
  std::string comment;             // last brace comment seen (informational)

  std::string tag(const std::string& name) const;
};

// Parse PDN text. Tolerates the usual noise: move numbers ("1.", "1..."), NAGs
// ($n), brace comments, semicolon line comments and nested "( ... )" variations
// (variations are skipped). Never throws; on malformed input it returns whatever
// it managed to read.
PdnGame parse_pdn(const std::string& text);

// Serialize a game: tags, a blank line, numbered move text, then the result.
std::string format_pdn(const PdnGame& game);

// Replay result of a parsed game.
struct PdnReplay {
  bool ok = false;
  std::string error;                 // set when ok == false
  Position final;
  std::vector<Position> positions;   // [0] = start, [k] = after k moves
};

// Replay every move from the game's FEN tag (or startpos). Stops at the first
// illegal move; `error` then names it.
PdnReplay replay_pdn(const PdnGame& game, const RuleSet& r = RuleSet{});

} // namespace mhf
