#pragma once

#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <limits>

namespace TimeConvert {

/// Convert seconds to milliseconds by nearest rounding.
/// Truncation of `seconds * 1000` can drop 0.3s to 299ms because 0.3 is not
/// an exact binary float. Non-finite and overflowing values saturate.
inline qint64 millisecondsFromSeconds(double seconds) {
  if (!std::isfinite(seconds)) return 0;

  constexpr double kMaxSeconds =
      static_cast<double>(std::numeric_limits<qint64>::max()) / 1000.0;
  constexpr double kMinSeconds =
      static_cast<double>(std::numeric_limits<qint64>::min()) / 1000.0;
  if (seconds >= kMaxSeconds) return std::numeric_limits<qint64>::max();
  if (seconds <= kMinSeconds) return std::numeric_limits<qint64>::min();
  return static_cast<qint64>(std::llround(seconds * 1000.0));
}

inline qint64 clampedMillisecondsFromSeconds(double seconds, qint64 minMs, qint64 maxMs) {
  return std::clamp(millisecondsFromSeconds(seconds), minMs, maxMs);
}

} // namespace TimeConvert
