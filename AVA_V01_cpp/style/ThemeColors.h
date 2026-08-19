#pragma once

#include <QColor>

namespace Style {

// Programmatic highlights for playhead proximity, active clips, and trim ranges.
// Distinct from QTableWidget selection colors in theme_light.qss.
namespace ThemeColors {

inline constexpr int kPlayheadHighlightRed = 147;
inline constexpr int kPlayheadHighlightGreen = 197;
inline constexpr int kPlayheadHighlightBlue = 253;

inline QColor playheadHighlight(int alpha = 255) {
  return QColor(kPlayheadHighlightRed, kPlayheadHighlightGreen, kPlayheadHighlightBlue, alpha);
}

} // namespace ThemeColors
} // namespace Style
