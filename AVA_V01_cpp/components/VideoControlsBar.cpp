#include "VideoControlsBar.h"
#include "../style/StyleProps.h"
#include "../i18n/AppLocale.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSizePolicy>

#include <QAction>
#include <QApplication>
#include <QKeySequence>
#include <QList>

#include <QTimer>
#include <QPointer>
#include <QPalette>
#include <QColor>

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
      playbackRate_(1.0) {
  buildUi();
  wireSignals();
  setPlaybackShortcutMediaGate(false);
  setPlaybackShortcutFocusGate(false);
  buildKeyboardShortcuts();
  setEnabledForMedia(false);
  setPlaying(false);
  setPlaybackRate(1.0);
  setMuted(false);
  applyUiStrings();
}

void VideoControlsBar
::buildUi() {
  setObjectName(QStringLiteral("VideoControlsBar"));

  auto* layout = new QHBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  playButton_       = new QPushButton(this);
  pauseButton_      = new QPushButton(this);
  backButton_       = new QPushButton(this);
  forwardButton_    = new QPushButton(this);
  slowerButton_     = new QPushButton(this);
  resetSpeedButton_ = new QPushButton(this);
  fasterButton_     = new QPushButton(this);
  muteButton_       = new QPushButton(this);

  std::array<QPushButton*, 8> videoControlButtons = {
    playButton_,
    pauseButton_,
    backButton_, 
    forwardButton_,
    slowerButton_,
    resetSpeedButton_,
    fasterButton_,
    muteButton_
  };

  for (auto* button : videoControlButtons) {
    Style::setSize(button, "sm");
    button->setFocusPolicy(Qt::NoFocus);
    button->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  }

  Style::setVariant(playButton_, "primary");
  Style::setVariant(pauseButton_, "secondary");
  Style::setVariant(backButton_, "outline");
  Style::setVariant(forwardButton_, "outline");
  Style::setVariant(slowerButton_, "ghost");
  Style::setVariant(resetSpeedButton_, "ghost");
  Style::setVariant(fasterButton_, "ghost");
  Style::setVariant(muteButton_, "outline");


  
  muteButton_->setCheckable(true);
  
  speedLabel_ = new QLabel(this);
  Style::setRole(speedLabel_, "muted");
  updateSpeedLabel();

  layout->addWidget(playButton_);
  layout->addWidget(pauseButton_);
  layout->addSpacing(8);
  layout->addWidget(backButton_);
  layout->addWidget(forwardButton_);
  layout->addSpacing(8);
  layout->addWidget(slowerButton_);
  layout->addWidget(resetSpeedButton_);
  layout->addWidget(fasterButton_);
  layout->addStretch(1);
  layout->addWidget(speedLabel_);
  layout->addSpacing(8);
  layout->addWidget(muteButton_);
}

void VideoControlsBar::applyUiStrings() {
  Q_ASSERT(playButton_ && pauseButton_ && backButton_ && forwardButton_
           && slowerButton_ && resetSpeedButton_ && fasterButton_ && muteButton_
           && speedLabel_);

  applyButtonStrings(playButton_, "vc.play", "vc.tt.play");
  applyButtonStrings(pauseButton_, "vc.pause", "vc.tt.pause");
  applyButtonStrings(backButton_, "vc.back", "vc.tt.back");
  applyButtonStrings(forwardButton_, "vc.forward", "vc.tt.forward");
  applyButtonStrings(slowerButton_, "vc.slower", "vc.tt.slower");
  applyButtonStrings(resetSpeedButton_, "vc.reset_speed", "vc.tt.reset");
  applyButtonStrings(fasterButton_, "vc.faster", "vc.tt.faster");
  updateMuteButton();
  updateSpeedLabel();
}

