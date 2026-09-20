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
inline QColor border() { return QColor(QStringLiteral("#e4e4e7")); } // --border / zinc-200
inline QColor mutedForeground() { return QColor(QStringLiteral("#52525b")); } // zinc-600

// Quarter track segments (QPainter; QWidget QSS does not paint empty frames).
inline QColor quarterSegmentEmpty() { return border(); }
inline QColor quarterSegmentCurrent() { return mutedForeground(); }
inline QColor quarterSegmentComplete() { return ring(); }

// ClipTrimBar (presentation trim track; QPainter-only, not QWidget QSS).
inline QColor clipTrimTrackBackground() { return QColor(QStringLiteral("#323232")); }
inline QColor clipTrimMarkLine() { return QColor(255, 200, 50, 180); }
inline QColor clipTrimPlayhead() { return QColor(QStringLiteral("#ffffff")); }
inline QColor clipTrimStartHandle() { return QColor(QStringLiteral("#48c78e")); }
inline QColor clipTrimEndHandle() { return QColor(QStringLiteral("#f87171")); }

} // namespace ThemeColors
} // namespace Style
