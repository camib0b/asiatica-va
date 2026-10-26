#include "TextFit.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QEvent>
#include <QFontMetrics>
#include <QPainter>
#include <QStringList>
#include <QStyle>
#include <QStyleOptionButton>
#include <QToolButton>

namespace Style {
namespace {

constexpr char kOwnsTooltipProperty[] = "textFitOwnsTooltip";
constexpr char kTunedProperty[] = "textFitTuned";

QString textWithoutMnemonic(const QString& text) {
  QString visible;
  visible.reserve(text.size());
  for (int index = 0; index < text.size(); ++index) {
    if (text.at(index) == QLatin1Char('&') && index + 1 < text.size()) {
      visible.append(text.at(index + 1));
      ++index;
      continue;
    }
    visible.append(text.at(index));
  }
  return visible;
}

int singleLineWidth(const QFontMetrics& metrics, const QString& text, int alignment) {
  return metrics.size(alignment | Qt::TextSingleLine, text).width();
}

bool textFitsWidth(const QFontMetrics& metrics, const QString& text, int availableWidth, int alignment) {
  if (availableWidth <= 0 || text.isEmpty()) return true;
  return singleLineWidth(metrics, text, alignment) <= availableWidth;
}

void publishOverflowTooltip(QWidget* widget, const QString& fullText, bool clipped) {
  if (!widget) return;
  const QString visibleText = textWithoutMnemonic(fullText);
  const QString currentTip = widget->toolTip();
  bool ownsTooltip = widget->property(kOwnsTooltipProperty).toBool();
  // A later owner (team-name tip, apply-button explanation) replaced the
  // overflow string. Drop the claim instead of clearing or overwriting it.
  if (ownsTooltip && currentTip != visibleText) {
    widget->setProperty(kOwnsTooltipProperty, false);
    ownsTooltip = false;
    if (!currentTip.isEmpty()) return;
  }
  if (clipped) {
    if (!currentTip.isEmpty() && !ownsTooltip) return;
    if (currentTip == visibleText && ownsTooltip) return;
    widget->setToolTip(visibleText);
    widget->setProperty(kOwnsTooltipProperty, true);
    return;
  }
  if (!ownsTooltip) return;
  widget->setToolTip(QString());
  widget->setProperty(kOwnsTooltipProperty, false);
}

void refreshButtonTooltip(QAbstractButton* button) {
  if (!button || button->text().isEmpty()) return;
  const bool clipped = !textFitsWidth(button->fontMetrics(),
                                      button->text(),
                                      availableButtonTextWidth(button),
                                      Qt::TextShowMnemonic);
  publishOverflowTooltip(button, button->text(), clipped);
}

void refreshComboTooltip(QComboBox* combo) {
  if (!combo || combo->isEditable()) return;
  // Item tips are the closed-box description (abbreviation shows the club name).
  if (!combo->currentData(Qt::ToolTipRole).toString().isEmpty()) {
    if (combo->property(kOwnsTooltipProperty).toBool()) {
      combo->setProperty(kOwnsTooltipProperty, false);
    }
    return;
  }
  combo->ensurePolished();
  QStyleOptionComboBox option;
  option.initFrom(combo);
  option.editable = false;
  option.currentText = combo->currentText();
  const QRect editField = combo->style()->subControlRect(QStyle::CC_ComboBox,
                                                         &option,
                                                         QStyle::SC_ComboBoxEditField,
                                                         combo);
  const bool clipped = !textFitsWidth(combo->fontMetrics(),
                                      combo->currentText(),
                                      editField.width(),
                                      Qt::AlignLeft | Qt::AlignVCenter);
  publishOverflowTooltip(combo, combo->currentText(), clipped);
}

void tuneButton(QAbstractButton* button) {
  if (!button || button->property(kTunedProperty).toBool()) return;
  if (button->text().isEmpty()) return;

  button->setProperty(kTunedProperty, true);
  QSizePolicy policy = button->sizePolicy();
  const auto horizontal = policy.horizontalPolicy();
  if (horizontal == QSizePolicy::Fixed || horizontal == QSizePolicy::Minimum) {
    policy.setHorizontalPolicy(QSizePolicy::Preferred);
    policy.setHorizontalStretch(0);
    button->setSizePolicy(policy);
  }
}

void tuneCombo(QComboBox* combo) {
  if (!combo || combo->property(kTunedProperty).toBool()) return;
  combo->setProperty(kTunedProperty, true);
  // Leave a view that opted out of elision (popup sized to the full strings) alone.
  // The default item view already elides when the popup is narrower than an entry.
  QSizePolicy policy = combo->sizePolicy();
  if (policy.horizontalPolicy() == QSizePolicy::Fixed ||
      policy.horizontalPolicy() == QSizePolicy::Minimum) {
    policy.setHorizontalPolicy(QSizePolicy::Preferred);
    combo->setSizePolicy(policy);
  }
}

class TextFitWatcher final : public QObject {
 public:
  explicit TextFitWatcher(QObject* parent) : QObject(parent) {}

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (!event) return false;
    switch (event->type()) {
      case QEvent::Polish:
      case QEvent::Show:
      case QEvent::Resize:
      case QEvent::FontChange:
      case QEvent::StyleChange:
      case QEvent::LayoutRequest:
        apply(watched);
        break;
      default:
        break;
    }
    return false;
  }

