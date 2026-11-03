// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Siravid Thongsook and MakHosFish contributors

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include "core/perft.hpp"
#include "core/movegen.hpp"
#include "core/position.hpp"
#include "protocol/fen.hpp"

using namespace mhf;

static void root_divide(const Position& p, const RuleSet& r, int depth) {
  Move buf[256];
  int n = generate_moves(p, r, buf, 256);
  uint64_t total = 0;
  for (int i = 0; i < n; ++i) {
    Position c = p;
    do_move(c, buf[i], r);
    uint64_t cnt = perft(c, r, depth - 1);
    total += cnt;
    std::printf("%-8s %llu\n", move_to_string(buf[i]).c_str(), (unsigned long long)cnt);
  }
  std::printf("total    %llu\n", (unsigned long long)total);
}

int main(int argc, char** argv) {
  RuleSet r = thai_rules();
  Position p;

  if (argc >= 2 && std::string(argv[1]) != "startpos") {
    if (!parse_fen(argv[1], p, r)) { std::fprintf(stderr, "bad fen\n"); return 2; }
  } else {
    p = start_position(r);
  }
  int depth = (argc >= 3) ? std::atoi(argv[2]) : 5;

  std::printf("fen %s\n", to_fen(p).c_str());
  auto t0 = std::chrono::steady_clock::now();
  uint64_t nodes = perft(p, r, depth);
  auto t1 = std::chrono::steady_clock::now();
  double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

  if (depth >= 2) root_divide(p, r, depth);
  std::printf("perft(%d) = %llu  (%.1f ms, %.2f Mnps)\n",
              depth, (unsigned long long)nodes, ms,
              ms > 0 ? double(nodes) / ms / 1000.0 : 0.0);
  return 0;
}
