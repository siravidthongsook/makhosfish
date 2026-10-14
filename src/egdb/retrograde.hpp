#pragma once
#include <vector>
#include "core/rules.hpp"
#include "egdb/bitbase.hpp"

namespace mhf {

// A WLD bitbase: the outcome of every position with 1..max_pieces pieces, from
// the side-to-move's perspective.
struct Egdb {
  EgdbLayout layout;
  std::vector<std::vector<Wdl>> values;  // values[k].size() == layout.k_size[k]

  // WDL of `p`, or Wdl::Unknown if `p` is outside the stored piece range.
  Wdl probe(const Position& p) const;
};

// Thompson/Bellman backward induction over every position with 1..kmax pieces.
// The 50-move and repetition rules are ignored, so draws are exact game-theoretic
// draws (e.g. K vs K), not clock-limited ones. Lower piece counts are solved
// first because captures (which reduce the count) are read from them.
Egdb solve_upto(int kmax, const RuleSet& r = thai_rules());

} // namespace mhf
