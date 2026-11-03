// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Siravid Thongsook and MakHosFish contributors

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <vector>
#include "core/position.hpp"
#include "core/rules.hpp"
#include "eval/eval.hpp"
#include "eval/pattern.hpp"
#include "protocol/fen.hpp"

using namespace mhf;

// Texel-style logistic-regression tuner for the E2 pattern weights (spec §8/E4).
// Reads `<fen> <result>` samples produced by `datagen`, fits the pattern weights
// by SGD on the cross-entropy between sigmoid(K*eval_stm) and the game result,
// and writes a `.mhpw` weight file. The E1 hand-feature score is a fixed offset,
// so the tuner only learns the pattern correction on top of E1.
//
// usage: makhosfish_tune <data.txt> <out.mhpw> [epochs=40] [lr=0.05] [init.mhpw]

namespace {
constexpr double K = 2.302585092994046 / 400.0;  // ln(10)/400 for 10^(-x/400)

struct Sample {
  int plus[PatternEval::NWINDOWS];
  int minus[PatternEval::NWINDOWS];
  float base_stm;   // E1 score from the side-to-move's perspective
  float sign;       // +1 if White to move, -1 if Black
  float target;     // 1 / 0.5 / 0 from the side-to-move's perspective
};

inline double sigmoid(double x) { return 1.0 / (1.0 + std::exp(-K * x)); }

double evaluate_loss(const std::vector<Sample>& s, const std::vector<float>& w) {
  double loss = 0.0;
  for (const Sample& x : s) {
    double pat = 0.0;
    for (int i = 0; i < PatternEval::NWINDOWS; ++i) pat += w[x.plus[i]] - w[x.minus[i]];
    const double p = sigmoid(x.base_stm + x.sign * pat);
    const double t = x.target;
    loss += -(t * std::log(p + 1e-12) + (1.0 - t) * std::log(1.0 - p + 1e-12));
  }
  return s.empty() ? 0.0 : loss / double(s.size());
}

int parse_result(const std::string& tok, double& out) {
  out = std::atof(tok.c_str());
  return (out == 0.0 || out == 0.5 || out == 1.0) ? 0 : -1;
}
} // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr,
                 "usage: makhosfish_tune <data.txt> <out.mhpw> [epochs=40] [lr=0.05] "
                 "[init.mhpw] [max_samples=3000000]\n");
    return 2;
  }
  const std::string data_path = argv[1];
  const std::string out_path = argv[2];
  const int epochs = (argc > 3) ? std::atoi(argv[3]) : 40;
  const double lr0 = (argc > 4) ? std::atof(argv[4]) : 0.05;
  const size_t max_samples = (argc > 6) ? size_t(std::strtoull(argv[6], nullptr, 10)) : 3000000u;

  const RuleSet r = thai_rules();
  PatternEval codes;  // weight-free; used only for codes() and phase_of()

  std::vector<float> w(codes.size(), 0.0f);
  if (argc > 5 && argv[5][0] != '\0' && std::strcmp(argv[5], "-") != 0) {
    PatternEval init;
    if (!init.load(argv[5])) { std::fprintf(stderr, "cannot load %s\n", argv[5]); return 2; }
    std::memcpy(w.data(), init.data(), w.size() * sizeof(int16_t));
  }

  std::ifstream is(data_path);
  if (!is) { std::fprintf(stderr, "cannot open %s\n", data_path.c_str()); return 2; }

  std::vector<Sample> samples;
  samples.reserve(1 << 20);
  std::string line;
  int bad = 0;
  while (std::getline(is, line)) {
    if (line.empty()) continue;
    std::istringstream ss(line);
    std::string fen, resTok;
    if (!(ss >> fen >> resTok)) continue;
    double target;
    if (parse_result(resTok, target) < 0) { ++bad; continue; }
    Position p;
    if (!parse_fen(fen, p, r)) { ++bad; continue; }

    Sample s{};
    int white[PatternEval::NWINDOWS], black[PatternEval::NWINDOWS];
    const int ph = codes.phase_of(p);
    codes.codes(p, ph, white, black);
    for (int i = 0; i < PatternEval::NWINDOWS; ++i) {
      s.plus[i] = PatternEval::idx(i, ph, white[i]);
      s.minus[i] = PatternEval::idx(PatternEval::PAIR[i], ph, black[i]);
    }
    const int base = eval_detail(p, r, EvalMode::E1).white_minus_black;
    s.sign = (p.stm == WHITE) ? 1.0f : -1.0f;
    s.base_stm = s.sign * float(base);
    s.target = float(target);
    samples.push_back(s);
    if (samples.size() % 2000000 == 0) {
      std::fprintf(stderr, "  loaded %zu samples\n", samples.size());
      std::fflush(stderr);
    }
  }
  std::printf("tune: %zu samples (%d skipped) from %s\n", samples.size(), bad, data_path.c_str());
  if (samples.empty()) { std::fprintf(stderr, "no samples\n"); return 2; }
  if (max_samples && samples.size() > max_samples) {
    std::mt19937_64 pick(987654321u);
    std::shuffle(samples.begin(), samples.end(), pick);
    samples.resize(max_samples);
    std::printf("  subsampled to %zu samples\n", samples.size());
  }

  // 90/10 train/validation split (validation = every 10th sample).
  std::vector<Sample> train, val;
  train.reserve(samples.size());
  for (size_t i = 0; i < samples.size(); ++i)
    (i % 10 == 9 ? val : train).push_back(samples[i]);

  const double base_val = evaluate_loss(val, w);
  std::printf("  E1 baseline val loss %.5f\n", base_val);

  std::vector<float> best = w;
  double best_val = base_val;

  std::mt19937_64 rng(12345);
  std::vector<size_t> order(train.size());
  std::iota(order.begin(), order.end(), 0);

  for (int ep = 0; ep < epochs; ++ep) {
    std::shuffle(order.begin(), order.end(), rng);
    const double lr = lr0 / (1.0 + 0.05 * ep);
    for (size_t idx : order) {
      const Sample& x = train[idx];
      double pat = 0.0;
      for (int i = 0; i < PatternEval::NWINDOWS; ++i) pat += w[x.plus[i]] - w[x.minus[i]];
      const double g = K * (sigmoid(x.base_stm + x.sign * pat) - x.target) * x.sign;
      for (int i = 0; i < PatternEval::NWINDOWS; ++i) {
        w[x.plus[i]] = float(w[x.plus[i]] - lr * g);
        w[x.minus[i]] = float(w[x.minus[i]] + lr * g);
      }
    }
    const double vl = evaluate_loss(val, w);
    const double tl = evaluate_loss(train, w);
    if (vl < best_val) { best_val = vl; best = w; }
    std::printf("  epoch %2d  train %.5f  val %.5f  lr %.4f%s\n",
                ep + 1, tl, vl, lr, vl <= best_val ? " *" : "");
    std::fflush(stdout);
  }

  PatternEval out;
  for (size_t i = 0; i < best.size(); ++i) {
    double v = best[i];
    if (v > 30000.0) v = 30000.0;
    if (v < -30000.0) v = -30000.0;
    out.data()[i] = int16_t(std::lround(v));
  }
  if (!out.save(out_path)) { std::fprintf(stderr, "cannot write %s\n", out_path.c_str()); return 2; }
  std::printf("tune: best val loss %.5f (E1 baseline %.5f) -> %s\n", best_val, base_val, out_path.c_str());
  return 0;
}
