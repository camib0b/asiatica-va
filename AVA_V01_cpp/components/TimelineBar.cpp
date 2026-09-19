#include "TimelineBar.h"
#include "../style/StyleProps.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QSlider>
#include <QTimer>
#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QStyle>
#include <QStyleOptionSlider>

#include <algorithm>
#include <array>
#include <limits>


namespace {
constexpr qint64 kScrubThrottleMs = 33; // ~30Hz
constexpr qint64 kSeekCommitToleranceMs = 250;
constexpr qint64 kSeekCommitTimeoutMs = 2000;
constexpr qint64 kMaxTimelineSeconds = std::numeric_limits<qint64>::max() / 1000;

bool parseNonNegativeInt64(const QString& text, qint64* outValue) {
  if (!outValue || text.isEmpty()) return false;
  for (const QChar ch : text) {
    if (ch < QLatin1Char('0') || ch > QLatin1Char('9')) return false;
  }
  bool ok = false;
  const qint64 value = text.toLongLong(&ok);
  if (!ok || value < 0) return false;
  *outValue = value;
  return true;
}

bool addTimeUnits(qint64* totalSeconds, qint64 value, qint64 secondsPerUnit) {
  if (!totalSeconds || value < 0 || secondsPerUnit <= 0) return false;
  if (value > 0 && secondsPerUnit > kMaxTimelineSeconds / value) return false;
  const qint64 increment = value * secondsPerUnit;
  if (*totalSeconds > kMaxTimelineSeconds - increment) return false;
  *totalSeconds += increment;
  return true;
}

int sliderMaximumForDurationMs(qint64 durationMs) {
  if (durationMs <= 0) return 0;
  if (durationMs <= std::numeric_limits<int>::max()) {
    return static_cast<int>(durationMs);
  }
  return std::numeric_limits<int>::max();
}

int sliderValueForPositionMs(qint64 positionMs, qint64 durationMs) {
  if (durationMs <= 0) return 0;
  const qint64 clampedPositionMs = std::max<qint64>(0, std::min(positionMs, durationMs));
  if (durationMs <= std::numeric_limits<int>::max()) {
    return static_cast<int>(clampedPositionMs);
  }
  return static_cast<int>((clampedPositionMs * static_cast<qint64>(std::numeric_limits<int>::max()))
                          / durationMs);
}

qint64 positionMsForSliderValue(int sliderValue, qint64 durationMs) {
  if (durationMs <= 0) return 0;
  if (durationMs <= std::numeric_limits<int>::max()) {
    return static_cast<qint64>(sliderValue);
  }
  return (static_cast<qint64>(sliderValue) * durationMs) / std::numeric_limits<int>::max();
}

// Click-to-seek slider (keeps your "real player" feel).
class ClickSeekSlider final : public QSlider {
public:
  explicit ClickSeekSlider(Qt::Orientation o, QWidget* parent = nullptr)
    : QSlider(o, parent) {}

protected:
  void mousePressEvent(QMouseEvent* e) override {
    if (e->button() == Qt::LeftButton) {
      QStyleOptionSlider opt;
      initStyleOption(&opt);

      const QPoint p = e->position().toPoint();
      const QRect handleRect =
        style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderHandle, this);
      const QRect grooveRect =
        style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, this);

      if (!handleRect.contains(p)) {
        const int positionInGroove = (orientation() == Qt::Horizontal)
            ? p.x() - grooveRect.x()
            : p.y() - grooveRect.y();
        const int grooveSpan = (orientation() == Qt::Horizontal)
            ? grooveRect.width()
            : grooveRect.height();
        const int v = QStyle::sliderValueFromPosition(
            minimum(), maximum(), positionInGroove, grooveSpan, opt.upsideDown);
        setValue(v);
        // Let the default behavior continue; we'll treat this as an immediate scrub-release.
      }
    }
    QSlider::mousePressEvent(e);
  }
};
} // namespace

TimelineBar::TimelineBar(QWidget* parent)
  : QWidget(parent) {
  buildUi();
  wireSignals();
  reset();
}

