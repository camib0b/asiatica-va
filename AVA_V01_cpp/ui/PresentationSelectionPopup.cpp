#include "PresentationSelectionPopup.h"

#include "../style/TabularDigits.h"
#include "QtPtr.h"

#include <QFontMetrics>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QTextLayout>
#include <QTextOption>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>

namespace {

constexpr int kMaxVisibleHeightPx = 240;
constexpr int kMinimumRowHeightPx = 32;
constexpr int kMaxWrappedLines = 6;
constexpr int kRowHorizontalMarginPx = 8;
constexpr int kRowVerticalMarginPx = 6;
constexpr int kRowSpacingPx = 6;

struct WrappedText {
  QString visible;
  int height = 0;
  bool clamped = false;
};

// QTextLayout keeps U+000A in the line. A label that was renamed with a
// newline would otherwise measure and paint as a single line.
QString withExplicitLineBreaks(QString text) {
  text.replace(QLatin1String("\r\n"), QString(QChar::LineSeparator));
  text.replace(QLatin1Char('\n'), QChar::LineSeparator);
  text.replace(QLatin1Char('\r'), QChar::LineSeparator);
  return text;
}

WrappedText wrapPlainText(const QString& text, const QFont& font, int width, int maxLines) {
  const QFontMetrics metrics(font);
  WrappedText result;
  result.height = metrics.height();
  if (text.isEmpty()) return result;

  const QString laidOut = withExplicitLineBreaks(text);
  const int lineWidth = qMax(1, width);
  QTextOption option;
  option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
  option.setAlignment(Qt::AlignLeft | Qt::AlignTop);

  QTextLayout layout(laidOut, font);
  layout.setTextOption(option);
  layout.beginLayout();
  qreal y = 0;
  int lineCount = 0;
  int lastFullStart = 0;
  while (true) {
    QTextLine line = layout.createLine();
    if (!line.isValid()) break;
    line.setLineWidth(lineWidth);
    line.setPosition(QPointF(0, y));
    if (lineCount < maxLines) {
      y += line.height();
      lastFullStart = line.textStart();
    }
    ++lineCount;
  }
  layout.endLayout();

  result.height = qMax(metrics.height(), static_cast<int>(std::ceil(y)));
  if (lineCount <= maxLines) {
    result.visible = laidOut;
    return result;
  }

  result.clamped = true;
  const QString head = laidOut.left(lastFullStart);
  const QString tail = laidOut.mid(lastFullStart);
  result.visible = head + metrics.elidedText(tail, Qt::ElideRight, lineWidth);
  return result;
}

/// Event name. Wraps at word boundaries, and mid-word only when the token
/// itself is wider than the column. Stops at six lines and keeps the full
/// string for the tooltip.
class PlainWrapLabel final : public QWidget {
 public:
  explicit PlainWrapLabel(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("PresentationSelectionEventName"));
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setMinimumWidth(0);
  }

  void setWrapText(const QString& text, int width) {
    fullText_ = text;
    wrapWidth_ = qMax(1, width);
    ensurePolished();
    const WrappedText wrapped = wrapPlainText(fullText_, font(), wrapWidth_, kMaxWrappedLines);
    visibleText_ = wrapped.visible;
    clamped_ = wrapped.clamped;
    setFixedSize(wrapWidth_, wrapped.height);
    setProperty("fullText", fullText_);
    setProperty("clamped", clamped_);
    update();
  }

  QString fullText() const { return fullText_; }
  bool clamped() const { return clamped_; }

 protected:
  void paintEvent(QPaintEvent* event) override {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setFont(font());
    painter.setPen(palette().color(QPalette::WindowText));
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    option.setAlignment(Qt::AlignLeft | Qt::AlignTop);
    painter.drawText(rect(), visibleText_, option);
  }

 private:
  QString fullText_;
  QString visibleText_;
  int wrapWidth_ = 1;
  bool clamped_ = false;
};

