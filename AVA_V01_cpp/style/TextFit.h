#pragma once

#include <QLabel>
#include <QProxyStyle>
#include <QResizeEvent>
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

}  // namespace Style
