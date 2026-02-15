#include "TimelineBar.h"
#include "../style/StyleProps.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QStyle>
#include <QStyleOptionSlider>


namespace {
constexpr qint64 kScrubThrottleMs = 33; // ~30Hz

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
  auto* layout = new QHBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(10);

  slider_ = new ClickSeekSlider(Qt::Horizontal, this);
  slider_->setObjectName("TimelineSlider");
  slider_->setRange(0, 0);
  slider_->setSingleStep(50);
  slider_->setPageStep(1000);
  slider_->setTracking(false); // you already found this feels better

  label_ = new QLabel("00:00.000 / 00:00.000", this);
  Style::setRole(label_, "muted");
  label_->setMinimumWidth(160);
  label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

  layout->addWidget(slider_, 1);
  layout->addWidget(label_);
}

void TimelineBar::wireSignals() {
  static QElapsedTimer t;
  t.invalidate();

  connect(slider_, &QSlider::sliderPressed, this, [this]() {
    isScrubbing_ = true;
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
    isScrubbing_ = false;
    emit scrubFinished(static_cast<qint64>(slider_->value()));
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
  isScrubbing_ = false;
  slider_->setRange(0, 0);
  slider_->setValue(0);
  slider_->setEnabled(false);
  updateLabel(0, 0);
}

void TimelineBar::setEnabledForMedia(bool on) {
  slider_->setEnabled(on && durationMs_ > 0);
}

void TimelineBar::setDurationMs(qint64 durMs) {
  durationMs_ = std::max<qint64>(0, durMs);
  slider_->setRange(0, static_cast<int>(durationMs_));
  slider_->setEnabled(durationMs_ > 0);
  updateLabel(slider_->value(), durationMs_);
}

void TimelineBar::setPositionMs(qint64 posMs) {
  posMs = std::max<qint64>(0, std::min(posMs, durationMs_));
  if (!isScrubbing_) {
    slider_->setValue(static_cast<int>(posMs));
  }
  updateLabel(posMs, durationMs_);
}

void TimelineBar::updateLabel(qint64 posMs, qint64 durMs) {
  label_->setText(QString("%1 / %2").arg(formatMs(posMs), formatMs(durMs)));
}

QString TimelineBar::formatMs(qint64 ms) {
  ms = std::max<qint64>(0, ms);
  const qint64 minutes = ms / 60000;
  ms %= 60000;
  const qint64 seconds = ms / 1000;
  const qint64 millis  = ms % 1000;

  return QString("%1:%2.%3")
    .arg(minutes, 2, 10, QChar('0'))
    .arg(seconds, 2, 10, QChar('0'))
    .arg(millis,  3, 10, QChar('0'));
}

