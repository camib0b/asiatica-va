#include "VideoControlsBar.h"
#include "PlaybackRates.h"
#include "PlaybackSpeedometer.h"
#include "../style/StyleProps.h"
#include "../i18n/AppLocale.h"

#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPushButton>
#include <QSizePolicy>

#include <QTimer>
#include <QPointer>

#include <array>


namespace {
constexpr qint64 kSeekStepMs = 2000;
constexpr int kFlashDurationMs = 150;
constexpr int kTrayCornerRadiusPx = 10;
constexpr int kTrayFillAlpha = 122;      // ~48% of zinc-950
constexpr int kTrayBorderAlpha = 31;     // ~12% white hairline

void setButtonFlashState(QPushButton* button, bool flashing) {
  if (!button) {
    return;
  }
  button->setProperty("flash", flashing);
  button->style()->unpolish(button);
  button->style()->polish(button);
  button->update();
}

void applyButtonStrings(QPushButton* button, const char* textKey, const char* tooltipKey) {
  Q_ASSERT(button != nullptr);
  button->setText(AppLocale::trUi(textKey));
  button->setToolTip(AppLocale::trUi(tooltipKey));
}
}

VideoControlsBar::VideoControlsBar(QWidget* parent)
    : QWidget(parent),
      mediaEnabled_(false),
      playing_(false),
      muted_(false),
      playbackRate_(PlaybackRates::kResetRate) {
  buildUi();
  wireSignals();
  setEnabledForMedia(false);
  setPlaying(false);
  setPlaybackRate(PlaybackRates::kResetRate);
  setMuted(false);
  applyUiStrings();
}

void VideoControlsBar
::buildUi() {
  setObjectName(QStringLiteral("VideoControlsBar"));
  setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
  setAttribute(Qt::WA_StyledBackground, true);

  auto* layout = new QHBoxLayout(this);
  layout->setContentsMargins(16, 14, 16, 14);
  layout->setSpacing(10);

  playPauseButton_ = new QPushButton(this);
  playPauseButton_->setObjectName(QStringLiteral("PlayPauseButton"));
  backButton_    = new QPushButton(this);
  forwardButton_ = new QPushButton(this);
  speedometer_   = new PlaybackSpeedometer(this);

  std::array<QPushButton*, 3> videoControlButtons = {
    playPauseButton_,
    backButton_,
    forwardButton_
  };

  for (auto* button : videoControlButtons) {
    Style::setSize(button, "sm");
    button->setFocusPolicy(Qt::NoFocus);
    button->setFlat(true);
    button->setAutoDefault(false);
    button->setDefault(false);
    button->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  }

  Style::setVariant(playPauseButton_, "outline");
  Style::setVariant(backButton_, "outline");
  Style::setVariant(forwardButton_, "outline");

  layout->addWidget(playPauseButton_);
  layout->addWidget(backButton_);
  layout->addWidget(forwardButton_);
  layout->addWidget(speedometer_);
}

void VideoControlsBar::applyUiStrings() {
  Q_ASSERT(playPauseButton_ && backButton_ && forwardButton_ && speedometer_);

  applyButtonStrings(backButton_, "vc.back", "vc.tt.back");
  applyButtonStrings(forwardButton_, "vc.forward", "vc.tt.forward");
  updatePlayPauseButton();
  updateSpeedometerTooltip();
}

