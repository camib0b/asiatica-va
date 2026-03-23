#include "TimelineBar.h"
#include "../style/StyleProps.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSlider>
#include <QElapsedTimer>
#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QStyle>
#include <QStyleOptionSlider>


namespace {
constexpr qint64 kScrubThrottleMs = 33; // ~30Hz
constexpr qint64 kSeekCommitToleranceMs = 250;

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

      const QRect handleRect =
        style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderHandle, this);

      const QPoint p = e->position().toPoint();
      const bool clickedHandle = handleRect.contains(p);

      if (!clickedHandle) {
        const int pos = (orientation() == Qt::Horizontal) ? p.x() : p.y();
        const int span = (orientation() == Qt::Horizontal) ? width() : height();
        const int v = QStyle::sliderValueFromPosition(minimum(), maximum(), pos, span);
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
  static QElapsedTimer t;
  t.invalidate();

  connect(slider_, &QSlider::sliderPressed, this, [this]() {
    isScrubbing_ = true;
    waitingForSeekCommit_ = false;
    pendingSeekMs_ = -1;
    emit scrubStarted();
  });

  connect(slider_, &QSlider::sliderMoved, this, [this](int value) {
    updateLabel(value, durationMs_);

    if (!enableLiveScrubSeek_) return;

    if (!t.isValid()) t.start();
    if (t.elapsed() >= kScrubThrottleMs) {
      emit scrubSeekTo(static_cast<qint64>(value));
      t.restart();
    }
  });

  connect(slider_, &QSlider::sliderReleased, this, [this]() {
    const qint64 releasedPosMs = static_cast<qint64>(slider_->value());
    waitingForSeekCommit_ = true;
    pendingSeekMs_ = releasedPosMs;
    isScrubbing_ = false;
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
    updateLabel(v, durationMs_);
  });
}

void TimelineBar::reset() {
  durationMs_ = 0;
  lastKnownPositionMs_ = 0;
  isScrubbing_ = false;
  isEditingTimeEntry_ = false;
  waitingForSeekCommit_ = false;
  pendingSeekMs_ = -1;
  slider_->setRange(0, 0);
  slider_->setValue(0);
  slider_->setEnabled(false);
  if (timeEntry_) timeEntry_->hide();
  if (label_) label_->show();
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
  slider_->setRange(0, static_cast<int>(durationMs_));
  slider_->setEnabled(durationMs_ > 0);
  updateLabel(slider_->value(), durationMs_);
}

void TimelineBar::setPositionMs(qint64 posMs) {
  posMs = std::max<qint64>(0, std::min(posMs, durationMs_));
  if (waitingForSeekCommit_ && pendingSeekMs_ >= 0) {
    if (qAbs(posMs - pendingSeekMs_) <= kSeekCommitToleranceMs) {
      waitingForSeekCommit_ = false;
      pendingSeekMs_ = -1;
    } else {
      updateLabel(pendingSeekMs_, durationMs_);
      return;
    }
  }
  if (!isScrubbing_) {
    slider_->setValue(static_cast<int>(posMs));
  }
  lastKnownPositionMs_ = posMs;
  updateLabel(posMs, durationMs_);
}

void TimelineBar::updateLabel(qint64 posMs, qint64 durMs) {
  if (!isEditingTimeEntry_) {
    label_->setText(QString("%1 / %2").arg(formatMs(posMs), formatMs(durMs)));
  }
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

void TimelineBar::beginTimeEntry() {
  if (!slider_ || !timeEntry_ || !label_) return;
  if (!slider_->isEnabled() || durationMs_ <= 0 || isEditingTimeEntry_) return;

  emit timeEntryStarted();
  isEditingTimeEntry_ = true;
  waitingForSeekCommit_ = false;
  pendingSeekMs_ = -1;

  const qint64 currentMs = static_cast<qint64>(slider_->value());
  timeEntry_->setText(formatMs(currentMs));
  label_->hide();
  timeEntry_->show();
  timeEntry_->setFocus(Qt::MouseFocusReason);
  timeEntry_->selectAll();
}

void TimelineBar::commitTimeEntry() {
  if (!isEditingTimeEntry_ || !timeEntry_ || !label_ || !slider_) return;

  qint64 targetMs = -1;
  if (!parseTimeEntryMs(timeEntry_->text(), &targetMs)) {
    cancelTimeEntry();
    return;
  }

  targetMs = std::max<qint64>(0, std::min(targetMs, durationMs_));
  waitingForSeekCommit_ = true;
  pendingSeekMs_ = targetMs;
  isEditingTimeEntry_ = false;

  slider_->setValue(static_cast<int>(targetMs));
  lastKnownPositionMs_ = targetMs;
  label_->show();
  timeEntry_->hide();
  updateLabel(targetMs, durationMs_);
  emit scrubFinished(targetMs);
}

void TimelineBar::cancelTimeEntry() {
  if (!isEditingTimeEntry_ || !timeEntry_ || !label_) return;
  isEditingTimeEntry_ = false;
  timeEntry_->hide();
  label_->show();
  updateLabel(lastKnownPositionMs_, durationMs_);
}

bool TimelineBar::parseTimeEntryMs(const QString& text, qint64* outMs) {
  if (!outMs) return false;
  const QString trimmed = text.trimmed();
  if (trimmed.isEmpty()) return false;

  auto isAllDigits = [](const QString& value) {
    for (const QChar c : value) {
      if (!c.isDigit()) return false;
    }
    return !value.isEmpty();
  };

  qint64 totalSeconds = 0;
  if (trimmed.contains(QLatin1Char(':'))) {
    const QStringList parts = trimmed.split(QLatin1Char(':'), Qt::KeepEmptyParts);
    if (parts.isEmpty() || parts.size() > 3) return false;
    for (const QString& part : parts) {
      if (!isAllDigits(part.trimmed())) return false;
    }
    const int partCount = parts.size();
    for (int i = 0; i < partCount; ++i) {
      const qint64 value = parts.at(i).trimmed().toLongLong();
      const int power = partCount - 1 - i;
      if (power == 2) totalSeconds += value * 3600;
      else if (power == 1) totalSeconds += value * 60;
      else totalSeconds += value;
    }
  } else {
    if (!isAllDigits(trimmed)) return false;
    if (trimmed.size() <= 2) {
      totalSeconds = trimmed.toLongLong();
    } else if (trimmed.size() <= 4) {
      const QString secondsPart = trimmed.right(2);
      const QString minutesPart = trimmed.left(trimmed.size() - 2);
      totalSeconds = minutesPart.toLongLong() * 60 + secondsPart.toLongLong();
    } else {
      const QString secondsPart = trimmed.right(2);
      const QString minutesPart = trimmed.mid(trimmed.size() - 4, 2);
      const QString hoursPart = trimmed.left(trimmed.size() - 4);
      totalSeconds = hoursPart.toLongLong() * 3600 + minutesPart.toLongLong() * 60 + secondsPart.toLongLong();
    }
  }

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

