#include "eval/pattern.hpp"
#include <cstdio>
#include <fstream>
#include "core/types.hpp"

namespace mhf {
namespace {

// State of `sq` from color `c`'s perspective (see PatternEval header comment).
inline int cell_state(uint32_t wm, uint32_t wk, uint32_t bm, uint32_t bk, int sq, Color c) {
  const uint32_t b = bit(sq);
  if (c == WHITE) {
    if (wm & b) return 1;
    if (wk & b) return 2;
    if (bm & b) return 3;
    if (bk & b) return 4;
  } else {
    if (bm & b) return 1;
    if (bk & b) return 2;
    if (wm & b) return 3;
    if (wk & b) return 4;
  }
  return 0;
}

inline int code_from(const int (&cells)[PatternEval::CELLS], uint32_t wm, uint32_t wk,
                     uint32_t bm, uint32_t bk, Color c) {
  int code = 0;
  for (int i = PatternEval::CELLS - 1; i >= 0; --i)
    code = code * 5 + cell_state(wm, wk, bm, bk, cells[i], c);
  return code;
}

void put_u16(std::ostream& os, uint16_t v) {
  os.put(char(v & 0xFF));
  os.put(char((v >> 8) & 0xFF));
}
bool get_u16(std::istream& is, uint16_t& v) {
  const int lo = is.get();
  const int hi = is.get();
  if (lo == EOF || hi == EOF) return false;
  v = uint16_t((hi << 8) | lo);
  return true;
}

} // namespace

int PatternEval::score(const Position& p) const {
  const int ph = phase_of(p);
  int sum = 0;
  for (int w = 0; w < NWINDOWS; ++w) {
    const int cw = code_from(WINDOWS[w], p.wm, p.wk, p.bm, p.bk, WHITE);
    const int cb = code_from(WINDOWS[w], p.wm, p.wk, p.bm, p.bk, BLACK);
    sum += w_[idx(w, ph, cw)] - w_[idx(PAIR[w], ph, cb)];
  }
  return sum;
}

void PatternEval::codes(const Position& p, int phase, int white[NWINDOWS],
                        int black[NWINDOWS]) const {
  (void)phase;
  for (int w = 0; w < NWINDOWS; ++w) {
    white[w] = code_from(WINDOWS[w], p.wm, p.wk, p.bm, p.bk, WHITE);
    black[w] = code_from(WINDOWS[w], p.wm, p.wk, p.bm, p.bk, BLACK);
  }
}

bool PatternEval::save(const std::string& path) const {
  std::ofstream os(path, std::ios::binary);
  if (!os) return false;
  os.put('M'); os.put('H'); os.put('P'); os.put('W');
  put_u16(os, 1);                         // version
  put_u16(os, uint16_t(NWINDOWS));
  put_u16(os, uint16_t(CELLS));
  put_u16(os, uint16_t(CLASSES));
  put_u16(os, uint16_t(PHASES));
  for (int16_t v : w_) put_u16(os, uint16_t(v));
  return static_cast<bool>(os);
}

bool PatternEval::load(const std::string& path) {
  std::ifstream is(path, std::ios::binary);
  if (!is) return false;
  char magic[4];
  is.read(magic, 4);
  if (!is || magic[0] != 'M' || magic[1] != 'H' || magic[2] != 'P' || magic[3] != 'W')
    return false;

  uint16_t version = 0, nw = 0, cells = 0, classes = 0, phases = 0;
  if (!get_u16(is, version) || !get_u16(is, nw) || !get_u16(is, cells) ||
      !get_u16(is, classes) || !get_u16(is, phases))
    return false;
  if (version != 1 || nw != NWINDOWS || cells != CELLS || classes != CLASSES ||
      phases != PHASES)
    return false;

  std::vector<int16_t> tmp(w_.size());
  for (int16_t& v : tmp) {
    uint16_t u = 0;
    if (!get_u16(is, u)) return false;
    v = int16_t(u);
  }
  w_.swap(tmp);
  return true;
}

PatternEval& active_pattern() {
  static PatternEval instance;
  return instance;
}

} // namespace mhf