 private:
  static void apply(QObject* watched) {
    if (auto* button = qobject_cast<QAbstractButton*>(watched)) {
      tuneButton(button);
      refreshButtonTooltip(button);
      return;
    }
    if (auto* combo = qobject_cast<QComboBox*>(watched)) {
      tuneCombo(combo);
      refreshComboTooltip(combo);
    }
  }
};

TextFitWatcher* textFitWatcher = nullptr;

}  // namespace

ElidingLabel::ElidingLabel(QWidget* parent) : QLabel(parent) {
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
  setWordWrap(false);
}

ElidingLabel::ElidingLabel(const QString& fullText, QWidget* parent) : ElidingLabel(parent) {
  setFullText(fullText);
}

void ElidingLabel::setFullText(const QString& fullText) {
  if (fullText_ == fullText && text() == fullText) {
    updateOverflowTooltip();
    return;
  }
  fullText_ = fullText;
  QLabel::setText(fullText);
  updateGeometry();
  updateOverflowTooltip();
}

QSize ElidingLabel::sizeHint() const {
  ensurePolished();
  const QFontMetrics metrics(font());
  return QSize(metrics.horizontalAdvance(fullText_) + 1, metrics.lineSpacing());
}

QSize ElidingLabel::minimumSizeHint() const {
  ensurePolished();
  return QSize(0, fontMetrics().lineSpacing());
}

void ElidingLabel::resizeEvent(QResizeEvent* event) {
  QLabel::resizeEvent(event);
  updateOverflowTooltip();
}

void ElidingLabel::changeEvent(QEvent* event) {
  QLabel::changeEvent(event);
  if (!event) return;
  if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange ||
      event->type() == QEvent::LanguageChange) {
    updateOverflowTooltip();
  }
}

void ElidingLabel::updateOverflowTooltip() {
  const bool clipped = !textFitsWidth(fontMetrics(), fullText_, contentsRect().width(), alignment());
  publishOverflowTooltip(this, fullText_, clipped);
}

ElidingProxyStyle::ElidingProxyStyle(QStyle* baseStyle) : QProxyStyle(baseStyle) {}

void ElidingProxyStyle::drawItemText(QPainter* painter,
                                     const QRect& rectangle,
                                     int alignment,
                                     const QPalette& palette,
                                     bool enabled,
                                     const QString& text,
                                     QPalette::ColorRole textRole) const {
  QString shown = text;
  const bool wraps = alignment & (Qt::TextWordWrap | Qt::TextWrapAnywhere);
  if (painter && !wraps && !text.isEmpty() && !text.contains(QLatin1Char('\n')) && rectangle.width() > 0) {
    const QFontMetrics metrics = painter->fontMetrics();
    const int flags = alignment | Qt::TextSingleLine;
    if (singleLineWidth(metrics, text, flags) > rectangle.width()) {
      const QString elided = metrics.elidedText(text, Qt::ElideRight, rectangle.width(), flags);
      const int ellipsisWidth = metrics.horizontalAdvance(QStringLiteral("…"));
      // A single glyph narrower than the ellipsis should stay (icon buttons).
      if (!(elided == QStringLiteral("…") && singleLineWidth(metrics, text, flags) <= ellipsisWidth)) {
        shown = elided;
      }
    }
  }
  QProxyStyle::drawItemText(painter, rectangle, alignment, palette, enabled, shown, textRole);
}

