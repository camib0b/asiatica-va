#pragma once

#include <QWidget>
#include <QGraphicsDropShadowEffect>
#include <QColor>
#include <algorithm> // std::clamp

namespace Style {

inline QGraphicsDropShadowEffect* ensureDropShadow(QWidget* w) {
  if (!w) return nullptr;

  if (auto* existing = qobject_cast<QGraphicsDropShadowEffect*>(w->graphicsEffect())) {
    return existing;
  }

  // Parent it; setGraphicsEffect takes ownership and will delete/replace old effects.
  auto* eff = new QGraphicsDropShadowEffect(w);
  w->setGraphicsEffect(eff);
  return eff;
}

inline void applyCardShadow(QWidget* w, int blur = 28, int yOffset = 8, int alpha = 40) {
  if (!w) return;

  auto* eff = ensureDropShadow(w);
  if (!eff) return;

  eff->setBlurRadius(blur);
  eff->setOffset(0, yOffset);
  eff->setColor(QColor(0, 0, 0, std::clamp(alpha, 0, 255)));
}

inline void clearShadow(QWidget* w) {
  if (!w) return;
  w->setGraphicsEffect(nullptr); // deletes the existing effect
}

} // namespace Style
