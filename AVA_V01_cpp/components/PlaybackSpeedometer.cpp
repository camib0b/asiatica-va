#include "PlaybackSpeedometer.h"
#include "PlaybackRates.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QSizePolicy>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace {
constexpr int kPreferredWidthPx = 140;
constexpr int kPreferredHeightPx = 26;
constexpr int kTrackMarginXPx = 8;
constexpr int kTrackHeightPx = 8;
constexpr int kThumbRadiusPx = 6;
constexpr int kFlashDurationMs = 150;
constexpr int kTickCount = static_cast<int>(PlaybackRates::kTicks.size());
}  // namespace

PlaybackSpeedometer::PlaybackSpeedometer(QWidget* parent)
    : QWidget(parent),
      rate_(PlaybackRates::kResetRate),
      dragging_(false),
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

QSize PlaybackSpeedometer::sizeHint() const {
  return QSize(kPreferredWidthPx, kPreferredHeightPx);
}

QSize PlaybackSpeedometer::minimumSizeHint() const {
  return sizeHint();
}

QRect PlaybackSpeedometer::trackRect() const {
  const int top = (height() - kTrackHeightPx) / 2;
  return QRect(kTrackMarginXPx, top, width() - (2 * kTrackMarginXPx), kTrackHeightPx);
}

int PlaybackSpeedometer::indexFromX(int x) const {
  const QRect track = trackRect();
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
  const QRect track = trackRect();
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
  const QRect track = trackRect();
  const int currentIndex = PlaybackRates::indexOfNearest(rate_);
  const int thumbX = xForIndex(currentIndex);
  const int fillWidth = std::max(0, thumbX - track.left());

  const int trackAlpha = enabled ? (flashing_ ? 70 : 40) : 20;
  const int fillAlpha = enabled ? (flashing_ ? 210 : 150) : 70;
  const int textAlpha = enabled ? 250 : 90;

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

  const QPoint thumbCenter(thumbX, track.center().y());
  painter.setPen(Qt::NoPen);
  painter.setBrush(QColor(250, 250, 250, enabled ? 255 : 120));
  painter.drawEllipse(thumbCenter, kThumbRadiusPx, kThumbRadiusPx);

  painter.setPen(QColor(250, 250, 250, textAlpha));
  painter.setFont(font());
  const QString rateText = QString::number(rate_, 'f', 2) + QLatin1Char('x');
  painter.drawText(rect(), Qt::AlignCenter, rateText);
}

void PlaybackSpeedometer::mousePressEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton || !isEnabled()) {
    QWidget::mousePressEvent(event);
    return;
  }
  dragging_ = true;
  grabMouse();
  emit interactionStarted();
  applyIndexFromX(event->position().toPoint().x());
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
  if (!dragging_ || event->button() != Qt::LeftButton) {
    QWidget::mouseReleaseEvent(event);
    return;
  }
  dragging_ = false;
  releaseMouse();
  applyIndexFromX(event->position().toPoint().x());
  emit interactionFinished();
  event->accept();
}

void PlaybackSpeedometer::mouseDoubleClickEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton || !isEnabled()) {
    QWidget::mouseDoubleClickEvent(event);
    return;
  }
  emit interactionStarted();
  if (!qFuzzyCompare(rate_, PlaybackRates::kResetRate)) {
    rate_ = PlaybackRates::kResetRate;
    update();
    emit rateChanged(rate_);
  }
  event->accept();
}