void TimelineBar::buildUi() {
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  auto* layout = new QHBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(10);
  layout->setAlignment(Qt::AlignVCenter);

  slider_ = new ClickSeekSlider(Qt::Horizontal, this);
  slider_->setObjectName("TimelineSlider");
  slider_->setRange(0, 0);
  slider_->setSingleStep(50);
  slider_->setPageStep(1000);
  slider_->setTracking(false); // you already found this feels better
  slider_->setMinimumHeight(40);
  slider_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

  label_ = new QLabel("00:00 / 00:00", this);
  Style::setRole(label_, "muted");
  label_->setMinimumWidth(160);
  label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  label_->setCursor(Qt::IBeamCursor);
  label_->installEventFilter(this);

  timeEntry_ = new QLineEdit(this);
  timeEntry_->setMinimumWidth(160);
  timeEntry_->setAlignment(Qt::AlignRight);
  timeEntry_->setPlaceholderText(QStringLiteral("mm:ss"));
  Style::setRole(timeEntry_, "muted");
  timeEntry_->hide();
  timeEntry_->installEventFilter(this);

  layout->addWidget(slider_, 1);
  layout->addWidget(label_);
  layout->addWidget(timeEntry_);
}

void TimelineBar::wireSignals() {
  scrubSeekThrottleTimer_ = new QTimer(this);
  scrubSeekThrottleTimer_->setSingleShot(true);
  scrubSeekThrottleTimer_->setInterval(static_cast<int>(kScrubThrottleMs));
  connect(scrubSeekThrottleTimer_, &QTimer::timeout, this, [this]() {
    if (pendingScrubSeekMs_ < 0) return;
    emit scrubSeekTo(pendingScrubSeekMs_);
    pendingScrubSeekMs_ = -1;
  });

  connect(slider_, &QSlider::sliderPressed, this, [this]() {
    isScrubbing_ = true;
    clearSeekCommitWait();
    pendingScrubSeekMs_ = -1;
    scrubSeekThrottleTimer_->stop();
    emit scrubStarted();
  });

  connect(slider_, &QSlider::sliderMoved, this, [this](int value) {
    const qint64 posMs = positionMsForSliderValue(value, durationMs_);
    updateLabel(posMs, durationMs_);

    pendingScrubSeekMs_ = posMs;
    if (!scrubSeekThrottleTimer_->isActive()) {
      scrubSeekThrottleTimer_->start();
    }
  });

  connect(slider_, &QSlider::sliderReleased, this, [this]() {
    const qint64 releasedPosMs = positionMsForSliderValue(slider_->value(), durationMs_);
    beginSeekCommitWait(releasedPosMs);
    pendingScrubSeekMs_ = -1;
    scrubSeekThrottleTimer_->stop();
    isScrubbing_ = false;
    lastKnownPositionMs_ = releasedPosMs;
    updateLabel(releasedPosMs, durationMs_);
    emit scrubFinished(releasedPosMs);
  });

  connect(timeEntry_, &QLineEdit::returnPressed, this, [this]() {
    commitTimeEntry();
  });

  connect(timeEntry_, &QLineEdit::editingFinished, this, [this]() {
    commitTimeEntry();
  });

  // If user clicks somewhere (not dragging), sliderMoved might not fire.
  connect(slider_, &QSlider::valueChanged, this, [this](int v) {
    if (isScrubbing_) return;
    // Keep label in sync even for click-to-seek
    updateLabel(positionMsForSliderValue(v, durationMs_), durationMs_);
  });
}

void TimelineBar::reset() {
  durationMs_ = 0;
  lastKnownPositionMs_ = 0;
  isScrubbing_ = false;
  clearSeekCommitWait();
  pendingScrubSeekMs_ = -1;
  lastDisplayedPosSeconds_ = -1;
  lastDisplayedDurSeconds_ = -1;
  if (scrubSeekThrottleTimer_) scrubSeekThrottleTimer_->stop();
  abandonTimeEntryEditing();
  slider_->setRange(0, 0);
  slider_->setValue(0);
  slider_->setEnabled(false);
  updateLabel(0, 0);
}

void TimelineBar::setEnabledForMedia(bool on) {
  slider_->setEnabled(on && durationMs_ > 0);
  if (!slider_->isEnabled() && isEditingTimeEntry_) {
    cancelTimeEntry();
  }
}

