#include "doctest/doctest.h"

#include <cstdint>
#include <cstdlib>
#include <vector>
#include "core/movegen.hpp"
#include "core/position.hpp"
#include "egdb/bitbase.hpp"
#include "egdb/retrograde.hpp"
#include "protocol/fen.hpp"

using namespace mhf;

namespace {

// k=4 solves ~18.4M positions; keep the default run at k<=3 and let MAKHOS_SLOW=1
// opt into the full <=4 sweep.
bool slow_mode() {
  const char* e = std::getenv("MAKHOS_SLOW");
  return e && e[0] == '1';
}

int kmax_default() { return slow_mode() ? 4 : 3; }

// Exact binomial via the multiplicative formula (no factorial overflow).
uint64_t choose(int n, int r) {
  if (r < 0 || r > n) return 0;
  if (r > n - r) r = n - r;
  uint64_t res = 1;
  for (int i = 0; i < r; ++i) res = res * uint64_t(n - i) / uint64_t(i + 1);
  return res;
}

// Independent placement count for one class composition, as a product of
// binomials (a different path than EgdbLayout's own combinatorial table).
uint64_t placements_formula(int n0, int n1, int n2, int n3) {
  const int n[4] = {n0, n1, n2, n3};
  uint64_t p = 1;
  int rem = 32;
  for (int t = 0; t < 4; ++t) { p *= choose(rem, n[t]); rem -= n[t]; }
  return p;
}

uint64_t expect_size(int k) {
  uint64_t total = 0;
  for (int n0 = 0; n0 <= k; ++n0)
    for (int n1 = 0; n1 <= k - n0; ++n1)
      for (int n2 = 0; n2 <= k - n0 - n1; ++n2) {
        const int n3 = k - n0 - n1 - n2;
        total += placements_formula(n0, n1, n2, n3);
      }
  return total * 2;  // both sides to move
}

uint32_t rot180(uint32_t x) {
  uint32_t out = 0;
  while (x) { const int s = lsb(x); x &= x - 1; out |= bit(31 - s); }
  return out;
}

// Colour-swap + board rotation + stm flip: the game is colour-symmetric, so a
// side-to-move-relative WDL must be invariant under this transformation.
Position mirror(const Position& p) {
  Position q;
  q.wm = rot180(p.bm); q.wk = rot180(p.bk);
  q.bm = rot180(p.wm); q.bk = rot180(p.wk);
  q.stm = (p.stm == WHITE) ? BLACK : WHITE;
  q.hash = recompute_hash(q);
  return q;
}

// Second, independent retrograde implementation: a forward value-iteration
// fixpoint over the same index space. Different propagation from solve_upto
// (no reverse move generation), so agreement is meaningful cross-validation.
Egdb solve_reference(int kmax, const RuleSet& r) {
  Egdb e;
  e.layout.build(kmax);
  e.values.assign(size_t(kmax + 1), {});
  for (int k = 1; k <= kmax; ++k) {
    const uint64_t N = e.layout.k_size[size_t(k)];
    auto& val = e.values[size_t(k)];
    val.assign(size_t(N), Wdl::Unknown);
    bool changed = true;
    while (changed) {
      changed = false;
      for (uint64_t i = 0; i < N; ++i) {
        if (val[size_t(i)] != Wdl::Unknown) continue;
        const Position p = e.layout.decode(k, i);
        Move buf[256];
        const int n = generate_moves(p, r, buf, 256);
        if (n == 0) { val[size_t(i)] = Wdl::Loss; changed = true; continue; }
        bool anyLoss = false, allWin = true;
        for (int j = 0; j < n; ++j) {
          Position q = p;
          do_move(q, buf[j], r);
          int kk = 0;
          const int64_t c = e.layout.encode(q, kk);
          const Wdl v = (kk == k) ? val[size_t(c)] : e.values[size_t(kk)][size_t(c)];
          if (v == Wdl::Loss) anyLoss = true;
          if (v != Wdl::Win) allWin = false;
        }
        if (anyLoss) { val[size_t(i)] = Wdl::Win; changed = true; }
        else if (allWin) { val[size_t(i)] = Wdl::Loss; changed = true; }
      }
    }
    for (uint64_t i = 0; i < N; ++i)
      if (val[size_t(i)] == Wdl::Unknown) val[size_t(i)] = Wdl::Draw;
  }
  return e;
}

} // namespace