void VideoControlsBar::wireSignals() {
  connect(playPauseButton_, &QPushButton::clicked, this, &VideoControlsBar::flashPlayPauseButton);
  connect(playPauseButton_, &QPushButton::clicked, this, [this]() {
    if (playing_) {
      emit pauseRequested();
    } else {
      emit playRequested();
    }
  });

  connect(backButton_,  &QPushButton::clicked, this, &VideoControlsBar::flashSeekBackButton);
  connect(backButton_,  &QPushButton::clicked, this, [this]() { emit seekRequestedMs(-kSeekStepMs); });

  connect(forwardButton_, &QPushButton::clicked, this, &VideoControlsBar::flashSeekForwardButton);
  connect(forwardButton_, &QPushButton::clicked, this, [this]() { emit seekRequestedMs(+kSeekStepMs); });

  connect(speedometer_, &PlaybackSpeedometer::rateChanged, this, [this](double rate) {
    playbackRate_ = rate;
    emit playbackRateRequested(rate);
  });
  connect(speedometer_, &PlaybackSpeedometer::interactionStarted, this,
          &VideoControlsBar::speedDragStarted);
  connect(speedometer_, &PlaybackSpeedometer::interactionFinished, this,
          &VideoControlsBar::speedDragFinished);
}

void VideoControlsBar::setEnabledForMedia(bool enabled) {
  mediaEnabled_ = enabled;
  updateEnabledState();
}

void VideoControlsBar::setPlaying(bool playing) {
  playing_ = playing;
  updatePlayPauseButton();
  updateEnabledState();
}

void VideoControlsBar::updateEnabledState() const {
  Q_ASSERT(playPauseButton_ && backButton_ && forwardButton_ && speedometer_);

  playPauseButton_->setEnabled(mediaEnabled_);
  backButton_->setEnabled(mediaEnabled_);
  forwardButton_->setEnabled(mediaEnabled_);
  speedometer_->setEnabled(mediaEnabled_);
}

void VideoControlsBar::updatePlayPauseButton() const {
  Q_ASSERT(playPauseButton_ != nullptr);

  playPauseButton_->setText(playing_ ? QString::fromUtf8("\u23F8")
                                     : QString::fromUtf8("\u25B6"));
  playPauseButton_->setToolTip(playing_ ? AppLocale::trUi("vc.tt.pause")
                                        : AppLocale::trUi("vc.tt.play"));
}

void VideoControlsBar::setPlaybackRate(double rate) {
  playbackRate_ = PlaybackRates::snap(rate);
  if (speedometer_) {
    speedometer_->setRate(playbackRate_);
  }
}

void VideoControlsBar::setMuted(bool muted) {
  muted_ = muted;
}

void VideoControlsBar::toggleMute() {
  if (!mediaEnabled_) {
    return;
  }
  muted_ = !muted_;
  emit muteToggled(muted_);
}

void VideoControlsBar::updateSpeedometerTooltip() const {
  if (!speedometer_) {
    return;
  }
  speedometer_->setToolTip(AppLocale::trUi("vc.tt.speed"));
}

void VideoControlsBar::flashButtonBorder(QPushButton* button) {
  if (!button) {
    return;
  }

  auto* timer = button->findChild<QTimer*>(QStringLiteral("flashClearTimer"), Qt::FindDirectChildrenOnly);
  if (!timer) {
    timer = new QTimer(button);
    timer->setObjectName(QStringLiteral("flashClearTimer"));
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, button, [buttonGuard = QPointer<QPushButton>(button)]() {
      setButtonFlashState(buttonGuard, false);
    });
  }

  setButtonFlashState(button, true);
  timer->start(kFlashDurationMs);
}

void VideoControlsBar::flashPlayPauseButton()  { flashButtonBorder(playPauseButton_); }
void VideoControlsBar::flashSeekBackButton()   { flashButtonBorder(backButton_); }
void VideoControlsBar::flashSeekForwardButton(){ flashButtonBorder(forwardButton_); }
void VideoControlsBar::flashSpeedometer() {
  if (speedometer_) {
    speedometer_->flash();
  }
}

void VideoControlsBar::paintEvent(QPaintEvent* event) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);

  QPainterPath trayPath;
  trayPath.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5),
                          kTrayCornerRadiusPx, kTrayCornerRadiusPx);
  painter.fillPath(trayPath, QColor(9, 9, 11, kTrayFillAlpha));
  painter.setPen(QPen(QColor(255, 255, 255, kTrayBorderAlpha), 1.0));
  painter.drawPath(trayPath);

  QWidget::paintEvent(event);
}
