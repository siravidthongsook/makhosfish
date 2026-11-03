// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Siravid Thongsook and MakHosFish contributors

#include "protocol/fen.hpp"
#include "core/movegen.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace mhf {
namespace {

std::vector<std::string> split(const std::string& s, char delim) {
  std::vector<std::string> out;
  std::string cur;
  for (char ch : s) {
    if (ch == delim) { out.push_back(cur); cur.clear(); }
    else cur.push_back(ch);
  }
  out.push_back(cur);
  return out;
}

bool parse_uint(const std::string& s, int& v) {
  if (s.empty()) return false;
  v = 0;
  for (char ch : s) {
    if (!std::isdigit((unsigned char)ch)) return false;
    v = v * 10 + (ch - '0');
  }
  return true;
}

// Returns false on any malformed item, so a typo (e.g. a stray `B21` mid-list)
// is rejected instead of being silently dropped.
bool add_square(uint32_t& men, uint32_t& kings, const std::string& item) {
  std::string t = item;
  bool king = false;
  if (!t.empty() && (t[0] == 'K' || t[0] == 'k')) { king = true; t.erase(t.begin()); }
  auto dash = t.find('-');
  if (dash == std::string::npos) {
    int n = 0;
    if (!parse_uint(t, n) || n < 1 || n > 32) return false;
    (king ? kings : men) |= bit(n - 1);
    return true;
  }
  int a = 0, b = 0;
  if (!parse_uint(t.substr(0, dash), a) || !parse_uint(t.substr(dash + 1), b)) return false;
  if (a < 1 || a > 32 || b < 1 || b > 32) return false;
  if (a > b) { int tmp = a; a = b; b = tmp; }
  for (int n = a; n <= b; ++n) (king ? kings : men) |= bit(n - 1);
  return true;
}

// Only a leading color letter (W/B) is the section tag; `K` stays to mark a king.
bool parse_side(const std::string& tok, uint32_t& men, uint32_t& kings) {
  std::string body = tok;
  if (!body.empty()) {
    char c = (char)std::toupper((unsigned char)body[0]);
    if (c == 'W' || c == 'B') body.erase(body.begin());
  }
  if (body.empty()) return true;  // an empty side list is allowed
  for (const auto& item : split(body, ',')) {
    if (item.empty() || !add_square(men, kings, item)) return false;
  }
  return true;
}

std::string list_side(uint32_t men, uint32_t kings) {
  std::string out;
  auto push = [&](const std::string& tok) {
    if (!out.empty()) out.push_back(',');
    out += tok;
  };
  int start = -1, prev = -1;
  auto flushRun = [&]() {
    if (start < 0) return;
    push(start == prev ? std::to_string(start + 1)
                       : std::to_string(start + 1) + "-" + std::to_string(prev + 1));
    start = prev = -1;
  };
  for (int sq = 0; sq < NB_SQUARES; ++sq) {
    if (men & bit(sq)) {
      if (start >= 0 && sq == prev + 1) prev = sq;
      else { flushRun(); start = prev = sq; }
    } else {
      flushRun();
      if (kings & bit(sq)) push("K" + std::to_string(sq + 1));
    }
  }
  flushRun();
  return out;
}

} // namespace

bool parse_fen(const std::string& s, Position& p, const RuleSet& r) {
  (void)r;
  auto parts = split(s, ':');
  if (parts.size() < 3) return false;

  std::string stm = parts[0];
  for (char& c : stm) c = (char)std::toupper((unsigned char)c);
  if (stm != "W" && stm != "B") return false;

  Position np;
  if (!parse_side(parts[1], np.wm, np.wk)) return false;
  if (!parse_side(parts[2], np.bm, np.bk)) return false;

  if (parts.size() >= 4 && !parts[3].empty()) {
    int v = 0;
    if (parse_uint(parts[3], v)) np.rule50 = uint16_t(v);
  }

  uint32_t occ = np.wm | np.wk | np.bm | np.bk;
  if (popcount(occ) != popcount(np.wm) + popcount(np.wk) + popcount(np.bm) + popcount(np.bk))
    return false;

  np.stm = (stm == "W") ? WHITE : BLACK;
  np.hash = recompute_hash(np);
  p = np;
  return true;
}

std::string to_fen(const Position& p) {
  std::string out;
  out += (p.stm == WHITE) ? "W" : "B";
  out += ":W"; out += list_side(p.wm, p.wk);
  out += ":B"; out += list_side(p.bm, p.bk);
  if (p.rule50) { out += ':'; out += std::to_string(p.rule50); }
  return out;
}

std::string move_to_string(const Move& m) {
  const char sep = m.is_capture() ? 'x' : '-';
  return std::to_string(int(m.from) + 1) + sep + std::to_string(int(m.to) + 1);
}

// `p[0]` = chain length L; `p[1..L]` = internal squares (from ... to).
std::string format_chain(const Move& m, const uint8_t* p) {
  const char sep = m.is_capture() ? 'x' : '-';
  const int L = p[0];
  std::string out;
  for (int k = 1; k <= L; ++k) {
    if (k > 1) out.push_back(sep);
    out += std::to_string(int(p[k]) + 1);
  }
  return out;
}

std::string move_to_notation(const Position& p, const RuleSet& r, const Move& m) {
  Move buf[256];
  uint8_t paths[256 * MAX_MOVE_PATH];
  const int n = generate_moves_with_paths(p, r, buf, paths, 256);
  for (int i = 0; i < n; ++i)
    if (buf[i].from == m.from && buf[i].to == m.to && buf[i].captured == m.captured)
      return format_chain(buf[i], paths + size_t(i) * MAX_MOVE_PATH);
  return move_to_string(m);
}

bool parse_move(const Position& p, const std::string& s, const RuleSet& r, Move& out) {
  std::string t;
  for (char c : s) if (!std::isspace((unsigned char)c)) t.push_back(c);
  if (t.empty()) return false;

  Move buf[256];
  uint8_t paths[256 * MAX_MOVE_PATH];
  const int n = generate_moves_with_paths(p, r, buf, paths, 256);
  for (int i = 0; i < n; ++i)
    if (format_chain(buf[i], paths + size_t(i) * MAX_MOVE_PATH) == t) { out = buf[i]; return true; }
  return false;
}

} // namespace mhf