void TimelineBar::setDurationMs(qint64 durMs) {
  durationMs_ = std::max<qint64>(0, durMs);
  slider_->setRange(0, sliderMaximumForDurationMs(durationMs_));
  slider_->setEnabled(durationMs_ > 0);
  updateLabel(positionMsForSliderValue(slider_->value(), durationMs_), durationMs_);
}

void TimelineBar::beginSeekCommitWait(qint64 pendingMs) {
  waitingForSeekCommit_ = true;
  pendingSeekMs_ = pendingMs;
  seekCommitElapsed_.restart();
}

void TimelineBar::clearSeekCommitWait() {
  waitingForSeekCommit_ = false;
  pendingSeekMs_ = -1;
  seekCommitElapsed_.invalidate();
}

void TimelineBar::setPositionMs(qint64 posMs) {
  posMs = std::max<qint64>(0, std::min(posMs, durationMs_));
  if (waitingForSeekCommit_ && pendingSeekMs_ >= 0) {
    const bool seekCommitted = qAbs(posMs - pendingSeekMs_) <= kSeekCommitToleranceMs;
    const bool waitTimedOut = !seekCommitElapsed_.isValid()
        || seekCommitElapsed_.elapsed() >= kSeekCommitTimeoutMs;
    if (!seekCommitted && !waitTimedOut) {
      lastKnownPositionMs_ = pendingSeekMs_;
      updateLabel(pendingSeekMs_, durationMs_);
      return;
    }
    clearSeekCommitWait();
  }
  if (!isScrubbing_) {
    slider_->setValue(sliderValueForPositionMs(posMs, durationMs_));
  }
  lastKnownPositionMs_ = posMs;
  updateLabel(posMs, durationMs_);
}

void TimelineBar::updateLabel(qint64 posMs, qint64 durMs) {
  if (isEditingTimeEntry_) return;

  // The timestamp is formatted at second resolution, so skip QString work and
  // QLabel::setText unless the visible text would actually change.
  const qint64 posSeconds = std::max<qint64>(0, posMs) / 1000;
  const qint64 durSeconds = std::max<qint64>(0, durMs) / 1000;
  if (posSeconds == lastDisplayedPosSeconds_ && durSeconds == lastDisplayedDurSeconds_) return;
  lastDisplayedPosSeconds_ = posSeconds;
  lastDisplayedDurSeconds_ = durSeconds;

  label_->setText(QString("%1 / %2").arg(formatMs(posMs), formatMs(durMs)));
}

bool TimelineBar::eventFilter(QObject* watched, QEvent* event) {
  if (watched == label_ && event && event->type() == QEvent::MouseButtonPress) {
    auto* mouse = static_cast<QMouseEvent*>(event);
    if (mouse->button() == Qt::LeftButton) {
      beginTimeEntry();
      return true;
    }
  }

  if (watched == timeEntry_ && event && event->type() == QEvent::KeyPress) {
    auto* key = static_cast<QKeyEvent*>(event);
    if (key->key() == Qt::Key_Escape) {
      cancelTimeEntry();
      return true;
    }
  }

  return QWidget::eventFilter(watched, event);
}

void TimelineBar::restoreTimeEntryUi() {
  if (!timeEntry_ || !label_) return;
  QSignalBlocker timeEntrySignals(timeEntry_);
  timeEntry_->hide();
  label_->show();
}

void TimelineBar::abandonTimeEntryEditing() {
  if (!isEditingTimeEntry_) return;
  timeEntryTransition_ = true;
  isEditingTimeEntry_ = false;
  restoreTimeEntryUi();
  timeEntryTransition_ = false;
}

void TimelineBar::beginTimeEntry() {
  if (!slider_ || !timeEntry_ || !label_) return;
  if (timeEntryTransition_ || isEditingTimeEntry_) return;
  if (!slider_->isEnabled() || durationMs_ <= 0) return;

  timeEntryTransition_ = true;
  isEditingTimeEntry_ = true;
  clearSeekCommitWait();

  const qint64 currentMs = positionMsForSliderValue(slider_->value(), durationMs_);
  timeEntry_->setText(formatMs(currentMs));
  label_->hide();
  timeEntry_->show();
  timeEntry_->setFocus(Qt::MouseFocusReason);
  timeEntry_->selectAll();
  timeEntryTransition_ = false;

  emit timeEntryStarted();
}

