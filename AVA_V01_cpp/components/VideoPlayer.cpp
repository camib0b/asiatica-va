#include "VideoPlayer.h"
#include "VideoControlsBar.h"
#include "TimelineBar.h"

#include <QtGlobal>
#include <QVBoxLayout>
#include <QAudioOutput>
#include <QMediaPlayer>
#include <QUrl>
#include <QVideoWidget>
#include <QAction>
#include <QKeySequence>
#include <QApplication>
#include <QMediaDevices>
#include <QMouseEvent>
#include <algorithm>

namespace {
    constexpr double kMinRate  = 0.25;
    constexpr double kMaxRate  = 4.0;
    constexpr double kRateStep = 0.25;
  
    constexpr qint64 kSeekSmallMs = 250;
    constexpr qint64 kSeekBigMs   = 3000;
} // namespace

VideoPlayer::VideoPlayer(QWidget* parent) : QWidget(parent) {
    buildUi();
    wireSignals();
    buildKeyboardShortcuts();
    setControlsEnabled(false);
}

qint64 VideoPlayer::currentPositionMs() const {
    return player_ ? player_->position() : 0;
}

void VideoPlayer::seekToMs(qint64 posMs) {
    if (!player_) return;
    const qint64 dur = player_->duration();
    const qint64 target = (dur > 0) ? std::clamp(posMs, qint64{0}, dur) : std::max<qint64>(0, posMs);
    player_->setPosition(target);
}

void VideoPlayer::buildUi() {
    // VideoPlayer manages the video widget and player logic
    // Controls and timeline are exposed separately for WorkWindow to lay out
    
    // video widget:
    videoWidget_ = new QVideoWidget(this);
    videoWidget_->setMinimumHeight(360);
    videoWidget_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    videoWidget_->setAttribute(Qt::WA_Hover, true);
    
    videoControlsBar_ = new VideoControlsBar(this);
    videoTimelineBar_ = new TimelineBar(this);

    // media player and audio output:
    player_ = new QMediaPlayer(this);
    audioOutput_ = new QAudioOutput(this);
    player_->setAudioOutput(audioOutput_);
    player_->setVideoOutput(videoWidget_);

    // initial visibility: hidden until video is loaded
    if (videoWidget_) videoWidget_->hide();
    if (videoControlsBar_) videoControlsBar_->hide();
    if (videoTimelineBar_) videoTimelineBar_->hide();
}

void VideoPlayer::wireSignals() {
    connect(videoControlsBar_, &VideoControlsBar::playRequested, this, &VideoPlayer::onPlayClicked);
    connect(videoControlsBar_, &VideoControlsBar::pauseRequested, this, &VideoPlayer::onPauseClicked);
    connect(videoControlsBar_, &VideoControlsBar::slowerRequested, this, &VideoPlayer::onSlowerClicked);
    connect(videoControlsBar_, &VideoControlsBar::fasterRequested, this, &VideoPlayer::onFasterClicked);
    connect(videoControlsBar_, &VideoControlsBar::resetSpeedRequested, this, &VideoPlayer::onResetSpeedClicked);
    connect(videoControlsBar_, &VideoControlsBar::muteToggled, this, &VideoPlayer::onMuteToggled);

    connect(videoControlsBar_, &VideoControlsBar::seekRequestedMs, this, [this](qint64 deltaMs) {
        seekByMs(deltaMs);
    });

    // play pause button sensible to state changes:
    connect(player_, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState state) {
        if (videoControlsBar_) videoControlsBar_->setPlaying(state == QMediaPlayer::PlayingState);
    });  

    // Player -> timeline widget
    connect(player_, &QMediaPlayer::durationChanged, this, [this](qint64 dur) {
        durationMs_ = dur;
        if (videoTimelineBar_) videoTimelineBar_->setDurationMs(dur);
    });

    connect(player_, &QMediaPlayer::positionChanged, this, [this](qint64 pos) {
        if (videoTimelineBar_) videoTimelineBar_->setPositionMs(pos);
        emit positionChangedMs(pos);
    });

    // Timeline widget -> Player (scrub behavior)
    connect(videoTimelineBar_, &TimelineBar::scrubStarted, this, [this]() {
        wasPlayingBeforeScrub_ = (player_->playbackState() == QMediaPlayer::PlayingState);
        if (wasPlayingBeforeScrub_) player_->pause();
    });

    // Live seeking while dragging (TimelineBar throttles; you just apply)
    connect(videoTimelineBar_, &TimelineBar::scrubSeekTo, this, [this](qint64 posMs) {
        player_->setPosition(posMs);
    });

    // Final seek + resume if needed
    connect(videoTimelineBar_, &TimelineBar::scrubFinished, this, [this](qint64 posMs) {
        player_->setPosition(posMs);
        if (wasPlayingBeforeScrub_) player_->play();
        wasPlayingBeforeScrub_ = false;
    });

    // Mouse click on video widget toggles play/pause
    videoWidget_->installEventFilter(this);
}

