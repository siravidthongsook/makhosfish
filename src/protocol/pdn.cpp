// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Siravid Thongsook and MakHosFish contributors

#include "protocol/pdn.hpp"
#include "protocol/fen.hpp"

#include <cctype>

namespace mhf {
namespace {

bool is_result(const std::string& t) {
  return t == "1-0" || t == "0-1" || t == "1/2-1/2" || t == "*";
}

// A move number is digits, optionally followed by dots ("12", "1.", "3...").
bool is_move_number(const std::string& t) {
  bool digit = false;
  for (char c : t) {
    if (std::isdigit((unsigned char)c)) { digit = true; continue; }
    if (c == '.') continue;
    return false;
  }
  return digit;
}

// Strip trailing annotation glyphs ("!?", "??", "!") from a move token.
std::string strip_annotations(std::string t) {
  while (!t.empty() && (t.back() == '!' || t.back() == '?')) t.pop_back();
  return t;
}

} // namespace

std::string PdnGame::tag(const std::string& name) const {
  for (const PdnTag& t : tags)
    if (t.name == name) return t.value;
  return "";
}

PdnGame parse_pdn(const std::string& text) {
  PdnGame game;
  std::string token;
  bool at_line_start = true;  // only tag lines start with '['

  auto flush = [&]() {
    if (token.empty()) return;
    const std::string t = token;
    token.clear();
    if (is_result(t)) { game.result = t; return; }
    if (is_move_number(t)) return;
    if (t[0] == '$') return;                       // NAG
    game.moves.push_back(strip_annotations(t));
  };

  const size_t n = text.size();
  size_t i = 0;
  // Skip a UTF-8 BOM if present.
  if (n >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB &&
      (unsigned char)text[2] == 0xBF)
    i = 3;

  for (; i < n;) {
    const char c = text[i];

    if (c == '{') {  // brace comment (no nesting)
      flush();
      const size_t end = text.find('}', i + 1);
      const size_t stop = (end == std::string::npos) ? n : end;
      std::string cm = text.substr(i + 1, stop - (i + 1));
      // Trim surrounding whitespace for the stored comment.
      size_t a = cm.find_first_not_of(" \t\r\n");
      size_t b = cm.find_last_not_of(" \t\r\n");
      if (a != std::string::npos) game.comment = cm.substr(a, b - a + 1);
      i = (end == std::string::npos) ? n : end + 1;
      at_line_start = false;
      continue;
    }
    if (c == ';') {  // line comment
      flush();
      while (i < n && text[i] != '\n') ++i;
      continue;
    }
    if (c == '(') {  // variation: skip to its matching ')'
      flush();
      int depth = 1;
      ++i;
      while (i < n && depth > 0) {
        if (text[i] == '(') ++depth;
        else if (text[i] == ')') --depth;
        ++i;
      }
      at_line_start = false;
      continue;
    }
    if (c == '[' && at_line_start) {  // tag
      const size_t end = text.find(']', i + 1);
      if (end == std::string::npos) break;
      const std::string body = text.substr(i + 1, end - (i + 1));
      const size_t q1 = body.find('"');
      const size_t q2 = (q1 == std::string::npos) ? std::string::npos : body.find('"', q1 + 1);
      if (q1 != std::string::npos && q2 != std::string::npos) {
        std::string name = body.substr(0, q1);
        while (!name.empty() && std::isspace((unsigned char)name.back())) name.pop_back();
        game.tags.push_back({name, body.substr(q1 + 1, q2 - (q1 + 1))});
      }
      i = end + 1;
      while (i < n && text[i] != '\n') ++i;  // rest of a tag line is ignored
      at_line_start = true;
      continue;
    }
    if (c == ')' ) { ++i; at_line_start = false; continue; }  // stray
    if (std::isspace((unsigned char)c)) {
      flush();
      if (c == '\n') at_line_start = true;
      ++i;
      continue;
    }
    token.push_back(c);
    at_line_start = false;
    ++i;
  }
  flush();
  return game;
}

std::string format_pdn(const PdnGame& game) {
  std::string out;
  for (const PdnTag& t : game.tags) out += "[" + t.name + " \"" + t.value + "\"]\n";
  if (!game.tags.empty()) out += '\n';

  std::string num;
  int move_no = 1;
  for (size_t k = 0; k < game.moves.size(); ++k) {
    if (k % 2 == 0) {
      out += std::to_string(move_no++);
      out += ". ";
    }
    out += game.moves[k];
    out += (k % 2 == 0) ? " " : "\n";
  }
  if (game.moves.size() % 2 == 1) out += '\n';
  out += game.result.empty() ? "*" : game.result;
  out += '\n';
  return out;
}

PdnReplay replay_pdn(const PdnGame& game, const RuleSet& r) {
  PdnReplay out;
  Position p;
  const std::string fen = game.tag("FEN");
  if (fen.empty()) {
    p = start_position(r);
  } else if (!parse_fen(fen, p, r)) {
    out.error = "bad FEN tag: " + fen;
    return out;
  }

  out.positions.push_back(p);
  for (const std::string& mv : game.moves) {
    Move m;
    if (!parse_move(p, mv, r, m)) {
      out.error = "illegal move: " + mv;
      return out;
    }
    do_move(p, m, r);
    out.positions.push_back(p);
  }
  out.final = p;
  out.ok = true;
  return out;
}

} // namespace mhf
