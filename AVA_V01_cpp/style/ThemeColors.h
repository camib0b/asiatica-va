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
inline QColor foreground() { return QColor(QStringLiteral("#09090b")); } // --foreground / zinc-950
inline QColor faint() { return QColor(QStringLiteral("#a1a1aa")); }  // QLabel[role="faint"]
inline QColor quietText() { return QColor(QStringLiteral("#71717a")); } // --muted-foreground
inline QColor border() { return QColor(QStringLiteral("#e4e4e7")); } // --border / zinc-200
inline QColor hoverBorder() { return QColor(QStringLiteral("#d4d4d8")); } // zinc-300
inline QColor surface() { return QColor(QStringLiteral("#ffffff")); }
inline QColor hoverSurface() { return QColor(QStringLiteral("#f4f4f5")); } // zinc-100
inline QColor pressedSurface() { return QColor(QStringLiteral("#e4e4e7")); }
inline QColor accent() { return QColor(QStringLiteral("#B08A2E")); } // QLabel[role="accent"]
inline QColor success() { return QColor(QStringLiteral("#48c78e")); } // clip trim start
inline QColor successWash() { return QColor(72, 199, 142, 88); }
inline QColor errorText() { return QColor(QStringLiteral("#b91c1c")); }
inline QColor transparent() { return QColor(0, 0, 0, 0); }
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
