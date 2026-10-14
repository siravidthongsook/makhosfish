#include <cstdio>
#include <cstdlib>
#include <string>
#include "core/perft.hpp"
#include "core/position.hpp"
#include "protocol/fen.hpp"

using namespace mhf;

int main(int argc, char** argv) {
  RuleSet r = thai_rules();
  Position p;

  if (argc >= 2 && std::string(argv[1]) == "startpos") {
    p = start_position(r);
  } else if (argc >= 2) {
    if (!parse_fen(argv[1], p, r)) { std::fprintf(stderr, "bad position\n"); return 2; }
  } else {
    p = start_position(r);
  }
  int depth = (argc >= 3) ? std::atoi(argv[2]) : 1;
  if (depth < 0) depth = 0;

  std::printf("MakHosFish perft\n");
  std::printf("fen   %s\n", to_fen(p).c_str());
  for (int d = 1; d <= depth; ++d)
    std::printf("perft(%d) = %llu\n", d, (unsigned long long)perft(p, r, d));
  return 0;
}
