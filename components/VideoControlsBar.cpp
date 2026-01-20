#include "VideoControlsBar.h"
#include "../style/StyleProps.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

#include <QTimer>
#include <QPalette>
#include <QColor>

namespace {
constexpr qint64 kSeekStepMs = 2000;
}

VideoControlsBar::VideoControlsBar(QWidget* parent): QWidget(parent) {
  buildUi();
  wireSignals();
  setEnabledForMedia(false);
  setPlaying(false);
  setPlaybackRate(1.0);
  setMuted(false);
}

void VideoControlsBar
::buildUi() {
  auto* layout = new QHBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  playButton_       = new QPushButton("Play", this);
  pauseButton_      = new QPushButton("Pause", this);
  backButton_       = new QPushButton("⟵ 2s", this);
  forwardButton_    = new QPushButton("2s ⟶", this);
  slowerButton_     = new QPushButton("Slower", this);
  resetSpeedButton_ = new QPushButton("Reset 1.0x", this);
  fasterButton_     = new QPushButton("Faster", this);
  muteButton_       = new QPushButton("Mute", this);

  Style::setSize(playButton_, "md");
  Style::setSize(pauseButton_, "md");
  Style::setSize(backButton_, "md");
  Style::setSize(forwardButton_, "md");
  Style::setSize(slowerButton_, "md");
  Style::setSize(resetSpeedButton_, "md");
  Style::setSize(fasterButton_, "md");
  Style::setSize(muteButton_, "md");
  
  Style::setVariant(playButton_, "primary");
  Style::setVariant(pauseButton_, "secondary");
  Style::setVariant(backButton_, "outline");
  Style::setVariant(forwardButton_, "outline");
  Style::setVariant(slowerButton_, "ghost");
  Style::setVariant(resetSpeedButton_, "ghost");
  Style::setVariant(fasterButton_, "ghost");
  Style::setVariant(muteButton_, "outline");
  

  
  // Prevent space/enter focus weirdness in analysis workflows
  /*
  If later you want the shadcn-like “focus ring”, you’ll have to allow focus
  and prevent “Enter triggers default button” via setAutoDefault(false).
  */
  playButton_       ->setFocusPolicy(Qt::NoFocus);
  pauseButton_      ->setFocusPolicy(Qt::NoFocus);
  backButton_       ->setFocusPolicy(Qt::NoFocus);
  forwardButton_    ->setFocusPolicy(Qt::NoFocus);
  slowerButton_     ->setFocusPolicy(Qt::NoFocus);
  resetSpeedButton_ ->setFocusPolicy(Qt::NoFocus);
  fasterButton_     ->setFocusPolicy(Qt::NoFocus);
  muteButton_       ->setFocusPolicy(Qt::NoFocus);
  
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

  // control bar button keyboard hotkeys and tooltips:
  playButton_       ->setToolTip("space  Play");
  pauseButton_      ->setToolTip("space  Pause");
  backButton_       ->setToolTip("⟵  Back");
  forwardButton_    ->setToolTip("⟶  Forward");
  slowerButton_     ->setToolTip("{  Slower");
  fasterButton_     ->setToolTip("}  Faster");
  resetSpeedButton_ ->setToolTip("\\  Reset speed");
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

  // Mute: flash on click, emit state on toggled
  connect(muteButton_, &QPushButton::clicked, this, &VideoControlsBar::flashMuteButton);
  connect(muteButton_, &QPushButton::toggled, this, &VideoControlsBar::muteToggled);
}


void VideoControlsBar::setEnabledForMedia(bool enabled) {
  playButton_->setEnabled(enabled);
  pauseButton_->setEnabled(enabled);
  backButton_->setEnabled(enabled);
  forwardButton_->setEnabled(enabled);
  slowerButton_->setEnabled(enabled);
  fasterButton_->setEnabled(enabled);
  resetSpeedButton_->setEnabled(enabled);
  muteButton_->setEnabled(enabled);
}

void VideoControlsBar::setPlaying(bool playing) {
  playButton_->setEnabled(!playing);
  pauseButton_->setEnabled(playing);
}

void VideoControlsBar::setPlaybackRate(double rate) {
  playbackRate_ = rate;
  updateSpeedLabel();
}

void VideoControlsBar::setMuted(bool muted) {
  muteButton_->blockSignals(true);
  muteButton_->setChecked(muted);
  muteButton_->setText(muted ? "Unmute" : "Mute");
  muteButton_->blockSignals(false);
}

void VideoControlsBar::updateSpeedLabel() {
  speedLabel_->setText(QString("Speed: %1x").arg(playbackRate_, 0, 'f', 2));
}

void VideoControlsBar::flashButtonBorder(QPushButton* button) {
  if (!button) return;

  QTimer* timer = flashTimers_.value(button, nullptr);
  if (!timer) {
    timer = new QTimer(button);
    timer->setSingleShot(true);
    flashTimers_.insert(button, timer);

    connect(timer, &QTimer::timeout, this, [button]() {
      if (!button) return;
      button->setProperty("flash", false);
      button->style()->unpolish(button);
      button->style()->polish(button);
      button->update();
    });
  }

  button->setProperty("flash", true);
  button->style()->unpolish(button);
  button->style()->polish(button);
  button->update();

  timer->start(150);
}






void VideoControlsBar::flashPlayButton()       { flashButtonBorder(playButton_); }
void VideoControlsBar::flashPauseButton()      { flashButtonBorder(pauseButton_); }
void VideoControlsBar::flashSeekBackButton()   { flashButtonBorder(backButton_); }
void VideoControlsBar::flashSeekForwardButton(){ flashButtonBorder(forwardButton_); }
void VideoControlsBar::flashSlowerButton()     { flashButtonBorder(slowerButton_); }
void VideoControlsBar::flashFasterButton()     { flashButtonBorder(fasterButton_); }
void VideoControlsBar::flashResetSpeedButton() { flashButtonBorder(resetSpeedButton_); }
void VideoControlsBar::flashMuteButton()       { flashButtonBorder(muteButton_); }