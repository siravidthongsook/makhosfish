#pragma once
#include <cstdint>
#include "core/move.hpp"
#include "core/position.hpp"
#include "core/rules.hpp"

namespace mhf {

// Evaluation stages (spec §8): E0 = material + PST; E1 adds hand features;
// E2 adds learned pattern terms (eval/pattern.hpp) on top of E1. The mode is a
// runtime switch so any stage can be A/B-tested against another in a gauntlet.
enum class EvalMode : uint8_t { E0 = 0, E1 = 1, E2 = 2 };

inline constexpr int MAN_VALUE = 100;
inline constexpr int KING_VALUE = 300;
inline constexpr int TEMPO = 10;

// E1 hand-feature weights in centipawns (hand-tuned start, spec §8 stage E1).
inline constexpr int MOB_MAN    = 4;    // per empty forward square a man can step to
inline constexpr int MOB_KING   = 1;    // per empty square reachable by a flying king
inline constexpr int CAP_THREAT = 12;   // per enemy man currently capturable
inline constexpr int DEFENDED   = 8;    // per man supported from the diagonal behind
inline constexpr int FIRST_KING = 40;   // side that owns a king while the other has none
inline constexpr int PRECROWN   = 12;   // per man one step from crowning

// White-minus-black feature breakdown (positive favours White). Tempo is not
// included here; `evaluate` adds it relative to the side to move.
struct EvalDetail {
  int material = 0;
  int pst = 0;
  int mobility = 0;
  int captures = 0;
  int structure = 0;
  int first_king = 0;
  int precrown = 0;
  int pattern = 0;   // E2 only: learned pattern-window contribution
  int white_minus_black = 0;
};

// Centipawns from the side-to-move's perspective (spec §7/§8).
int evaluate(const Position& p, const RuleSet& r, EvalMode mode = EvalMode::E1);

// Structured, side-independent breakdown of the evaluation (for tests/debug).
EvalDetail eval_detail(const Position& p, const RuleSet& r, EvalMode mode = EvalMode::E1);

// Piece-square value for color `c` on square `sq` (white-relative geometry).
int pst_of(Color c, bool king, int sq);

// PST change for the moving piece only; cheap quiet-move ordering signal.
int move_pst_delta(const Position& p, const Move& m, const RuleSet& r);

} // namespace mhf