TEST_CASE("egdb layout sizes match the multinomial placement formula") {
  EgdbLayout L;
  L.build(4);
  for (int k = 1; k <= 4; ++k) CHECK(L.k_size[size_t(k)] == expect_size(k));
  CHECK(L.k_size[1] == 256);
  CHECK(L.k_size[3] == 634880);
}

TEST_CASE("egdb encode/decode is a bijection over the placement space") {
  EgdbLayout L;
  const int kmax = kmax_default();
  L.build(kmax);
  uint64_t visited = 0, mismatches = 0;
  for (int k = 1; k <= kmax; ++k) {
    const uint64_t N = L.k_size[size_t(k)];
    for (uint64_t i = 0; i < N; ++i) {
      const Position p = L.decode(k, i);
      if (p.piece_count() != k) ++mismatches;
      int kk = 0;
      const int64_t slot = L.encode(p, kk);
      if (kk != k || slot != int64_t(i)) ++mismatches;
      ++visited;
    }
  }
  CHECK(visited > 0);
  CHECK(mismatches == 0);
}

TEST_CASE("retrograde solver agrees with the independent reference solver") {
  const RuleSet r = thai_rules();
  const int kmax = kmax_default();
  const Egdb fast = solve_upto(kmax, r);
  // Forward value iteration is O(passes*N), so only push it to k=3 when explicitly
  // asked; k=1,2 always cross-check.
  const int ref_k = slow_mode() ? 3 : 2;
  const Egdb ref = solve_reference(ref_k, r);

  uint64_t mismatches = 0;
  for (int k = 1; k <= ref_k; ++k) {
    const uint64_t N = fast.layout.k_size[size_t(k)];
    for (uint64_t i = 0; i < N; ++i)
      if (fast.values[size_t(k)][size_t(i)] != ref.values[size_t(k)][size_t(i)]) ++mismatches;
  }
  CHECK(mismatches == 0);

  // No leftover Unknown values: every position resolves to win/loss/draw.
  uint64_t unknown = 0;
  for (int k = 1; k <= kmax; ++k)
    for (Wdl v : fast.values[size_t(k)])
      if (v == Wdl::Unknown) ++unknown;
  CHECK(unknown == 0);
}

TEST_CASE("solved WDL is invariant under the colour-symmetry mirror") {
  const RuleSet r = thai_rules();
  const int kmax = kmax_default();
  const Egdb db = solve_upto(kmax, r);

  uint64_t checked = 0, mismatches = 0;
  for (int k = 1; k <= kmax; ++k) {
    const uint64_t N = db.layout.k_size[size_t(k)];
    for (uint64_t i = 0; i < N; ++i) {
      const Position p = db.layout.decode(k, i);
      if (db.probe(p) != db.probe(mirror(p))) ++mismatches;
      ++checked;
    }
  }
  CHECK(checked > 0);
  CHECK(mismatches == 0);
}

TEST_CASE("known small endgames resolve to the expected WDL") {
  const RuleSet r = thai_rules();
  const Egdb db = solve_upto(kmax_default(), r);

  // Two kings: neither side can force a capture -> draw.
  Position kk;
  kk.wk = bit(18); kk.bk = bit(13); kk.stm = WHITE;
  kk.hash = recompute_hash(kk);
  CHECK(db.probe(kk) == Wdl::Draw);

  // A lone king with the opponent wiped out: the bare side to move has no move.
  Position soleWhite;  soleWhite.wk = bit(9); soleWhite.stm = WHITE;
  soleWhite.hash = recompute_hash(soleWhite);
  CHECK(db.probe(soleWhite) == Wdl::Win);

  Position soleWhiteBtm = soleWhite;
  soleWhiteBtm.stm = BLACK;
  soleWhiteBtm.hash = recompute_hash(soleWhiteBtm);
  CHECK(db.probe(soleWhiteBtm) == Wdl::Loss);

  // White king can capture the only black man in one move (white wins at once).
  Position wkin1;
  wkin1.wk = bit(9);   // (row2,col3)
  wkin1.bm = bit(13);  // (row3,col2), adjacent on the SW diagonal
  wkin1.stm = WHITE;
  wkin1.hash = recompute_hash(wkin1);
  CHECK(db.probe(wkin1) == Wdl::Win);

  // Positions outside the stored range report Unknown.
  Position start = start_position(r);
  CHECK(db.probe(start) == Wdl::Unknown);
}
