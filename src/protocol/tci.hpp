// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Siravid Thongsook and MakHosFish contributors

#pragma once
#include <atomic>
#include <iosfwd>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "core/position.hpp"
#include "core/rules.hpp"
#include "search/search.hpp"

namespace mhf {

// Line-oriented TCI v1 server (spec §11.1, a UCI-shaped Thai Checkers
// Interface). Reads commands from `in`, writes responses to `out`. `go` runs on
// a worker thread, so `isready`, `stop` and `quit` keep working while a search
// is in flight; `info` lines stream per iterative-deepening iteration.
class TciSession {
 public:
  TciSession();
  ~TciSession();

  // Blocking; returns on `quit`, EOF, or after draining a running search.
  void run(std::istream& in, std::ostream& out);

 private:
  void out_line(const std::string& s);
  void handle(const std::string& line);
  void cmd_position(const std::vector<std::string>& tok);
  void cmd_go(const std::vector<std::string>& tok);
  void start_search(const SearchLimits& lim);
  void join_search();

  RuleSet rules_;
  Position pos_;
  std::vector<uint64_t> history_;   // hashes of the game before pos_ (for repeats)
  Searcher searcher_{128};          // default 128 MB Hash (spec §11.1)
  int multipv_ = 1;                 // sticky MultiPV setting (overridden by `go multipv N`)

  std::ostream* out_ = nullptr;
  std::mutex mu_;
  std::thread worker_;
  std::atomic<bool> searching_{false};
  bool quit_ = false;
};

} // namespace mhf
