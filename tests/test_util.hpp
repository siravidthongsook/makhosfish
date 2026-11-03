// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Siravid Thongsook and MakHosFish contributors

#pragma once
#include <algorithm>
#include <random>
#include <tuple>
#include <vector>
#include "core/move.hpp"
#include "core/movegen.hpp"
#include "core/position.hpp"
#include "core/rules.hpp"

namespace testutil {

using MoveKey = std::tuple<int, int, unsigned, unsigned, bool>;

inline std::vector<MoveKey> keys(const std::vector<mhf::Move>& mv) {
  std::vector<MoveKey> k;
  k.reserve(mv.size());
  for (const auto& m : mv)
    k.emplace_back(m.from, m.to, m.captured, m.capturedKings, m.promotes);
  std::sort(k.begin(), k.end());
  return k;
}

inline bool same_moves(const std::vector<mhf::Move>& a, const std::vector<mhf::Move>& b) {
  return keys(a) == keys(b);
}

inline mhf::Position random_playout(mhf::Position p, const mhf::RuleSet& r,
                                   std::mt19937_64& rng, int plies) {
  for (int i = 0; i < plies; ++i) {
    auto mv = mhf::legal_moves(p, r);
    if (mv.empty()) break;
    std::uniform_int_distribution<size_t> d(0, mv.size() - 1);
    mhf::do_move(p, mv[d(rng)], r);
  }
  return p;
}

} // namespace testutil
