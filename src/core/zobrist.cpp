#include "core/zobrist.hpp"

namespace mhf {

static uint64_t splitmix64(uint64_t& x) {
  uint64_t z = (x += 0x9E3779B97F4A7C15ULL);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31);
}

static Zobrist make_zobrist() {
  Zobrist z{};
  uint64_t seed = 0xC0FFEE123456789ULL;
  for (int pt = 0; pt < 4; ++pt)
    for (int sq = 0; sq < NB_SQUARES; ++sq)
      z.piece[pt][sq] = splitmix64(seed);
  z.stm = splitmix64(seed);
  return z;
}

const Zobrist& zobrist() {
  static const Zobrist z = make_zobrist();
  return z;
}

} // namespace mhf
