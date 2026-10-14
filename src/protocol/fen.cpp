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

void add_square(uint32_t& men, uint32_t& kings, const std::string& item) {
  std::string t = item;
  bool king = false;
  if (!t.empty() && (t[0] == 'K' || t[0] == 'k')) { king = true; t.erase(t.begin()); }
  auto dash = t.find('-');
  if (dash == std::string::npos) {
    int n = 0;
    if (parse_uint(t, n) && n >= 1 && n <= 32)
      (king ? kings : men) |= bit(n - 1);
  } else {
    int a = 0, b = 0;
    if (parse_uint(t.substr(0, dash), a) && parse_uint(t.substr(dash + 1), b)) {
      if (a > b) { int tmp = a; a = b; b = tmp; }
      for (int n = a; n <= b; ++n)
        if (n >= 1 && n <= 32) (king ? kings : men) |= bit(n - 1);
    }
  }
}

void parse_side(const std::string& tok, uint32_t& men, uint32_t& kings) {
  std::string body = tok;
  if (!body.empty() && std::isalpha((unsigned char)body[0])) body.erase(body.begin());
  for (const auto& item : split(body, ','))
    if (!item.empty()) add_square(men, kings, item);
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
  parse_side(parts[1], np.wm, np.wk);
  parse_side(parts[2], np.bm, np.bk);

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

bool parse_move(const Position& p, const std::string& s, const RuleSet& r, Move& out) {
  auto dash = s.find('-');
  auto ex = s.find('x');
  size_t pos = std::min(dash == std::string::npos ? s.size() : dash,
                        ex == std::string::npos ? s.size() : ex);
  if (pos == s.size()) return false;
  int a = 0, b = 0;
  if (!parse_uint(s.substr(0, pos), a)) return false;
  if (!parse_uint(s.substr(pos + 1), b)) return false;
  if (a < 1 || a > 32 || b < 1 || b > 32) return false;

  for (const Move& m : legal_moves(p, r))
    if (m.from == a - 1 && m.to == b - 1) { out = m; return true; }
  return false;
}

} // namespace mhf