void VideoPlayer::buildKeyboardShortcuts() {
    Q_ASSERT(QApplication::instance() != nullptr);
  
    auto makeAction = [this](const QKeySequence& seq, auto slot) -> QAction* {
      auto* act = new QAction(this);
      Q_ASSERT(act != nullptr);
  
      act->setShortcut(seq);
      act->setShortcutContext(Qt::ApplicationShortcut);
      connect(act, &QAction::triggered, this, slot);
      this->addAction(act);
      return act;
    };
  
    slowerAction_ = makeAction(QKeySequence(Qt::Key_BraceLeft), [this]() {
        if (videoControlsBar_) videoControlsBar_->flashSlowerButton();
        onSlowerClicked();
    });
    fasterAction_ = makeAction(QKeySequence(Qt::Key_BraceRight), [this]() {
        if (videoControlsBar_) videoControlsBar_->flashFasterButton();
        onFasterClicked();
    });
    resetSpeedAction_ = makeAction(QKeySequence(Qt::Key_Backslash), [this]() {
        if (videoControlsBar_) videoControlsBar_->flashResetSpeedButton();
        onResetSpeedClicked();
    });
  
    togglePlayPauseAction_ = makeAction(QKeySequence(Qt::Key_Space), [this]() {
        if (!player_) return;
        const auto state = player_->playbackState();
        if (videoControlsBar_) {
            (state == QMediaPlayer::PlayingState) ? videoControlsBar_->flashPauseButton()
                                                  : videoControlsBar_->flashPlayButton();
        }
        onTogglePlayPause();
    });
    
    // Arrows: small seek
    seekSmallBackAction_ = makeAction(QKeySequence(Qt::Key_Left), [this]() {
        if (videoControlsBar_) videoControlsBar_->flashSeekBackButton();
        onSeekSmallBackward();
    });
    seekSmallForwardAction_ = makeAction(QKeySequence(Qt::Key_Right), [this]() {
        if (videoControlsBar_) videoControlsBar_->flashSeekForwardButton();
        onSeekSmallForward();
    });
    
    // Shift + Arrows: big seek
    seekBigBackAction_ = makeAction(QKeySequence(Qt::SHIFT | Qt::Key_Left), [this]() {
        if (videoControlsBar_) videoControlsBar_->flashSeekBackButton();
        onSeekBigBackward();
    });
    seekBigForwardAction_ = makeAction(QKeySequence(Qt::SHIFT | Qt::Key_Right), [this]() {
        if (videoControlsBar_) videoControlsBar_->flashSeekForwardButton();
        onSeekBigForward();
    });
    
    slowerAction_->setEnabled(false);
    fasterAction_->setEnabled(false);
    resetSpeedAction_->setEnabled(false);
    togglePlayPauseAction_->setEnabled(false);
    seekSmallBackAction_->setEnabled(false);
    seekSmallForwardAction_->setEnabled(false);
    seekBigBackAction_->setEnabled(false);
    seekBigForwardAction_->setEnabled(false);
}

void VideoPlayer::setControlsVisible(bool visible) {
    if (videoControlsBar_) videoControlsBar_->setVisible(visible);
    if (videoTimelineBar_) videoTimelineBar_->setVisible(visible);
    if (videoWidget_) videoWidget_->setVisible(visible);
}