namespace {

void chopOneCodePoint(QString* text) {
  if (!text || text->isEmpty()) return;
  if (text->size() >= 2 && text->back().isLowSurrogate() &&
      text->at(text->size() - 2).isHighSurrogate()) {
    text->chop(2);
    return;
  }
  text->chop(1);
}

QString elideToWidth(const QFontMetrics& metrics, const QString& text, int width) {
  if (text.isEmpty() || width <= 0) return {};
  if (metrics.horizontalAdvance(text) <= width) return text;

  const QString ellipsis = QString(QChar(0x2026));
  QString elided = metrics.elidedText(text, Qt::ElideRight, width);
  if (metrics.horizontalAdvance(elided) <= width) return elided;

  QString stem = elided;
  if (stem.endsWith(ellipsis)) stem.chop(1);
  while (!stem.isEmpty() && metrics.horizontalAdvance(stem + ellipsis) > width) {
    chopOneCodePoint(&stem);
  }
  if (stem.isEmpty()) {
    if (metrics.horizontalAdvance(ellipsis) <= width) return ellipsis;
    return {};
  }
  return stem + ellipsis;
}

QStringList wordsOf(const QString& text) {
  QStringList words;
  QString current;
  const int count = text.size();
  for (int index = 0; index < count; ++index) {
    const QChar character = text.at(index);
    if (character.isSpace() || character == QLatin1Char('\n') || character == QLatin1Char('\r')) {
      if (!current.isEmpty()) {
        words.append(current);
        current.clear();
      }
      continue;
    }
    current.append(character);
  }
  if (!current.isEmpty()) words.append(current);
  return words;
}

bool titleBoxFits(const QFontMetrics& metrics, const QString& text, int textWidthPx, int maxLines) {
  if (textWidthPx <= 0 || maxLines < 1) return false;
  const int maxHeight = metrics.lineSpacing() * maxLines;
  const QRect bounds = metrics.boundingRect(QRect(0, 0, textWidthPx, maxHeight + metrics.lineSpacing()),
                                            Qt::AlignHCenter | Qt::TextWordWrap, text);
  return bounds.width() <= textWidthPx + 1 && bounds.height() <= maxHeight + 1;
}

int textWidthForColumnCount(int availableWidthPx, int columnCount, int horizontalChromePx, int spacingPx) {
  if (columnCount < 1) return 0;
  const int gaps = spacingPx * (columnCount - 1);
  if (availableWidthPx <= gaps) return 0;
  const int buttonWidth = (availableWidthPx - gaps) / columnCount;
  return buttonWidth - horizontalChromePx;
}

}  // namespace

