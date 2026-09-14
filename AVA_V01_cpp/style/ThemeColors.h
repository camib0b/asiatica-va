#pragma once

#include <QColor>

namespace Style {
namespace ThemeColors {

// Programmatic playhead highlight; distinct from QTableWidget selection in theme_light.qss.
inline QColor playheadHighlight(int alpha = 255) {
  return QColor(147, 197, 253, alpha);
}

} // namespace ThemeColors
} // namespace Style
