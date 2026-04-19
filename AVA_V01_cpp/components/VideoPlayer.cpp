#include "VideoPlayer.h"
#include "VideoControlsBar.h"
#include "TimelineBar.h"

#ifdef Q_OS_MACOS
#include "../macos/PlaybackActivity.h"
#else
static void avaBeginPlaybackUserActivity() {}
static void avaEndPlaybackUserActivity() {}
#endif

#include <QtGlobal>
#include <QVBoxLayout>
#include <QAudioOutput>
#include <QGuiApplication>
#include <QMediaPlayer>
#include <QTimer>
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
    setupPlaybackReliabilityHooks();
    buildKeyboardShortcuts();
    setControlsEnabled(false);
}

VideoPlayer::~VideoPlayer() {
    avaEndPlaybackUserActivity();
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
    videoWidget_->setAspectRatioMode(Qt::KeepAspectRatio);
    videoWidget_->setMinimumHeight(360);
    videoWidget_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    videoWidget_->setAttribute(Qt::WA_Hover, true);
    videoWidget_->setStyleSheet(QStringLiteral("background-color: black;"));
    
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
    connect(videoControlsBar_, &VideoControlsBar::togglePlayPauseFromKeyboardShortcut, this,
            &VideoPlayer::togglePlayPauseWithControlFlash);

    // play pause button sensible to state changes:
    connect(player_, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState state) {
        if (videoControlsBar_) videoControlsBar_->setPlaying(state == QMediaPlayer::PlayingState);
        updateStallMonitorForPlaybackState(state);
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

    // Time-entry seek should pause and stay paused after jumping.
    connect(videoTimelineBar_, &TimelineBar::timeEntryStarted, this, [this]() {
        wasPlayingBeforeScrub_ = false;
        if (player_->playbackState() == QMediaPlayer::PlayingState) {
            player_->pause();
        }
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
  
    // Space and playback-speed keys live on VideoControlsBar (Qt::ApplicationShortcut), same pattern as GameControls.
    // This widget stays hidden while its children are reparented; the controls bar is visible in the layout.

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
    mediaControlsEnabled_ = enabled;
    if (videoControlsBar_) videoControlsBar_->setEnabledForMedia(enabled);
    if (videoWidget_) videoWidget_->setEnabled(enabled);
    if (videoTimelineBar_) {
        videoTimelineBar_->setEnabled(enabled);
        videoTimelineBar_->setEnabledForMedia(enabled);
    }

    updatePlaybackShortcutActionStates();
}

void VideoPlayer::setPlaybackKeyboardShortcutsEnabled(bool enabled) {
    playbackKeyboardShortcutsEnabled_ = enabled;
    updatePlaybackShortcutActionStates();
}

void VideoPlayer::updatePlaybackShortcutActionStates() {
    const bool shortcutsOn = mediaControlsEnabled_ && playbackKeyboardShortcutsEnabled_;
    if (seekSmallBackAction_) seekSmallBackAction_->setEnabled(shortcutsOn);
    if (seekSmallForwardAction_) seekSmallForwardAction_->setEnabled(shortcutsOn);
    if (seekBigBackAction_) seekBigBackAction_->setEnabled(shortcutsOn);
    if (seekBigForwardAction_) seekBigForwardAction_->setEnabled(shortcutsOn);
    if (videoControlsBar_) {
        videoControlsBar_->setPlaybackShortcutMediaGate(shortcutsOn);
    }
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

void VideoPlayer::togglePlayPauseWithControlFlash() {
    if (!player_) return;
    const auto state = player_->playbackState();
    if (videoControlsBar_) {
        (state == QMediaPlayer::PlayingState) ? videoControlsBar_->flashPauseButton()
                                              : videoControlsBar_->flashPlayButton();
    }
    onTogglePlayPause();
}

void VideoPlayer::playbackSlowerWithControlFlash() {
    if (videoControlsBar_) videoControlsBar_->flashSlowerButton();
    onSlowerClicked();
}

void VideoPlayer::playbackFasterWithControlFlash() {
    if (videoControlsBar_) videoControlsBar_->flashFasterButton();
    onFasterClicked();
}

void VideoPlayer::playbackResetSpeedWithControlFlash() {
    if (videoControlsBar_) videoControlsBar_->flashResetSpeedButton();
    onResetSpeedClicked();
}

void VideoPlayer::setPlaybackRateAndPlay(double rate) {
    playbackRate_ = rate;
    player_->setPlaybackRate(playbackRate_);
    if (videoControlsBar_) videoControlsBar_->setPlaybackRate(playbackRate_);
    player_->play();
}

void VideoPlayer::setupPlaybackReliabilityHooks() {
    playbackStallTimer_ = new QTimer(this);
    playbackStallTimer_->setInterval(3500);
    connect(playbackStallTimer_, &QTimer::timeout, this, [this]() {
        if (!player_ || loadedSourcePath_.isEmpty()) return;
        if (player_->playbackState() != QMediaPlayer::PlayingState) return;

        const qint64 dur = player_->duration();
        const qint64 pos = player_->position();
        if (dur > 0 && pos >= dur - 400) return;

        if (lastStallCheckPositionMs_ < 0) {
            lastStallCheckPositionMs_ = pos;
            return;
        }
        if (pos == lastStallCheckPositionMs_) {
            ++consecutivePlaybackStallTicks_;
            nudgePlaybackAfterBackendStall();
            if (consecutivePlaybackStallTicks_ >= 2) {
                reloadCurrentMediaFromDisk();
                consecutivePlaybackStallTicks_ = 0;
            }
        } else {
            consecutivePlaybackStallTicks_ = 0;
        }
        lastStallCheckPositionMs_ = pos;
    });

    if (QGuiApplication::instance() != nullptr) {
        connect(qGuiApp, &QGuiApplication::applicationStateChanged, this,
                [this](Qt::ApplicationState state) {
                    if (state != Qt::ApplicationActive || !player_ || loadedSourcePath_.isEmpty()) return;
                    if (userRequestedPlaying_ &&
                        player_->playbackState() != QMediaPlayer::PlayingState) {
                        player_->play();
                    }
                    consecutivePlaybackStallTicks_ = 0;
                    lastStallCheckPositionMs_ = -1;
                });
    }

    connect(player_, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error error, const QString& /*errorString*/) {
                if (error == QMediaPlayer::NoError || loadedSourcePath_.isEmpty()) return;
                nudgePlaybackAfterBackendStall();
            });
}

void VideoPlayer::updateStallMonitorForPlaybackState(QMediaPlayer::PlaybackState state) {
    if (!playbackStallTimer_) return;
    userRequestedPlaying_ = (state == QMediaPlayer::PlayingState);
    if (state == QMediaPlayer::PlayingState) {
        lastStallCheckPositionMs_ = -1;
        consecutivePlaybackStallTicks_ = 0;
        playbackStallTimer_->start();
    } else {
        playbackStallTimer_->stop();
    }
}

void VideoPlayer::nudgePlaybackAfterBackendStall() {
    if (!player_ || loadedSourcePath_.isEmpty()) return;
    const qint64 pos = player_->position();
    const qint64 dur = player_->duration();
    qint64 bumpMs = 1;
    if (dur > 0 && pos >= dur - 5) bumpMs = 0;

    player_->pause();
    player_->setPosition(pos + bumpMs);
    if (userRequestedPlaying_) player_->play();
}

void VideoPlayer::reloadCurrentMediaFromDisk() {
    if (!player_ || loadedSourcePath_.isEmpty()) return;
    const qint64 pos = player_->position();
    const double savedRate = playbackRate_;
    const bool resumePlaying = userRequestedPlaying_;

    player_->stop();
    player_->setSource(QUrl());
    player_->setSource(QUrl::fromLocalFile(loadedSourcePath_));
    player_->setPlaybackRate(savedRate);
    if (videoControlsBar_) videoControlsBar_->setPlaybackRate(savedRate);
    player_->setPosition(pos);
    if (resumePlaying) player_->play();
}

void VideoPlayer::loadVideoFromFile(const QString& filePath) {
    if (filePath.isEmpty()) return;

    if (!loadedSourcePath_.isEmpty() && loadedSourcePath_ != filePath) {
        avaEndPlaybackUserActivity();
    }

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
    
    loadedSourcePath_ = filePath;
    avaBeginPlaybackUserActivity();

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