class TabularCountLabel final : public QWidget {
 public:
  explicit TabularCountLabel(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("PresentationSelectionEventCount"));
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  }

  void setCount(int count, int columnWidth) {
    text_ = QString::number(count);
    ensurePolished();
    const int textWidth = Style::tabularTextWidth(fontMetrics(), text_);
    const int width = qMax(columnWidth, textWidth);
    setFixedSize(width, qMax(fontMetrics().height(), fontMetrics().lineSpacing()));
    setProperty("countText", text_);
    update();
  }

 protected:
  void paintEvent(QPaintEvent* event) override {
    Q_UNUSED(event);
    QPainter painter(this);
    const int textWidth = Style::tabularTextWidth(fontMetrics(), text_);
    const QRect area(qMax(0, width() - textWidth), 0, textWidth, height());
    Style::drawTabularText(&painter, area, text_, font(), palette().color(QPalette::WindowText));
  }

 private:
  QString text_;
};

class SelectionEventRow final : public QFrame {
 public:
  SelectionEventRow(const PresentationSelectionBreakdown::EventCount& event, int rowWidth,
                    int countColumnWidth, QWidget* parent)
      : QFrame(parent), eventKey_(event.eventKey) {
    setObjectName(QStringLiteral("PresentationSelectionEventRow"));
    setFrameShape(QFrame::NoFrame);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_Hover, true);
    setAttribute(Qt::WA_StyledBackground, true);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setFixedWidth(rowWidth);

    setProperty("eventId", static_cast<int>(event.eventKey.id));
    setProperty("unlistedName", event.eventKey.unlistedName);
    setProperty("displayLabel", event.displayLabel);
    setProperty("selectedCount", event.selectedCount);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(kRowHorizontalMarginPx, kRowVerticalMarginPx, kRowHorizontalMarginPx,
                               kRowVerticalMarginPx);
    layout->setSpacing(kRowSpacingPx);

    auto dash = makeQtPtr<QLabel>(QStringLiteral("—"), this);
    dash->setObjectName(QStringLiteral("PresentationSelectionEventDash"));
    dash->setTextFormat(Qt::PlainText);
    dash->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    dash->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    dash->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    dash->setMinimumWidth(0);
    dash->ensurePolished();
    dash->setFixedSize(dash->fontMetrics().horizontalAdvance(dash->text()),
                       qMax(dash->fontMetrics().height(), dash->fontMetrics().lineSpacing()));

    auto count = makeQtPtr<TabularCountLabel>(this);
    count->setCount(event.selectedCount, countColumnWidth);

    // name, stretch, dash, count: three gaps, even when the stretch is empty.
    const int chrome = (kRowHorizontalMarginPx * 2) + (kRowSpacingPx * 3) + dash->width() +
                       count->width();
    const int nameWidth = qMax(32, rowWidth - chrome);

    auto name = makeQtPtr<PlainWrapLabel>(this);
    name->setWrapText(event.displayLabel, nameWidth);

    layout->addWidget(name.get(), 0, Qt::AlignTop);
    layout->addStretch(1);
    layout->addWidget(dash.get(), 0, Qt::AlignTop);
    layout->addWidget(count.get(), 0, Qt::AlignTop);

    const int contentHeight = std::max(name->height(), std::max(dash->height(), count->height()));
    const int rowHeight = qMax(kMinimumRowHeightPx, contentHeight + kRowVerticalMarginPx * 2);
    setFixedHeight(rowHeight);

    const QString accessible = event.displayLabel + QStringLiteral(" — ") +
                               QString::number(event.selectedCount);
    setAccessibleName(accessible);
    setProperty("clamped", name->clamped());
    if (name->clamped()) setToolTip(event.displayLabel.toHtmlEscaped());
  }

  TagEvent::MainEventKey eventKey() const { return eventKey_; }

  std::function<void()> onActivated;

 protected:
  void mousePressEvent(QMouseEvent* event) override {
    if (event->button() == Qt::LeftButton) {
      pressed_ = true;
      event->accept();
      return;
    }
    QFrame::mousePressEvent(event);
  }

  void mouseReleaseEvent(QMouseEvent* event) override {
    const bool activate = pressed_ && event->button() == Qt::LeftButton && rect().contains(event->pos());
    pressed_ = false;
    if (activate && onActivated) {
      onActivated();
      return;
    }
    QFrame::mouseReleaseEvent(event);
  }

 private:
  TagEvent::MainEventKey eventKey_;
  bool pressed_ = false;
};

