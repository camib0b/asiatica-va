#include "VideoControlsBar.h"
#include "PlaybackRates.h"
#include "PlaybackSpeedometer.h"
#include "../style/StyleProps.h"
#include "../i18n/AppLocale.h"

#include <QHBoxLayout>
#include <QPushButton>
#include <QSizePolicy>

#include <QAction>
#include <QApplication>
#include <QKeySequence>
#include <QList>

#include <QTimer>
#include <QPointer>

#include <array>


namespace {
constexpr qint64 kSeekStepMs = 2000;
constexpr int kFlashDurationMs = 150;

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
      playbackShortcutMediaGate_(false),
      playbackShortcutFocusGate_(false),
      mediaEnabled_(false),
      playing_(false),
      muted_(false),
      playbackRate_(PlaybackRates::kResetRate) {
  buildUi();
  wireSignals();
  setPlaybackShortcutMediaGate(false);
  setPlaybackShortcutFocusGate(false);
  buildKeyboardShortcuts();
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
  layout->setContentsMargins(6, 4, 6, 4);
  layout->setSpacing(4);

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

void VideoControlsBar::buildKeyboardShortcuts() {
  Q_ASSERT(QApplication::instance() != nullptr);

  auto makeGatedPlaybackShortcut = [this](const QList<QKeySequence>& shortcuts) {
    auto* action = new QAction(this);
    action->setShortcuts(shortcuts);
    action->setShortcutContext(Qt::ApplicationShortcut);
    action->setEnabled(false);
    addAction(action);
    return action;
  };

  togglePlayPauseAction_ = makeGatedPlaybackShortcut({QKeySequence(Qt::Key_Space)});
  connect(togglePlayPauseAction_, &QAction::triggered, this, [this]() {
    emit togglePlayPauseFromKeyboardShortcut();
  });

  slowerPlaybackAction_ = makeGatedPlaybackShortcut({
      QKeySequence(Qt::Key_Minus),
      QKeySequence(Qt::Key_Minus | Qt::KeypadModifier),
  });
  connect(slowerPlaybackAction_, &QAction::triggered, this, [this]() {
    flashSpeedometer();
    emit playbackRateRequested(PlaybackRates::slower(playbackRate_));
  });

  fasterPlaybackAction_ = makeGatedPlaybackShortcut({
      QKeySequence(Qt::Key_Plus),
      QKeySequence(Qt::Key_Plus | Qt::KeypadModifier),
      QKeySequence(Qt::SHIFT | Qt::Key_Equal),
  });
  connect(fasterPlaybackAction_, &QAction::triggered, this, [this]() {
    flashSpeedometer();
    emit playbackRateRequested(PlaybackRates::faster(playbackRate_));
  });

  resetSpeedAction_ = makeGatedPlaybackShortcut({
      QKeySequence(Qt::SHIFT | Qt::Key_BraceRight),
  });
  connect(resetSpeedAction_, &QAction::triggered, this, [this]() {
    flashSpeedometer();
    emit playbackRateRequested(PlaybackRates::kResetRate);
  });

  muteToggleAction_ = makeGatedPlaybackShortcut({
      QKeySequence(Qt::SHIFT | Qt::Key_M),
      QKeySequence(Qt::Key_VolumeMute),
  });
  connect(muteToggleAction_, &QAction::triggered, this, [this]() {
    if (mediaEnabled_) {
      emit toggleMuteFromKeyboardShortcut();
    }
  });

  updatePlaybackShortcutEnablement();
}

void VideoControlsBar::setPlaybackShortcutMediaGate(bool enabled) {
  playbackShortcutMediaGate_ = enabled;
  updatePlaybackShortcutEnablement();
}

void VideoControlsBar::setPlaybackShortcutFocusGate(bool allowed) {
  playbackShortcutFocusGate_ = allowed;
  updatePlaybackShortcutEnablement();
}

void VideoControlsBar::updatePlaybackShortcutEnablement() {
  const bool shortcutsActive = playbackShortcutMediaGate_ && playbackShortcutFocusGate_;
  const std::array<QAction*, 5> playbackShortcutActions = {
    togglePlayPauseAction_,
    slowerPlaybackAction_,
    fasterPlaybackAction_,
    resetSpeedAction_,
    muteToggleAction_,
  };
  for (auto* action : playbackShortcutActions) {
    if (action) {
      action->setEnabled(shortcutsActive);
    }
  }
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