FittedButtonTitle fitButtonTitle(const QFontMetrics& metrics,
                                 const QString& fullText,
                                 int textWidthPx,
                                 int maxLines) {
  FittedButtonTitle result;
  result.shown = fullText;
  if (fullText.isEmpty() || textWidthPx <= 0 || maxLines < 1) return result;
  if (titleBoxFits(metrics, fullText, textWidthPx, maxLines)) return result;

  const QStringList words = wordsOf(fullText);
  if (words.isEmpty()) {
    result.shown = elideToWidth(metrics, fullText.simplified(), textWidthPx);
    result.elided = result.shown != fullText;
    return result;
  }

  QStringList lines;
  QString currentLine;
  int wordIndex = 0;
  while (wordIndex < words.size()) {
    const bool lastLine = lines.size() >= maxLines - 1;
    if (lastLine) {
      QStringList remainderWords = words.mid(wordIndex);
      if (!currentLine.isEmpty()) remainderWords.prepend(currentLine);
      const QString remainder = remainderWords.join(QLatin1Char(' '));
      const QString fitted = elideToWidth(metrics, remainder, textWidthPx);
      lines.append(fitted);
      if (fitted != remainder) result.elided = true;
      currentLine.clear();
      break;
    }

    const QString& word = words.at(wordIndex);
    const QString candidate = currentLine.isEmpty() ? word : currentLine + QLatin1Char(' ') + word;
    if (metrics.horizontalAdvance(candidate) <= textWidthPx) {
      currentLine = candidate;
      ++wordIndex;
      continue;
    }
    if (!currentLine.isEmpty()) {
      lines.append(currentLine);
      currentLine.clear();
      continue;
    }

    const QString fittedWord = elideToWidth(metrics, word, textWidthPx);
    lines.append(fittedWord);
    if (fittedWord != word) result.elided = true;
    ++wordIndex;
  }
  if (!currentLine.isEmpty()) lines.append(currentLine);

  if (lines.size() > maxLines) {
    const QString tail = lines.mid(qMax(0, maxLines - 1)).join(QLatin1Char(' '));
    lines = lines.mid(0, qMax(0, maxLines - 1));
    const QString fittedTail = elideToWidth(metrics, tail, textWidthPx);
    lines.append(fittedTail);
    if (fittedTail != tail) result.elided = true;
  }

  result.shown = lines.join(QLatin1Char('\n'));
  if (result.shown != fullText) {
    const bool onlyWrapped = !result.elided && wordsOf(result.shown) == words;
    if (!onlyWrapped) result.elided = true;
  }
  return result;
}

int fittingColumnCount(int availableWidthPx,
                       int buttonCount,
                       int horizontalChromePx,
                       int spacingPx,
                       const QFontMetrics& metrics,
                       const QStringList& labels,
                       int maxLines) {
  if (buttonCount <= 1) return 1;
  if (availableWidthPx <= 0) return buttonCount;

  const int singleColumnTextWidth =
      textWidthForColumnCount(availableWidthPx, 1, horizontalChromePx, spacingPx);
  auto fitsColumn = [&](const QString& label, int columnCount) {
    const int textWidth =
        textWidthForColumnCount(availableWidthPx, columnCount, horizontalChromePx, spacingPx);
    return !fitButtonTitle(metrics, label, textWidth, maxLines).elided;
  };

  for (int columnCount = buttonCount; columnCount >= 1; --columnCount) {
    bool everyFlexibleLabelFits = true;
    for (const QString& label : labels) {
      const bool fitsOnOneColumn = singleColumnTextWidth > 0 && fitsColumn(label, 1);
      if (!fitsOnOneColumn) continue;
      if (!fitsColumn(label, columnCount)) {
        everyFlexibleLabelFits = false;
        break;
      }
    }
    if (everyFlexibleLabelFits) return columnCount;
  }
  return 1;
}

int availableButtonTextWidth(const QAbstractButton* button) {
  if (!button) return 0;
  button->ensurePolished();

  QStyleOptionButton option;
  option.initFrom(button);
  const bool toolButton = qobject_cast<const QToolButton*>(button) != nullptr;
  const QSize chrome = button->style()->sizeFromContents(
      toolButton ? QStyle::CT_ToolButton : QStyle::CT_PushButton,
      &option,
      QSize(0, 0),
      button);
  int available = button->width() - chrome.width();
  if (!button->icon().isNull()) {
    const int spacing = button->style()->pixelMetric(QStyle::PM_ButtonMargin, nullptr, button);
    available -= button->iconSize().width() + qMax(4, spacing);
  }
  auto* menuButton = qobject_cast<const QToolButton*>(button);
  if (menuButton && menuButton->popupMode() != QToolButton::DelayedPopup) {
    available -= menuButton->style()->pixelMetric(QStyle::PM_MenuButtonIndicator, nullptr, menuButton);
  }
  return qMax(0, available);
}

void installTextFitWatcher() {
  if (!qApp || textFitWatcher) return;
  textFitWatcher = new TextFitWatcher(qApp);
  qApp->installEventFilter(textFitWatcher);
}

}  // namespace Style