void TimelineBar::commitTimeEntry() {
  if (!isEditingTimeEntry_ || timeEntryTransition_ || !timeEntry_ || !label_ || !slider_) return;

  qint64 targetMs = -1;
  if (!parseTimeEntryMs(timeEntry_->text(), &targetMs)) {
    cancelTimeEntry();
    return;
  }

  timeEntryTransition_ = true;
  targetMs = std::max<qint64>(0, std::min(targetMs, durationMs_));
  beginSeekCommitWait(targetMs);
  isEditingTimeEntry_ = false;
  restoreTimeEntryUi();

  slider_->setValue(sliderValueForPositionMs(targetMs, durationMs_));
  lastKnownPositionMs_ = targetMs;
  updateLabel(targetMs, durationMs_);
  timeEntryTransition_ = false;

  emit timeEntryFinished(targetMs);
  emit scrubFinished(targetMs);
}

void TimelineBar::cancelTimeEntry(bool notify) {
  if (!isEditingTimeEntry_ || timeEntryTransition_ || !timeEntry_ || !label_) return;

  timeEntryTransition_ = true;
  isEditingTimeEntry_ = false;
  restoreTimeEntryUi();
  updateLabel(lastKnownPositionMs_, durationMs_);
  timeEntryTransition_ = false;

  if (notify) {
    emit timeEntryCancelled();
  }
}

bool TimelineBar::parseTimeEntryMs(const QString& text, qint64* outMs) {
  if (!outMs) return false;
  const QString trimmed = text.trimmed();
  if (trimmed.isEmpty()) return false;

  qint64 hours = 0;
  qint64 minutes = 0;
  qint64 seconds = 0;

  if (trimmed.contains(QLatin1Char(':'))) {
    const QStringList parts = trimmed.split(QLatin1Char(':'), Qt::KeepEmptyParts);
    if (parts.size() < 2 || parts.size() > 3) return false;

    std::array<qint64, 3> components{};
    const int partCount = parts.size();
    for (int i = 0; i < partCount; ++i) {
      const QString part = parts.at(i).trimmed();
      if (part.isEmpty()) return false;
      if (!parseNonNegativeInt64(part, &components[i])) return false;
    }

    if (partCount == 3) {
      hours = components[0];
      minutes = components[1];
      seconds = components[2];
      if (minutes > 59 || seconds > 59) return false;
    } else {
      minutes = components[0];
      seconds = components[1];
      if (seconds > 59) return false;
    }
  } else {
    if (trimmed.size() <= 2) {
      if (!parseNonNegativeInt64(trimmed, &seconds)) return false;
    } else if (trimmed.size() <= 4) {
      if (!parseNonNegativeInt64(trimmed.left(trimmed.size() - 2), &minutes)) return false;
      if (!parseNonNegativeInt64(trimmed.right(2), &seconds)) return false;
      if (seconds > 59) return false;
    } else {
      if (!parseNonNegativeInt64(trimmed.left(trimmed.size() - 4), &hours)) return false;
      if (!parseNonNegativeInt64(trimmed.mid(trimmed.size() - 4, 2), &minutes)) return false;
      if (!parseNonNegativeInt64(trimmed.right(2), &seconds)) return false;
      if (minutes > 59 || seconds > 59) return false;
    }
  }

  qint64 totalSeconds = 0;
  if (!addTimeUnits(&totalSeconds, hours, 3600)) return false;
  if (!addTimeUnits(&totalSeconds, minutes, 60)) return false;
  if (!addTimeUnits(&totalSeconds, seconds, 1)) return false;

  *outMs = totalSeconds * 1000;
  return true;
}

QString TimelineBar::formatMs(qint64 ms) {
  ms = std::max<qint64>(0, ms);
  const qint64 totalSeconds = ms / 1000;
  const qint64 hours = totalSeconds / 3600;
  const qint64 minutes = (totalSeconds / 60) % 60;
  const qint64 seconds = totalSeconds % 60;

  if (hours > 0) {
    return QStringLiteral("%1:%2:%3")
        .arg(hours)
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'));
  }

  const qint64 minutesTotal = totalSeconds / 60;
  return QStringLiteral("%1:%2")
      .arg(minutesTotal, 2, 10, QChar('0'))
      .arg(seconds, 2, 10, QChar('0'));
}