void VideoControlsBar::wireSignals() {
  // Play / Pause
  connect(playButton_,  &QPushButton::clicked, this, &VideoControlsBar::flashPlayButton);
  connect(playButton_,  &QPushButton::clicked, this, &VideoControlsBar::playRequested);

  connect(pauseButton_, &QPushButton::clicked, this, &VideoControlsBar::flashPauseButton);
  connect(pauseButton_, &QPushButton::clicked, this, &VideoControlsBar::pauseRequested);

  // Seek back / forward
  connect(backButton_,  &QPushButton::clicked, this, &VideoControlsBar::flashSeekBackButton);
  connect(backButton_,  &QPushButton::clicked, this, [this]() { emit seekRequestedMs(-kSeekStepMs); });

  connect(forwardButton_, &QPushButton::clicked, this, &VideoControlsBar::flashSeekForwardButton);
  connect(forwardButton_, &QPushButton::clicked, this, [this]() { emit seekRequestedMs(+kSeekStepMs); });

  // Speed controls
  connect(slowerButton_, &QPushButton::clicked, this, &VideoControlsBar::flashSlowerButton);
  connect(slowerButton_, &QPushButton::clicked, this, &VideoControlsBar::slowerRequested);

  connect(fasterButton_, &QPushButton::clicked, this, &VideoControlsBar::flashFasterButton);
  connect(fasterButton_, &QPushButton::clicked, this, &VideoControlsBar::fasterRequested);

  connect(resetSpeedButton_, &QPushButton::clicked, this, &VideoControlsBar::flashResetSpeedButton);
  connect(resetSpeedButton_, &QPushButton::clicked, this, &VideoControlsBar::resetSpeedRequested);

  // Mute: flash on click; keep label/tooltip in sync, then notify the player.
  connect(muteButton_, &QPushButton::clicked, this, &VideoControlsBar::flashMuteButton);
  connect(muteButton_, &QPushButton::toggled, this, [this](bool muted) {
    muted_ = muted;
    updateMuteButton();
    emit muteToggled(muted);
  });
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
  connect(slowerPlaybackAction_, &QAction::triggered, this, &VideoControlsBar::flashSlowerButton);
  connect(slowerPlaybackAction_, &QAction::triggered, this, &VideoControlsBar::slowerRequested);

  fasterPlaybackAction_ = makeGatedPlaybackShortcut({
      QKeySequence(Qt::Key_Plus),
      QKeySequence(Qt::Key_Plus | Qt::KeypadModifier),
      QKeySequence(Qt::SHIFT | Qt::Key_Equal),
  });
  connect(fasterPlaybackAction_, &QAction::triggered, this, &VideoControlsBar::flashFasterButton);
  connect(fasterPlaybackAction_, &QAction::triggered, this, &VideoControlsBar::fasterRequested);

  resetSpeedAction_ = makeGatedPlaybackShortcut({
      QKeySequence(Qt::SHIFT | Qt::Key_BraceRight),
  });
  connect(resetSpeedAction_, &QAction::triggered, this, &VideoControlsBar::flashResetSpeedButton);
  connect(resetSpeedAction_, &QAction::triggered, this, &VideoControlsBar::resetSpeedRequested);

  // Shift+M: plain M is WorkWindow mode-cycle. Hardware mute is an extra no-conflict binding.
  muteToggleAction_ = makeGatedPlaybackShortcut({
      QKeySequence(Qt::SHIFT | Qt::Key_M),
      QKeySequence(Qt::Key_VolumeMute),
  });
  connect(muteToggleAction_, &QAction::triggered, this, [this]() {
    if (muteButton_ && muteButton_->isEnabled()) {
      muteButton_->click();
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
  updateEnabledState();
}

void VideoControlsBar::updateEnabledState() const {
  Q_ASSERT(playButton_ && pauseButton_ && backButton_ && forwardButton_
           && slowerButton_ && fasterButton_ && resetSpeedButton_ && muteButton_);

  playButton_->setEnabled(mediaEnabled_ && !playing_);
  pauseButton_->setEnabled(mediaEnabled_ && playing_);
  backButton_->setEnabled(mediaEnabled_);
  forwardButton_->setEnabled(mediaEnabled_);
  slowerButton_->setEnabled(mediaEnabled_);
  fasterButton_->setEnabled(mediaEnabled_);
  resetSpeedButton_->setEnabled(mediaEnabled_);
  muteButton_->setEnabled(mediaEnabled_);
}

void VideoControlsBar::setPlaybackRate(double rate) {
  playbackRate_ = rate;
  updateSpeedLabel();
  
  // Highlight Slower button when rate < 1.0, Faster button when rate > 1.0
  if (slowerButton_) {
    Style::setState(slowerButton_, "speedActive", (rate < 1.0));
  }
  if (fasterButton_) {
    Style::setState(fasterButton_, "speedActive", (rate > 1.0));
  }
}

void VideoControlsBar::setMuted(bool muted) {
  muted_ = muted;
  updateMuteButton();
}

void VideoControlsBar::updateMuteButton() const {
  Q_ASSERT(muteButton_ != nullptr);

  muteButton_->setText(muted_ ? AppLocale::trUi("vc.unmute") : AppLocale::trUi("vc.mute"));
  muteButton_->setToolTip(muted_ ? AppLocale::trUi("vc.tt.unmute") : AppLocale::trUi("vc.tt.mute"));
  if (muteButton_->isChecked() != muted_) {
    muteButton_->setChecked(muted_);
  }
}

void VideoControlsBar::updateSpeedLabel() {
  const QString rateStr = QString::number(playbackRate_, 'f', 2) + QLatin1Char('x');
  speedLabel_->setText(AppLocale::trUi("vc.speed_label").arg(rateStr));
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

void VideoControlsBar::flashPlayButton()       { flashButtonBorder(playButton_); }
void VideoControlsBar::flashPauseButton()      { flashButtonBorder(pauseButton_); }
void VideoControlsBar::flashSeekBackButton()   { flashButtonBorder(backButton_); }
void VideoControlsBar::flashSeekForwardButton(){ flashButtonBorder(forwardButton_); }
void VideoControlsBar::flashSlowerButton()     { flashButtonBorder(slowerButton_); }
void VideoControlsBar::flashFasterButton()     { flashButtonBorder(fasterButton_); }
void VideoControlsBar::flashResetSpeedButton() { flashButtonBorder(resetSpeedButton_); }
void VideoControlsBar::flashMuteButton()       { flashButtonBorder(muteButton_); }