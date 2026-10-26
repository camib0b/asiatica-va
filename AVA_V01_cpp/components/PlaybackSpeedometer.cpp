#include "PlaybackSpeedometer.h"
#include "PlaybackRates.h"
#include "../style/TabularDigits.h"

#include <QEvent>
#include <QHideEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPointer>
#include <QSizePolicy>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace {
constexpr int kMinimumHeightPx = 20;
constexpr int kPreferredTrackWidthPx = 120;
constexpr int kTrackLeftMarginPx = 8;
constexpr int kReadoutGapPx = 10;
constexpr int kReadoutTextSlackPx = 6;
constexpr int kTrackHeightPx = 8;
constexpr int kThumbRadiusPx = 6;
constexpr int kFlashDurationMs = 150;
constexpr int kTickCount = static_cast<int>(PlaybackRates::kTicks.size());

QString formatRateText(double rate) {
  return QString::number(rate, 'f', 2) + QLatin1Char('x');
}
}  // namespace

PlaybackSpeedometer::PlaybackSpeedometer(QWidget* parent)
    : QWidget(parent),
      rate_(PlaybackRates::kResetRate),
      dragging_(false),
      pressedOnReadout_(false),
      flashing_(false) {
  setObjectName(QStringLiteral("PlaybackSpeedometer"));
  setFocusPolicy(Qt::NoFocus);
  setCursor(Qt::PointingHandCursor);
  setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  setAttribute(Qt::WA_Hover, true);
}

void PlaybackSpeedometer::setRate(double rate) {
  const double snapped = PlaybackRates::snap(rate);
  if (qFuzzyCompare(rate_, snapped)) {
    return;
  }
  rate_ = snapped;
  update();
}

QString PlaybackSpeedometer::displayedRateText() const {
  return formatRateText(rate_);
}

void PlaybackSpeedometer::flash() {
  setFlashing(true);

  auto* timer = findChild<QTimer*>(QStringLiteral("flashClearTimer"), Qt::FindDirectChildrenOnly);
  if (!timer) {
    timer = new QTimer(this);
    timer->setObjectName(QStringLiteral("flashClearTimer"));
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this,
            [this, self = QPointer<PlaybackSpeedometer>(this)]() {
              if (!self) {
                return;
              }
              self->setFlashing(false);
            });
  }
  timer->start(kFlashDurationMs);
}

int PlaybackSpeedometer::readoutWidthPx() const {
  return Style::tabularTextWidth(fontMetrics(), formatRateText(PlaybackRates::kTicks.back()))
         + kReadoutTextSlackPx;
}

int PlaybackSpeedometer::preferredHeightPx() const {
  return std::max(kMinimumHeightPx, fontMetrics().height() + 2);
}

QSize PlaybackSpeedometer::sizeHint() const {
  const int width = kTrackLeftMarginPx + kPreferredTrackWidthPx + kReadoutGapPx + readoutWidthPx();
  return QSize(width, preferredHeightPx());
}

QSize PlaybackSpeedometer::minimumSizeHint() const {
  return sizeHint();
}

QRect PlaybackSpeedometer::trackGrooveRect() const {
  const int grooveWidth = width() - kTrackLeftMarginPx - kReadoutGapPx - readoutWidthPx();
  const int top = (height() - kTrackHeightPx) / 2;
  return QRect(kTrackLeftMarginPx, top, std::max(0, grooveWidth), kTrackHeightPx);
}

QRect PlaybackSpeedometer::rateReadoutRect() const {
  const int readoutWidth = readoutWidthPx();
  return QRect(width() - readoutWidth, 0, readoutWidth, height());
}

bool PlaybackSpeedometer::isOnRateReadout(const QPoint& position) const {
  return rateReadoutRect().contains(position);
}

int PlaybackSpeedometer::indexFromX(int x) const {
  const QRect track = trackGrooveRect();
  if (track.width() <= 0) {
    return PlaybackRates::indexOfNearest(rate_);
  }
  const double clampedX = std::clamp(static_cast<double>(x),
                                     static_cast<double>(track.left()),
                                     static_cast<double>(track.right()));
  const double fraction = (clampedX - track.left()) / static_cast<double>(track.width());
  const int index = static_cast<int>(std::lround(fraction * (kTickCount - 1)));
  return std::clamp(index, 0, kTickCount - 1);
}

int PlaybackSpeedometer::xForIndex(int index) const {
  const QRect track = trackGrooveRect();
  const int clamped = std::clamp(index, 0, kTickCount - 1);
  if (kTickCount <= 1) {
    return track.center().x();
  }
  return track.left() + (clamped * track.width()) / (kTickCount - 1);
}

void PlaybackSpeedometer::applyIndexFromX(int x) {
  const double nextRate = PlaybackRates::atIndex(indexFromX(x));
  if (qFuzzyCompare(rate_, nextRate)) {
    return;
  }
  rate_ = nextRate;
  update();
  emit rateChanged(rate_);
}

void PlaybackSpeedometer::resetRate() {
  if (qFuzzyCompare(rate_, PlaybackRates::kResetRate)) {
    return;
  }
  rate_ = PlaybackRates::kResetRate;
  update();
  emit rateChanged(rate_);
}

void PlaybackSpeedometer::finishDrag() {
  if (!dragging_) {
    return;
  }
  dragging_ = false;
  pressedOnReadout_ = false;
  if (mouseGrabber() == this) {
    releaseMouse();
  }
  emit interactionFinished();
}

