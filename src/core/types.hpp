// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Siravid Thongsook and MakHosFish contributors

#pragma once
#include <cstdint>

namespace mhf {

inline constexpr int NB_SQUARES = 32;

// Internal square indexing 0..31 on the 32 dark squares (see spec §3.1).
// External PDN/FEN numbering is internal + 1.

enum Color : uint8_t { WHITE = 0, BLACK = 1 };

inline constexpr Color opposite(Color c) { return c == WHITE ? BLACK : WHITE; }

inline constexpr uint32_t bit(int sq) { return uint32_t(1) << sq; }

inline int popcount(uint32_t x) { return __builtin_popcount(x); }
inline int lsb(uint32_t x) { return __builtin_ctz(x); }
inline int msb(uint32_t x) { return 31 - __builtin_clz(x); }

} // namespace mhf