void VideoPlayer::setControlsEnabled(bool enabled) {
    if (videoControlsBar_) videoControlsBar_->setEnabledForMedia(enabled);
    if (videoWidget_) videoWidget_->setEnabled(enabled);
    if (videoTimelineBar_) {
        videoTimelineBar_->setEnabled(enabled);
        videoTimelineBar_->setEnabledForMedia(enabled);
    }

    // keyboard shortcuts:
    if (slowerAction_) slowerAction_->setEnabled(enabled);
    if (fasterAction_) fasterAction_->setEnabled(enabled);
    if (resetSpeedAction_) resetSpeedAction_->setEnabled(enabled);
    if (togglePlayPauseAction_) togglePlayPauseAction_->setEnabled(enabled);
    if (seekSmallBackAction_) seekSmallBackAction_->setEnabled(enabled);
    if (seekSmallForwardAction_) seekSmallForwardAction_->setEnabled(enabled);
    if (seekBigBackAction_) seekBigBackAction_->setEnabled(enabled);
    if (seekBigForwardAction_) seekBigForwardAction_->setEnabled(enabled);
}

void VideoPlayer::seekByMs(qint64 deltaMs) {
    const qint64 dur  = player_->duration();
    const qint64 pos  = player_->position();
    const qint64 next = pos + deltaMs;

    const qint64 target = (dur > 0) ? std::clamp(next, qint64{0}, dur) : std::max<qint64>(0, next);
    player_->setPosition(target);
}

void VideoPlayer::onPlayClicked() { player_->play(); }
void VideoPlayer::onPauseClicked() { player_->pause(); }
void VideoPlayer::onSeekSmallBackward() { seekByMs(-kSeekSmallMs); }
void VideoPlayer::onSeekSmallForward() { seekByMs(+kSeekSmallMs); }
void VideoPlayer::onSeekBigBackward() { seekByMs(-kSeekBigMs); }
void VideoPlayer::onSeekBigForward() { seekByMs(+kSeekBigMs); }

void VideoPlayer::onSlowerClicked() {
    playbackRate_ = std::max(kMinRate, playbackRate_ - kRateStep);
    player_->setPlaybackRate(playbackRate_);
    if (videoControlsBar_) videoControlsBar_->setPlaybackRate(playbackRate_);
}

void VideoPlayer::onResetSpeedClicked() {
    playbackRate_ = 1.0;
    player_->setPlaybackRate(playbackRate_);
    if (videoControlsBar_) videoControlsBar_->setPlaybackRate(playbackRate_);
}

void VideoPlayer::onFasterClicked() {
    playbackRate_ = std::min(kMaxRate, playbackRate_ + kRateStep);
    player_->setPlaybackRate(playbackRate_);
    if (videoControlsBar_) videoControlsBar_->setPlaybackRate(playbackRate_);
}

void VideoPlayer::onMuteToggled(bool muted) {
    audioOutput_->setMuted(muted);
    if (videoControlsBar_) videoControlsBar_->setMuted(muted); 
}

void VideoPlayer::onTogglePlayPause() {
    if (!player_) return;
    const auto state = player_->playbackState();
    (state == QMediaPlayer::PlayingState) ? player_->pause() : player_->play();
}

void VideoPlayer::setPlaybackRateAndPlay(double rate) {
    playbackRate_ = rate;
    player_->setPlaybackRate(playbackRate_);
    if (videoControlsBar_) videoControlsBar_->setPlaybackRate(playbackRate_);
    player_->play();
}

void VideoPlayer::loadVideoFromFile(const QString& filePath) {
    if (filePath.isEmpty()) return;

    setControlsVisible(true);
    if (videoControlsBar_) videoControlsBar_->setEnabledForMedia(false);
    
    // Reset timeline UI immediately; durationChanged will set real range later
    durationMs_ = 0;
    wasPlayingBeforeScrub_ = false;
    
    if (videoTimelineBar_) videoTimelineBar_->reset();
    audioOutput_->setMuted(false);
    if (videoControlsBar_) videoControlsBar_->setMuted(false);
    
    playbackRate_ = 1.0;
    player_->setPlaybackRate(playbackRate_);
    if (videoControlsBar_) videoControlsBar_->setPlaybackRate(playbackRate_);
    
    // Load (don't assume it will succeed)
    player_->stop();
    player_->setSource(QUrl::fromLocalFile(filePath));
    setControlsEnabled(true);
    player_->setPosition(0);
    player_->play();
}

void VideoPlayer::onAudioOutputsChanged() {
    // Handle audio output device changes (e.g., headphones plugged/unplugged)
    // This slot can be connected to QMediaDevices::audioOutputsChanged signal
}

bool VideoPlayer::eventFilter(QObject* obj, QEvent* event) {
    if (obj == videoWidget_ && event->type() == QEvent::MouseButtonPress) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            onTogglePlayPause();
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}
