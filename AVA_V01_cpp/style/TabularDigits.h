#pragma once

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QRect>
#include <QString>

namespace Style {

/// Width of one digit slot in \p fontMetrics. Qt 6.4 cannot enable the OpenType
/// `tnum` feature, so each digit is given the widest digit's advance and drawn
/// centered in that slot. The glyphs stay the theme font.
inline int tabularDigitSlotWidth(const QFontMetrics& fontMetrics) {
  int slotWidth = 0;
  const QString digits = QStringLiteral("0123456789");
  for (const QChar digit : digits) {
    slotWidth = qMax(slotWidth, fontMetrics.horizontalAdvance(digit));
  }
  return slotWidth;
}

/// Advance of \p text when digits use tabularDigitSlotWidth and other characters
/// (the colon in a timestamp) keep their natural width.
inline int tabularTextWidth(const QFontMetrics& fontMetrics, const QString& text) {
  const int digitSlotWidth = tabularDigitSlotWidth(fontMetrics);
  int width = 0;
  for (const QChar character : text) {
    if (character.isDigit()) {
      width += digitSlotWidth;
    } else {
      width += fontMetrics.horizontalAdvance(character);
    }
  }
  return width;
}

/// Draws \p text left-aligned and vertically centered. Digit glyphs sit in the
/// middle of a fixed slot so "09:59" and "10:00" occupy the same horizontal span.
inline void drawTabularText(QPainter* painter, const QRect& rectangle, const QString& text,
                            const QFont& font, const QColor& color) {
  if (!painter || text.isEmpty() || rectangle.isEmpty()) return;

  painter->save();
  painter->setFont(font);
  painter->setPen(color);
  painter->setClipRect(rectangle, Qt::IntersectClip);

  const QFontMetrics fontMetrics(font);
  const int digitSlotWidth = tabularDigitSlotWidth(fontMetrics);
  const int baseline = rectangle.top()
                       + (rectangle.height() + fontMetrics.ascent() - fontMetrics.descent()) / 2;

  int cursorX = rectangle.left();
  for (const QChar character : text) {
    if (character.isDigit()) {
      const int glyphWidth = fontMetrics.horizontalAdvance(character);
      const int glyphX = cursorX + (digitSlotWidth - glyphWidth) / 2;
      painter->drawText(QPoint(glyphX, baseline), QString(character));
      cursorX += digitSlotWidth;
    } else {
      painter->drawText(QPoint(cursorX, baseline), QString(character));
      cursorX += fontMetrics.horizontalAdvance(character);
    }
  }

  painter->restore();
}

} // namespace Style