int countColumnWidth(const QFontMetrics& metrics,
                     const QVector<PresentationSelectionBreakdown::EventCount>& events) {
  int width = 0;
  for (const PresentationSelectionBreakdown::EventCount& event : events) {
    width = qMax(width, Style::tabularTextWidth(metrics, QString::number(event.selectedCount)));
  }
  return width;
}

}  // namespace

PresentationSelectionPopup::PresentationSelectionPopup(QWidget* parent)
    : QFrame(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus),
      listHost_(nullptr),
      listLayout_(nullptr),
      scrollArea_(nullptr) {
  setObjectName(QStringLiteral("PresentationSelectionPopup"));
  setAttribute(Qt::WA_ShowWithoutActivating, true);
  setAttribute(Qt::WA_StyledBackground, true);
  setFocusPolicy(Qt::NoFocus);
  setFrameShape(QFrame::NoFrame);

  auto scroll = makeQtPtr<QScrollArea>(this);
  scrollArea_ = scroll.get();
  scrollArea_->setObjectName(QStringLiteral("PresentationSelectionEventScroll"));
  scrollArea_->setFrameShape(QFrame::NoFrame);
  scrollArea_->setWidgetResizable(false);
  scrollArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scrollArea_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  scrollArea_->setFocusPolicy(Qt::NoFocus);
  if (scrollArea_->viewport()) scrollArea_->viewport()->setAutoFillBackground(false);

  auto host = makeQtPtr<QWidget>();
  listHost_ = host.get();
  listHost_->setObjectName(QStringLiteral("PresentationSelectionEventList"));
  listLayout_ = new QVBoxLayout(listHost_);
  listLayout_->setContentsMargins(4, 4, 4, 4);
  listLayout_->setSpacing(2);
  scrollArea_->setWidget(listHost_);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->addWidget(scrollArea_);
}

void PresentationSelectionPopup::setActivationHandler(
    std::function<void(const TagEvent::MainEventKey&)> handler) {
  activationHandler_ = std::move(handler);
}

void PresentationSelectionPopup::clearRows() {
  if (!listLayout_) return;
  while (listLayout_->count() > 0) {
    QLayoutItem* item = listLayout_->takeAt(0);
    if (item->widget()) unparentAndDelete(item->widget());
    delete item;
  }
}

void PresentationSelectionPopup::setEvents(
    const QVector<PresentationSelectionBreakdown::EventCount>& events, int popupWidth) {
  const QVector<PresentationSelectionBreakdown::EventCount> rows =
      PresentationSelectionBreakdown::inCatalogOrder(events);

  setFixedWidth(qMax(1, popupWidth));
  ensurePolished();
  int innerWidth = contentsRect().width();
  if (innerWidth <= 0) innerWidth = width();

  const auto fill = [&](bool reserveScrollBar) {
    clearRows();
    const int scrollBarWidth =
        reserveScrollBar ? style()->pixelMetric(QStyle::PM_ScrollBarExtent) : 0;
    const int hostWidth = qMax(1, innerWidth - scrollBarWidth);
    listHost_->setFixedWidth(hostWidth);

    const QMargins listMargins = listLayout_->contentsMargins();
    const int rowWidth = qMax(1, hostWidth - listMargins.left() - listMargins.right());
    const QFontMetrics metrics(font());
    const int columnWidth = countColumnWidth(metrics, rows);

    int contentHeight = listMargins.top() + listMargins.bottom();
    bool placedRow = false;
    for (const PresentationSelectionBreakdown::EventCount& event : rows) {
      auto row = makeQtPtr<SelectionEventRow>(event, rowWidth, columnWidth, listHost_);
      row->onActivated = [this, key = event.eventKey]() {
        if (activationHandler_) activationHandler_(key);
      };
      if (placedRow) contentHeight += listLayout_->spacing();
      contentHeight += row->height();
      placedRow = true;
      listLayout_->addWidget(row.get());
    }
    listHost_->setFixedHeight(qMax(1, contentHeight));
    return contentHeight;
  };

  int contentHeight = fill(false);
  if (contentHeight > kMaxVisibleHeightPx) contentHeight = fill(true);

  const int visibleHeight = qMin(contentHeight, kMaxVisibleHeightPx);
  scrollArea_->setFixedHeight(visibleHeight);
  const int frameHeight = contentsMargins().top() + contentsMargins().bottom();
  setFixedHeight(visibleHeight + frameHeight);
}
