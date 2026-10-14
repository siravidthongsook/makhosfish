#include "search/tt.hpp"

namespace mhf {

static_assert(sizeof(TTEntry) == 32, "TTEntry must pack to 32 bytes");
static_assert(sizeof(Move) == 12, "Move layout changed");

void TTable::resize_mb(size_t mb) {
  if (mb == 0) mb = 1;
  size_t want = (mb * 1024 * 1024) / sizeof(Cluster);
  size_t n = 1;
  while (n * 2 <= want) n *= 2;
  table_.assign(n, Cluster{});
  index_mask_ = n - 1;
  gen_ = 0;
}

void TTable::clear() {
  for (auto& c : table_) c = Cluster{};
}

void TTable::new_generation() { gen_ = uint8_t(gen_ + 1); }

namespace {
// Mate scores are stored relative to the node so they survive transpositions.
inline int to_tt(int v, int ply) {
  if (v >= MATE_IN_MAX) return v + ply;
  if (v <= -MATE_IN_MAX) return v - ply;
  return v;
}
inline int from_tt(int v, int ply) {
  if (v >= MATE_IN_MAX) return v - ply;
  if (v <= -MATE_IN_MAX) return v + ply;
  return v;
}
} // namespace

bool TTable::probe(uint64_t key, int depth, int alpha, int beta, int& score, Move& move) {
  if (table_.empty()) return false;
  Cluster& c = table_[key & index_mask_];
  const uint16_t k16 = uint16_t(key >> 48);
  for (int i = 0; i < CLUSTER; ++i) {
    const TTEntry& e = c.e[i];
    if (e.flag == TT_NONE || e.key16 != k16) continue;
    move = e.move;
    if (e.depth >= depth) {
      const int s = from_tt(e.score, ply_);
      if (e.flag == TT_EXACT) { score = s; return true; }
      if (e.flag == TT_LOWER && s >= beta) { score = s; return true; }
      if (e.flag == TT_UPPER && s <= alpha) { score = s; return true; }
    }
    return false;  // key matched, but no usable cutoff
  }
  return false;
}

void TTable::store(uint64_t key, int depth, int score, uint8_t flag, const Move& move,
                   const Move& move2) {
  if (table_.empty()) return;
  Cluster& c = table_[key & index_mask_];
  const uint16_t k16 = uint16_t(key >> 48);

  TTEntry* dest = nullptr;
  for (int i = 0; i < CLUSTER; ++i) {
    if (c.e[i].flag != TT_NONE && c.e[i].key16 == k16) { dest = &c.e[i]; break; }
  }
  if (!dest) {
    // Victim = empty slot, else shallowest, tie-broken by oldest generation.
    int best = 1 << 30;
    for (int i = 0; i < CLUSTER; ++i) {
      TTEntry& e = c.e[i];
      if (e.flag == TT_NONE) { dest = &e; break; }
      int rank = e.depth * 2 + (e.gen == gen_ ? 1 : 0);
      if (rank < best) { best = rank; dest = &e; }
    }
  }

  dest->key16 = k16;
  dest->depth = uint8_t(depth < 0 ? 0 : depth);
  dest->flag = flag;
  dest->gen = gen_;
  dest->score = int16_t(to_tt(score, ply_));
  dest->move = move;
  dest->move2 = move2;
}

} // namespace mhf
