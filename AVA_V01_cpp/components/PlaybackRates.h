#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace PlaybackRates {

inline constexpr std::array<double, 10> kTicks{
    0.25, 0.50, 0.75, 1.00, 1.25, 1.50, 1.75, 2.00, 3.00, 4.00};
inline constexpr double kResetRate = 1.0;

inline int indexOfNearest(double rate) {
  int bestIndex = 0;
  double bestDistance = std::abs(kTicks.front() - rate);
  for (std::size_t index = 1; index < kTicks.size(); ++index) {
    const double distance = std::abs(kTicks.at(index) - rate);
    if (distance < bestDistance) {
      bestDistance = distance;
      bestIndex = static_cast<int>(index);
    }
  }
  return bestIndex;
}

inline double atIndex(int index) {
  const int clamped = std::clamp(index, 0, static_cast<int>(kTicks.size()) - 1);
  return kTicks.at(static_cast<std::size_t>(clamped));
}

inline double snap(double rate) {
  return atIndex(indexOfNearest(rate));
}

inline double slower(double rate) {
  return atIndex(indexOfNearest(rate) - 1);
}

inline double faster(double rate) {
  return atIndex(indexOfNearest(rate) + 1);
}

}  // namespace PlaybackRates
