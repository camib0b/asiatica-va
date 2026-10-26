#pragma once

#include <QFontMetrics>
#include <QLabel>
#include <QProxyStyle>
#include <QResizeEvent>
#include <QStringList>
#include <QStyle>

class QAbstractButton;
class QEvent;

namespace Style {

/// Single-line label that asks for its full text, shrinks in a tight layout,
/// and keeps the full string available when the line is elided.
class ElidingLabel final : public QLabel {
 public:
  explicit ElidingLabel(QWidget* parent = nullptr);
  explicit ElidingLabel(const QString& fullText, QWidget* parent = nullptr);

  void setFullText(const QString& fullText);
  QString fullText() const { return fullText_; }

  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;

 protected:
  void resizeEvent(QResizeEvent* event) override;
  void changeEvent(QEvent* event) override;

 private:
  void updateOverflowTooltip();

  QString fullText_;
};

/// Fusion (or the platform fallback) with end-ellipsis for single-line text
/// that does not fit the rectangle it is drawn into.
class ElidingProxyStyle final : public QProxyStyle {
 public:
  explicit ElidingProxyStyle(QStyle* baseStyle);

  void drawItemText(QPainter* painter,
                    const QRect& rectangle,
                    int alignment,
                    const QPalette& palette,
                    bool enabled,
                    const QString& text,
                    QPalette::ColorRole textRole = QPalette::NoRole) const override;
};

/// Watches buttons and combo boxes created after the theme is applied.
void installTextFitWatcher();

/// Width left for a button or tool-button label after style padding, the icon,
/// and a visible menu indicator. Zero before the widget has a width.
int availableButtonTextWidth(const QAbstractButton* button);

/// Tagging-panel titles may use a second line. A third line means the label
/// should have been elided instead.
inline constexpr int kButtonTitleMaxLines = 2;

/// Text that fits a button title, plus whether the source had to be shortened.
struct FittedButtonTitle {
  /// Plain text. A newline is a hard line break when the source was elided
  /// onto more than one line. When nothing was cut, this is the source.
  QString shown;
  /// True when \p shown drops characters from \p fullText. Wrapping alone is
  /// not elision.
  bool elided = false;
};

/// Fits \p fullText into \p maxLines at \p textWidthPx without cutting a glyph.
///
/// Words stay whole when they fit. A word wider than the line, or text that
/// needs more than \p maxLines, ends with an ellipsis on the last visible
/// line. \p textWidthPx <= 0 means the widget has not been laid out yet: the
/// source is returned and \c elided stays false.
FittedButtonTitle fitButtonTitle(const QFontMetrics& metrics,
                                 const QString& fullText,
                                 int textWidthPx,
                                 int maxLines = kButtonTitleMaxLines);

/// Most columns at which every label that can fit in one column still fits
/// within \p maxLines. A label that cannot fit even at one column is elided
/// later and does not force the other labels into a single column.
/// Returns 1 when \p buttonCount is less than 2.
int fittingColumnCount(int availableWidthPx,
                       int buttonCount,
                       int horizontalChromePx,
                       int spacingPx,
                       const QFontMetrics& metrics,
                       const QStringList& labels,
                       int maxLines = kButtonTitleMaxLines);

}  // namespace Style
