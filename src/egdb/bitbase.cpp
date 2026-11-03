// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Siravid Thongsook and MakHosFish contributors

#include "egdb/bitbase.hpp"

namespace mhf {

const char* wdl_name(Wdl v) {
  switch (v) {
    case Wdl::Win:  return "win";
    case Wdl::Loss: return "loss";
    case Wdl::Draw: return "draw";
    default:        return "unknown";
  }
}

namespace {

// Binomial coefficients C(n, r) for n <= 32. C(32,16) = 601080390 fits in u32
// but the products used for slot counts want u64.
constexpr int MAXN = 32;
uint64_t g_binom[MAXN + 1][MAXN + 1];
struct BinomInit {
  BinomInit() {
    for (int n = 0; n <= MAXN; ++n) {
      for (int r = 0; r <= MAXN; ++r) g_binom[n][r] = 0;
      g_binom[n][0] = 1;
      for (int r = 1; r <= n; ++r)
        g_binom[n][r] = g_binom[n - 1][r - 1] + g_binom[n - 1][r];
    }
  }
} g_binom_init;

inline uint64_t binom(int n, int r) {
  if (n < 0 || r < 0 || r > n) return 0;
  return g_binom[n][r];
}

// Colex rank of an ascending r-subset of {0..n-1}: sum_j C(s[j], j+1).
uint64_t colex_rank(const int* s, int r) {
  uint64_t rank = 0;
  for (int j = 0; j < r; ++j) rank += binom(s[j], j + 1);
  return rank;
}

// Inverse of colex_rank: writes the ascending r-subset of {0..n-1}.
void colex_unrank(uint64_t rank, int n, int r, int* out) {
  for (int i = r; i >= 1; --i) {
    int c = i - 1;                                   // C(i-1, i) == 0 <= rank
    while (c + 1 <= n - 1 && binom(c + 1, i) <= rank) ++c;
    out[i - 1] = c;
    rank -= binom(c, i);
  }
}

// Ascending squares of a mask.
int squares_of(uint32_t mask, int* out) {
  int c = 0;
  while (mask) { out[c++] = lsb(mask); mask &= mask - 1; }
  return c;
}

uint64_t placements_of(const uint8_t n[4]) {
  uint64_t p = 1;
  int rem = NB_SQUARES;
  for (int t = 0; t < 4; ++t) { p *= binom(rem, n[t]); rem -= n[t]; }
  return p;
}

} // namespace

void EgdbLayout::build(int kmax) {
  max_pieces = kmax;
  comps.assign(size_t(kmax + 1), {});
  k_size.assign(size_t(kmax + 1), 0);
  for (int k = 1; k <= kmax; ++k) {
    uint64_t offset = 0;
    for (int n0 = 0; n0 <= k; ++n0)
      for (int n1 = 0; n1 <= k - n0; ++n1)
        for (int n2 = 0; n2 <= k - n0 - n1; ++n2) {
          Comp c;
          c.n[0] = uint8_t(n0);
          c.n[1] = uint8_t(n1);
          c.n[2] = uint8_t(n2);
          c.n[3] = uint8_t(k - n0 - n1 - n2);
          c.placements = placements_of(c.n);
          c.offset = offset;
          offset += c.placements * 2;
          comps[size_t(k)].push_back(c);
        }
    k_size[size_t(k)] = offset;
  }
}

int64_t EgdbLayout::encode(const Position& p, int& k_out) const {
  const int n[4] = {popcount(p.wm), popcount(p.wk), popcount(p.bm), popcount(p.bk)};
  const int k = n[0] + n[1] + n[2] + n[3];
  k_out = k;
  if (k < 1 || k > max_pieces) return -1;

  const Comp* comp = nullptr;
  for (const Comp& c : comps[size_t(k)])
    if (c.n[0] == n[0] && c.n[1] == n[1] && c.n[2] == n[2] && c.n[3] == n[3]) {
      comp = &c;
      break;
    }
  if (!comp) return -1;

  const uint32_t bits[4] = {p.wm, p.wk, p.bm, p.bk};
  int avail[NB_SQUARES];
  for (int i = 0; i < NB_SQUARES; ++i) avail[i] = i;
  int rem = NB_SQUARES;

  uint64_t place = 0;
  for (int t = 0; t < 4; ++t) {
    const int r = comp->n[t];
    const uint64_t radix = binom(rem, r);
    int sq[NB_SQUARES];
    const int cnt = squares_of(bits[t], sq);
    int digits[NB_SQUARES];
    for (int j = 0; j < cnt; ++j) {
      int pos = 0;
      while (avail[pos] != sq[j]) ++pos;
      digits[j] = pos;
    }
    place = place * radix + colex_rank(digits, cnt);
    for (int j = 0; j < cnt; ++j) {          // consume the chosen squares
      int pos = 0;
      while (avail[pos] != sq[j]) ++pos;
      for (int m = pos; m < rem - 1; ++m) avail[m] = avail[m + 1];
      --rem;
    }
  }
  return int64_t(comp->offset + place * 2 + (p.stm == BLACK ? 1u : 0u));
}

Position EgdbLayout::decode(int k, uint64_t slot) const {
  Position p;
  const Comp* comp = nullptr;
  for (const Comp& c : comps[size_t(k)])
    if (slot >= c.offset && slot < c.offset + c.placements * 2) { comp = &c; break; }
  if (!comp) return p;

  uint64_t local = slot - comp->offset;
  uint64_t place = local >> 1;
  p.stm = (local & 1) ? BLACK : WHITE;

  // Mixed radix rad[t] = C(remaining_t, n_t); a class's digit has weight equal
  // to the product of the radices of all LATER classes.
  uint64_t rad[4];
  {
    int r = NB_SQUARES;
    for (int t = 0; t < 4; ++t) { rad[t] = binom(r, comp->n[t]); r -= comp->n[t]; }
  }
  uint64_t tail[4];
  tail[3] = 1;
  for (int t = 2; t >= 0; --t) tail[t] = tail[t + 1] * rad[t + 1];

  int avail[NB_SQUARES];
  for (int i = 0; i < NB_SQUARES; ++i) avail[i] = i;
  int rem = NB_SQUARES;
  uint32_t masks[4] = {0, 0, 0, 0};
  for (int t = 0; t < 4; ++t) {
    const int r = comp->n[t];
    const uint64_t digit = place / tail[t];
    place %= tail[t];
    int digits[NB_SQUARES];
    colex_unrank(digit, rem, r, digits);
    for (int j = 0; j < r; ++j) masks[t] |= bit(avail[digits[j]]);
    for (int j = r - 1; j >= 0; --j) {       // consume from the back so earlier digit positions stay valid
      const int pos = digits[j];
      for (int m = pos; m < rem - 1; ++m) avail[m] = avail[m + 1];
      --rem;
    }
  }
  p.wm = masks[0]; p.wk = masks[1]; p.bm = masks[2]; p.bk = masks[3];
  return p;
}

} // namespace mhf
