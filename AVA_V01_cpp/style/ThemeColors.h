#pragma once

#include <QColor>
#include <QString>

namespace Style {
namespace ThemeColors {

// Programmatic playhead highlight; distinct from QTableWidget selection in theme_light.qss.
inline QColor playheadHighlight(int alpha = 255) {
  return QColor(147, 197, 253, alpha);
}

// Tokens mirrored from theme_light.qss for QPainter (QSS cannot stroke custom paint).
inline QColor ring() { return QColor(QStringLiteral("#18181b")); }   // --ring / zinc-900
inline QColor faint() { return QColor(QStringLiteral("#a1a1aa")); }  // QLabel[role="faint"]

} // namespace ThemeColors
} // namespace Style
