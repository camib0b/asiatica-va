#include "XmlDropHighlight.h"

#include "../style/ThemeColors.h"

#include <QPaintEvent>
#include <QPainter>

XmlDropHighlight::XmlDropHighlight(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("XmlDropHighlight"));
  setFocusPolicy(Qt::NoFocus);
  setAttribute(Qt::WA_TransparentForMouseEvents, true);
  setAttribute(Qt::WA_NoSystemBackground, true);
  setAttribute(Qt::WA_TranslucentBackground, true);
  setAutoFillBackground(false);
}

QColor XmlDropHighlight::strokeColor() {
  return Style::ThemeColors::foreground();
}

void XmlDropHighlight::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  QPainter painter(this);
  painter.setPen(Qt::NoPen);
  painter.setBrush(strokeColor());

  const QRect bounds = rect();
  const QRect band = bounds.adjusted(edgeInsetPx, edgeInsetPx, -edgeInsetPx, -edgeInsetPx);
  if (band.width() <= strokePx * 2 || band.height() <= strokePx * 2) return;

  painter.fillRect(QRect(band.left(), band.top(), band.width(), strokePx), strokeColor());
  painter.fillRect(QRect(band.left(), band.bottom() - strokePx + 1, band.width(), strokePx),
                   strokeColor());
  const int verticalHeight = band.height() - strokePx * 2;
  painter.fillRect(QRect(band.left(), band.top() + strokePx, strokePx, verticalHeight), strokeColor());
  painter.fillRect(
      QRect(band.right() - strokePx + 1, band.top() + strokePx, strokePx, verticalHeight),
      strokeColor());
}