void PlaybackSpeedometer::setFlashing(bool flashing) {
  if (flashing_ == flashing) {
    return;
  }
  flashing_ = flashing;
  update();
}

void PlaybackSpeedometer::paintEvent(QPaintEvent* /*event*/) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);

  const bool enabled = isEnabled();
  const QRect track = trackGrooveRect();
  const int currentIndex = PlaybackRates::indexOfNearest(rate_);
  const int thumbX = xForIndex(currentIndex);
  const int fillWidth = std::max(0, thumbX - track.left());

  const int trackAlpha = enabled ? (flashing_ ? 70 : 40) : 20;
  const int fillAlpha = enabled ? (flashing_ ? 210 : 150) : 70;
  const int textAlpha = enabled ? 250 : 90;
  const int homeIndex = PlaybackRates::indexOfNearest(PlaybackRates::kResetRate);

  QPainterPath trackPath;
  trackPath.addRoundedRect(track, track.height() / 2.0, track.height() / 2.0);
  painter.fillPath(trackPath, QColor(255, 255, 255, trackAlpha));

  if (fillWidth > 0) {
    QRect fillRect = track;
    fillRect.setWidth(fillWidth);
    QPainterPath fillPath;
    fillPath.addRoundedRect(fillRect, track.height() / 2.0, track.height() / 2.0);
    painter.fillPath(fillPath, QColor(250, 250, 250, fillAlpha));
  }

  painter.setRenderHint(QPainter::Antialiasing, false);
  for (int index = 0; index < kTickCount; ++index) {
    const bool homeTick = index == homeIndex;
    const int tickHalfHeight = homeTick ? (kTrackHeightPx / 2 + 4) : (kTrackHeightPx / 2 + 2);
    const int tickAlpha = enabled ? (homeTick ? 200 : 120) : 50;
    const int tickX = xForIndex(index);
    const int centerY = track.center().y();
    const int tickTop = std::max(0, centerY - tickHalfHeight);
    const int tickBottom = std::min(height() - 1, centerY + tickHalfHeight);
    painter.setPen(QPen(QColor(250, 250, 250, tickAlpha), 1));
    painter.drawLine(tickX, tickTop, tickX, tickBottom);
  }

  painter.setRenderHint(QPainter::Antialiasing, true);
  const QPoint thumbCenter(thumbX, track.center().y());
  painter.setPen(Qt::NoPen);
  painter.setBrush(QColor(250, 250, 250, enabled ? 255 : 120));
  painter.drawEllipse(thumbCenter, kThumbRadiusPx, kThumbRadiusPx);

  const QRect textRect = rateReadoutRect().adjusted(2, 0, 0, 0);
  Style::drawTabularText(&painter, textRect, displayedRateText(), font(),
                         QColor(250, 250, 250, textAlpha));
}

void PlaybackSpeedometer::mousePressEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton || !isEnabled()) {
    QWidget::mousePressEvent(event);
    return;
  }
  const QPoint position = event->position().toPoint();
  if (isOnRateReadout(position)) {
    pressedOnReadout_ = true;
    event->accept();
    return;
  }
  pressedOnReadout_ = false;
  // A second press can arrive before the release (a rapid click, or the
  // platform delivering the rest of a double-click as another press). That
  // press is still the same drag: starting again would stop the controls
  // idle timer with no matching finish.
  const bool dragWasOpen = dragging_;
  dragging_ = true;
  if (mouseGrabber() != this) {
    grabMouse();
  }
  if (!dragWasOpen) {
    emit interactionStarted();
  }
  applyIndexFromX(position.x());
  event->accept();
}

void PlaybackSpeedometer::mouseMoveEvent(QMouseEvent* event) {
  if (!dragging_) {
    QWidget::mouseMoveEvent(event);
    return;
  }
  applyIndexFromX(event->position().toPoint().x());
  event->accept();
}

void PlaybackSpeedometer::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton) {
    QWidget::mouseReleaseEvent(event);
    return;
  }
  if (pressedOnReadout_ && !dragging_) {
    pressedOnReadout_ = false;
    event->accept();
    return;
  }
  if (!dragging_) {
    // The release that follows a double-click. Applying it would put the
    // dial back on the step under the cursor and undo a reset.
    event->accept();
    return;
  }
  applyIndexFromX(event->position().toPoint().x());
  finishDrag();
  event->accept();
}

void PlaybackSpeedometer::mouseDoubleClickEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton || !isEnabled()) {
    QWidget::mouseDoubleClickEvent(event);
    return;
  }
  // The default handler turns this event into another press. That press
  // selects the step under the cursor, and the release after the double-click
  // then commits it again. On the digits, that undoes the reset.
  const QPoint position = event->position().toPoint();
  pressedOnReadout_ = false;
  if (isOnRateReadout(position)) {
    resetRate();
  }
  finishDrag();
  event->accept();
}

void PlaybackSpeedometer::hideEvent(QHideEvent* event) {
  finishDrag();
  QWidget::hideEvent(event);
}

void PlaybackSpeedometer::changeEvent(QEvent* event) {
  QWidget::changeEvent(event);
  if (event->type() == QEvent::EnabledChange && !isEnabled()) {
    finishDrag();
  }
}
