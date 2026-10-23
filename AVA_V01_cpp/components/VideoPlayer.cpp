#include "VideoPlayer.h"
#include "PlaybackRates.h"
#include "TelestrationSurface.h"
#include "TimelineBar.h"
#include "VideoControlsBar.h"

#ifdef Q_OS_MACOS
#include "../macos/PlaybackActivity.h"
#include "../macos/SystemPowerNotifier.h"
#else
static void avaBeginPlaybackUserActivity() {}
static void avaEndPlaybackUserActivity() {}
using AvaSystemPowerCallback = void (*)(void* context);
static void avaInstallSystemPowerObservers(void*, AvaSystemPowerCallback, AvaSystemPowerCallback) {}
static void avaRemoveSystemPowerObservers(void*) {}
#endif

#include <QtGlobal>
#include <QVBoxLayout>
#include <QAudioDevice>
#include <QAudioOutput>
#include <QCursor>
#include <QEvent>
#include <QEnterEvent>
#include <QGuiApplication>
#include <QMediaPlayer>
#include <QAbstractAnimation>
#include <QHideEvent>
#include <QMoveEvent>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QShowEvent>
#include <QSizePolicy>
#include <QTimer>
#include <QUrl>
#include <QFrame>
#include <QGraphicsScene>
#include <QGraphicsVideoItem>
#include <QGraphicsView>
#include <QPainter>
#include <QVideoFrame>
#include <QVideoSink>
#include <QAction>
#include <QKeySequence>
#include <QApplication>
#include <QMediaDevices>
#include <QMouseEvent>
#include <QWindow>
#include <algorithm>

namespace {
    constexpr qint64 kSeekSmallMs = 250;
    constexpr qint64 kSeekBigMs   = 3000;

    // AVFoundation often freezes position a few hundred ms before duration while still
    // reporting PlayingState. Treat that window as natural EOF, not a backend stall.
    constexpr qint64 kPlaybackEndGuardMs = 400;
    constexpr int kStallTicksBeforeReload = 2;
    constexpr int kMaxPipelineReloadsWithoutProgress = 1;

    bool isNearPlaybackEnd(qint64 positionMs, qint64 durationMs) {
        return durationMs > 0 && positionMs >= durationMs - kPlaybackEndGuardMs;
    }

    // A scrub seek counts as presented when positionChanged lands here, or a
    // new frame is at least this close to the requested target.
    constexpr qint64 kScrubSeekPresentToleranceMs = 250;
    // Matches TimelineBar's seek-commit timeout: a decoder that never reports
    // a frame or a matching position must not pin later scrub seeks.
    constexpr int kScrubSeekPresentTimeoutMs = 2000;

    qint64 absoluteDifference(qint64 left, qint64 right) {
        return left >= right ? left - right : right - left;
    }

    qint64 clampedMediaPositionMs(const QMediaPlayer* player, qint64 positionMs) {
        const qint64 nonNegativePositionMs = std::max<qint64>(0, positionMs);
        if (!player) return nonNegativePositionMs;
        const qint64 durationMs = player->duration();
        if (durationMs <= 0) return nonNegativePositionMs;
        return std::clamp(nonNegativePositionMs, qint64{0}, durationMs);
    }

    constexpr int kControlsOverlayBottomMarginPx = 12;
    constexpr int kControlsOverlayIdleMs = 2000;
    constexpr int kControlsOverlayFadeMs = 250;
    constexpr qreal kControlsOverlayActiveOpacity = 0.85;

    void lockOverlayWindowSize(QWidget* overlay) {
        if (!overlay) return;
        overlay->adjustSize();
        overlay->setFixedSize(overlay->sizeHint());
    }

    void ensureOverlayWindowNotResizable(QWidget* overlay) {
        if (!overlay) return;
        if (QWindow* window = overlay->windowHandle()) {
            const QSize size = overlay->size();
            window->setMinimumSize(size);
            window->setMaximumSize(size);
        }
    }
} // namespace

VideoPlayer::VideoPlayer(QWidget* parent)
    : QWidget(parent),
      mediaControlsEnabled_(false),
      playbackKeyboardShortcutsEnabled_(true),
      controlsChromeEnabled_(false),
      controlsOverlayOpacity_(kControlsOverlayActiveOpacity),
      playbackRate_(1.0),
      wasPlayingBeforeScrub_(false),
      scrubSeekInFlight_(false),
      scrubSeekWatchingForFrame_(false),
      scrubResumePlayback_(false),
      scrubSeekIssuePosted_(false),
      scrubSeekInFlightTargetMs_(-1),
      scrubSeekPendingMs_(-1),
      scrubSeekPositionAtIssueMs_(-1),
      scrubSeekFrameStartAtIssueUs_(-1),
      lastStallCheckPositionMs_(-1),
      consecutivePlaybackStallTicks_(0),
      userRequestedPlaying_(false),
      pipelineReloadsWithoutProgress_(0),
      sleepRecoveryPending_(false),
      sleepRecoveryWasPlaying_(false),
      sleepRecoveryPositionMs_(0),
      sleepRecoveryRate_(1.0) {
    buildUi();
    wireSignals();
    setupPlaybackReliabilityHooks();
    registerSystemPowerObservers();
    buildKeyboardShortcuts();
    setControlsEnabled(false);
}

VideoPlayer::~VideoPlayer() {
    sleepRecoveryPending_ = false;
    if (overlayPositionHost_) {
        overlayPositionHost_->removeEventFilter(this);
        overlayPositionHost_ = nullptr;
    }
    unregisterSystemPowerObservers();
    avaEndPlaybackUserActivity();
}

qint64 VideoPlayer::currentPositionMs() const {
    return player_ ? player_->position() : 0;
}

qint64 VideoPlayer::durationMs() const {
    if (!player_) return 0;
    const qint64 duration = player_->duration();
    return duration > 0 ? duration : 0;
}

void VideoPlayer::seekToMs(qint64 posMs) {
    if (!player_) return;
    cancelScrubSeekGate();
    const qint64 dur = player_->duration();
    const qint64 target = (dur > 0) ? std::clamp(posMs, qint64{0}, dur) : std::max<qint64>(0, posMs);
    player_->setPosition(target);
}

void VideoPlayer::buildUi() {
    setObjectName(QStringLiteral("VideoStage"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAttribute(Qt::WA_Hover, true);
    setAttribute(Qt::WA_StyledBackground, true);

    auto* stageLayout = new QVBoxLayout(this);
    stageLayout->setContentsMargins(0, 0, 0, 0);
    stageLayout->setSpacing(0);

    videoView_ = new QGraphicsView(this);
    videoView_->setObjectName(QStringLiteral("VideoSurface"));
    videoView_->setFrameShape(QFrame::NoFrame);
    videoView_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    videoView_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    videoView_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    videoView_->setDragMode(QGraphicsView::NoDrag);
    videoView_->setInteractive(false);
    videoView_->setRenderHint(QPainter::Antialiasing, true);
    videoView_->setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    videoView_->setBackgroundBrush(Qt::black);
    videoView_->setMinimumHeight(360);
    videoView_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    videoView_->setAttribute(Qt::WA_Hover, true);
    videoView_->setAttribute(Qt::WA_StyledBackground, true);
    videoView_->setMouseTracking(true);
    videoScene_ = new QGraphicsScene(videoView_);
    videoScene_->setBackgroundBrush(Qt::black);
    videoView_->setScene(videoScene_);
    videoItem_ = new QGraphicsVideoItem();
    videoItem_->setAspectRatioMode(Qt::KeepAspectRatio);
    videoItem_->setOffset(QPointF(0, 0));
    videoItem_->setFlag(QGraphicsItem::ItemClipsChildrenToShape, true);
    videoScene_->addItem(videoItem_);
    videoViewport_ = videoView_->viewport();
    if (videoViewport_) {
        videoViewport_->setMouseTracking(true);
        videoViewport_->setAttribute(Qt::WA_Hover, true);
        videoViewport_->installEventFilter(this);
    }
    stageLayout->addWidget(videoView_, 1);
    telestrationSurface_ = new TelestrationSurface(this);
    telestrationSurface_->attach(videoView_, videoItem_, nullptr);

    videoControlsBar_ = new VideoControlsBar(this);
    videoTimelineBar_ = new TimelineBar(this);
    videoTimelineBar_->setObjectName(QStringLiteral("VideoTimelineBar"));
    videoTimelineBar_->setMinimumHeight(44);
    videoTimelineBar_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    videoTimelineBar_->setAttribute(Qt::WA_StyledBackground, true);
    stageLayout->addWidget(videoTimelineBar_, 0);
    setupControlsOverlay();

    player_ = new QMediaPlayer(this);
    audioOutput_ = new QAudioOutput(this);
    mediaDevices_ = new QMediaDevices(this);
    player_->setAudioOutput(audioOutput_);
    player_->setVideoOutput(videoItem_);
    if (telestrationSurface_) telestrationSurface_->attach(videoView_, videoItem_, player_);
    connect(videoItem_, &QGraphicsVideoItem::nativeSizeChanged, this, [this](const QSizeF&) {
        layoutVideoPicture();
    });

    if (videoView_) videoView_->hide();
    if (videoControlsBar_) videoControlsBar_->hide();
    if (videoTimelineBar_) videoTimelineBar_->hide();
}

void VideoPlayer::setupControlsOverlay() {
    if (!videoControlsBar_) return;

    // Frameless tool window so the bar stays above the picture and keeps the
    // existing hover, fade, and move tracking. Drawings live in the scene.
    videoControlsBar_->setAttribute(Qt::WA_StyledBackground, true);
    videoControlsBar_->setAttribute(Qt::WA_TranslucentBackground, true);
    videoControlsBar_->setAttribute(Qt::WA_ShowWithoutActivating, true);
    videoControlsBar_->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint |
                                      Qt::WindowDoesNotAcceptFocus | Qt::NoDropShadowWindowHint |
                                      Qt::MSWindowsFixedSizeDialogHint);
    videoControlsBar_->setAutoFillBackground(false);
    videoControlsBar_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    videoControlsBar_->setWindowOpacity(kControlsOverlayActiveOpacity);

    controlsIdleTimer_ = new QTimer(this);
    controlsIdleTimer_->setSingleShot(true);
    controlsIdleTimer_->setInterval(kControlsOverlayIdleMs);
    connect(controlsIdleTimer_, &QTimer::timeout, this, &VideoPlayer::hideControlsOverlay);

    controlsFadeAnimation_ = new QPropertyAnimation(videoControlsBar_, "windowOpacity", this);
    controlsFadeAnimation_->setDuration(kControlsOverlayFadeMs);
    connect(controlsFadeAnimation_, &QPropertyAnimation::finished, this, [this]() {
        if (!videoControlsBar_) return;
        if (videoControlsBar_->windowOpacity() > 0.01) return;
        videoControlsBar_->hide();
        videoControlsBar_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    });
    videoControlsBar_->hide();
}

void VideoPlayer::applyControlsOverlayOpacity(qreal opacity) {
    controlsOverlayOpacity_ = std::clamp(opacity, 0.0, 1.0);
    if (!videoControlsBar_) return;
    videoControlsBar_->setWindowOpacity(controlsOverlayOpacity_);
}

void VideoPlayer::raiseControlsOverlay() {
    if (!videoControlsBar_) return;
    videoControlsBar_->raise();
}

void VideoPlayer::installWindowMoveTracking() {
    QWidget* host = window();
    if (overlayPositionHost_ == host) return;
    if (overlayPositionHost_) {
        overlayPositionHost_->removeEventFilter(this);
    }
    overlayPositionHost_ = host;
    if (overlayPositionHost_ && overlayPositionHost_ != this) {
        overlayPositionHost_->installEventFilter(this);
    }
}

void VideoPlayer::updateControlsOverlayGeometry() {
    if (!videoControlsBar_) return;

    lockOverlayWindowSize(videoControlsBar_);
    const QSize barSize = videoControlsBar_->size();
    const QRect overlayAnchorRect = (videoView_ && videoView_->isVisible())
        ? videoView_->geometry()
        : rect();
    const int x = overlayAnchorRect.x() + qMax(0, (overlayAnchorRect.width() - barSize.width()) / 2);
    const int y = overlayAnchorRect.y()
        + qMax(0, overlayAnchorRect.height() - barSize.height() - kControlsOverlayBottomMarginPx);
    const QPoint globalTopLeft = mapToGlobal(QPoint(x, y));
    videoControlsBar_->move(globalTopLeft);
    ensureOverlayWindowNotResizable(videoControlsBar_);
    raiseControlsOverlay();
}

bool VideoPlayer::isPointerOverControlsBar() const {
    if (!videoControlsBar_ || !videoControlsBar_->isVisible()) return false;
    if (videoControlsBar_->underMouse()) return true;
    return videoControlsBar_->rect().contains(videoControlsBar_->mapFromGlobal(QCursor::pos()));
}

void VideoPlayer::revealControls() {
    if (!controlsChromeEnabled_ || !videoControlsBar_) return;
    if (!isVisible()) return;

    if (controlsFadeAnimation_ && controlsFadeAnimation_->state() == QAbstractAnimation::Running) {
        controlsFadeAnimation_->stop();
    }

    installWindowMoveTracking();
    videoControlsBar_->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    applyControlsOverlayOpacity(kControlsOverlayActiveOpacity);
    updateControlsOverlayGeometry();
    videoControlsBar_->show();
    ensureOverlayWindowNotResizable(videoControlsBar_);
    raiseControlsOverlay();
    startControlsIdleTimer();
}

void VideoPlayer::startControlsIdleTimer() {
    if (!controlsChromeEnabled_ || !controlsIdleTimer_) return;
    if (isPointerOverControlsBar()) {
        controlsIdleTimer_->stop();
        return;
    }
    controlsIdleTimer_->start();
}

void VideoPlayer::hideControlsOverlay() {
    if (!videoControlsBar_ || !controlsFadeAnimation_) return;
    if (!videoControlsBar_->isVisible()) return;
    if (isPointerOverControlsBar()) {
        startControlsIdleTimer();
        return;
    }

    if (controlsFadeAnimation_->state() == QAbstractAnimation::Running) {
        controlsFadeAnimation_->stop();
    }

    controlsFadeAnimation_->setStartValue(videoControlsBar_->windowOpacity());
    controlsFadeAnimation_->setEndValue(0.0);
    controlsFadeAnimation_->start();
}

void VideoPlayer::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    layoutVideoPicture();
    updateControlsOverlayGeometry();
}

void VideoPlayer::layoutVideoPicture() {
    if (telestrationSurface_) telestrationSurface_->relayout();
}

QVideoSink* VideoPlayer::videoSink() const {
    return player_ ? player_->videoSink() : nullptr;
}

void VideoPlayer::setTelestrationSession(Telestration::Session* session) {
    if (telestrationSurface_) telestrationSurface_->setSession(session);
}

void VideoPlayer::setDrawingTagResolver(
    std::function<std::optional<quint64>(qint64 positionMs)> resolver) {
    if (telestrationSurface_) telestrationSurface_->setTagResolver(std::move(resolver));
}

void VideoPlayer::setPresentationDrawingsActive(bool active) {
    if (telestrationSurface_) telestrationSurface_->setPresentationMode(active);
}

void VideoPlayer::setForcedVisibleDrawingId(quint64 drawingId) {
    if (telestrationSurface_) telestrationSurface_->setForcedVisibleDrawingId(drawingId);
}

void VideoPlayer::moveEvent(QMoveEvent* event) {
    QWidget::moveEvent(event);
    updateControlsOverlayGeometry();
}

void VideoPlayer::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    layoutVideoPicture();
    installWindowMoveTracking();
    if (controlsChromeEnabled_) {
        revealControls();
    }
}

void VideoPlayer::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    if (controlsIdleTimer_) controlsIdleTimer_->stop();
    if (controlsFadeAnimation_ && controlsFadeAnimation_->state() == QAbstractAnimation::Running) {
        controlsFadeAnimation_->stop();
    }
    if (videoControlsBar_) {
        videoControlsBar_->hide();
    }
}

void VideoPlayer::enterEvent(QEnterEvent* event) {
    QWidget::enterEvent(event);
    revealControls();
}

void VideoPlayer::leaveEvent(QEvent* event) {
    QWidget::leaveEvent(event);
    startControlsIdleTimer();
}

void VideoPlayer::wireSignals() {
    connect(videoControlsBar_, &VideoControlsBar::playRequested, this, &VideoPlayer::onPlayClicked);
    connect(videoControlsBar_, &VideoControlsBar::pauseRequested, this, &VideoPlayer::onPauseClicked);
    connect(videoControlsBar_, &VideoControlsBar::playbackRateRequested, this,
            &VideoPlayer::onPlaybackRateRequested);
    connect(videoControlsBar_, &VideoControlsBar::muteToggled, this, &VideoPlayer::onMuteToggled);

    connect(videoControlsBar_, &VideoControlsBar::seekRequestedMs, this, [this](qint64 deltaMs) {
        seekByMs(deltaMs);
    });
    connect(videoControlsBar_, &VideoControlsBar::playRequested, this, &VideoPlayer::revealControls);
    connect(videoControlsBar_, &VideoControlsBar::pauseRequested, this, &VideoPlayer::revealControls);
    connect(videoControlsBar_, &VideoControlsBar::seekRequestedMs, this, [this](qint64) {
        revealControls();
    });
    connect(videoControlsBar_, &VideoControlsBar::playbackRateRequested, this, [this](double) {
        revealControls();
    });
    connect(videoControlsBar_, &VideoControlsBar::speedDragStarted, this, [this]() {
        revealControls();
        if (controlsIdleTimer_) controlsIdleTimer_->stop();
    });
    connect(videoControlsBar_, &VideoControlsBar::speedDragFinished, this, [this]() {
        startControlsIdleTimer();
    });
    connect(videoControlsBar_, &VideoControlsBar::muteToggled, this, [this](bool) {
        revealControls();
    });

    // play pause button sensible to state changes:
    connect(player_, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState state) {
        if (videoControlsBar_) videoControlsBar_->setPlaying(state == QMediaPlayer::PlayingState);
        updateStallMonitorForPlaybackState(state);
        if (controlsChromeEnabled_ && state != QMediaPlayer::StoppedState) {
            raiseControlsOverlay();
        }
    });

    connect(player_, &QMediaPlayer::mediaStatusChanged, this, &VideoPlayer::onMediaStatusChanged);

    // Player -> timeline widget
    connect(player_, &QMediaPlayer::durationChanged, this, [this](qint64 durationMs) {
        if (videoTimelineBar_) videoTimelineBar_->setDurationMs(durationMs);
        emit durationChangedMs(durationMs);
    });

    connect(player_, &QMediaPlayer::positionChanged, this, [this](qint64 pos) {
        if (videoTimelineBar_) videoTimelineBar_->setPositionMs(pos);
        emit positionChangedMs(pos);
        noteScrubSeekPosition(pos);
    });

    if (QVideoSink* videoSink = player_->videoSink()) {
        connect(videoSink, &QVideoSink::videoFrameChanged, this,
                [this](const QVideoFrame& frame) {
                    noteScrubSeekFrame(frame.startTime());
                });
    }

    scrubSeekTimeoutTimer_ = new QTimer(this);
    scrubSeekTimeoutTimer_->setSingleShot(true);
    scrubSeekTimeoutTimer_->setInterval(kScrubSeekPresentTimeoutMs);
    connect(scrubSeekTimeoutTimer_, &QTimer::timeout, this, [this]() {
        finishScrubSeekInFlight();
    });

    // Timeline widget -> Player (scrub behavior)
    connect(videoTimelineBar_, &TimelineBar::scrubStarted, this, [this]() {
        scrubResumePlayback_ = false;
        scrubSeekPendingMs_ = -1;
        wasPlayingBeforeScrub_ = (player_->playbackState() == QMediaPlayer::PlayingState);
        if (wasPlayingBeforeScrub_) player_->pause();
    });

    // Live seeking while dragging. TimelineBar already keeps only the newest
    // target; this gate waits until the previous setPosition has presented.
    connect(videoTimelineBar_, &TimelineBar::scrubSeekTo, this, [this](qint64 posMs) {
        enqueueScrubSeek(posMs);
    });

    // Final seek + resume if needed. The resume still follows the drop seek;
    // it waits only when an earlier scrub seek is still in flight.
    connect(videoTimelineBar_, &TimelineBar::scrubFinished, this, [this](qint64 posMs) {
        if (wasPlayingBeforeScrub_) scrubResumePlayback_ = true;
        wasPlayingBeforeScrub_ = false;
        enqueueScrubSeek(posMs);
    });

    // Time-entry seek should pause and stay paused after jumping.
    connect(videoTimelineBar_, &TimelineBar::timeEntryStarted, this, [this]() {
        wasPlayingBeforeScrub_ = false;
        if (player_->playbackState() == QMediaPlayer::PlayingState) {
            player_->pause();
        }
    });

    if (videoView_) videoView_->installEventFilter(this);
    if (videoControlsBar_) {
        videoControlsBar_->setMouseTracking(true);
        videoControlsBar_->installEventFilter(this);
    }
}

void VideoPlayer::buildKeyboardShortcuts() {
    Q_ASSERT(QApplication::instance() != nullptr);

    auto makePlaybackShortcut = [this](const QList<QKeySequence>& shortcuts) {
        auto action = makeQtPtr<QAction>(this);
        action->setShortcuts(shortcuts);
        action->setShortcutContext(Qt::ApplicationShortcut);
        action->setEnabled(false);
        addAction(action.get());
        return action;
    };

    togglePlayPauseAction_ = makePlaybackShortcut({QKeySequence(Qt::Key_Space)});
    connect(togglePlayPauseAction_.get(), &QAction::triggered, this,
            &VideoPlayer::togglePlayPauseWithControlFlash);

    slowerPlaybackAction_ = makePlaybackShortcut({
        QKeySequence(Qt::Key_Minus),
        QKeySequence(Qt::Key_Minus | Qt::KeypadModifier),
    });
    connect(slowerPlaybackAction_.get(), &QAction::triggered, this,
            &VideoPlayer::playbackSlowerWithControlFlash);

    fasterPlaybackAction_ = makePlaybackShortcut({
        QKeySequence(Qt::Key_Plus),
        QKeySequence(Qt::Key_Plus | Qt::KeypadModifier),
        QKeySequence(Qt::SHIFT | Qt::Key_Equal),
    });
    connect(fasterPlaybackAction_.get(), &QAction::triggered, this,
            &VideoPlayer::playbackFasterWithControlFlash);

    resetSpeedAction_ = makePlaybackShortcut({
        QKeySequence(Qt::SHIFT | Qt::Key_BraceRight),
    });
    connect(resetSpeedAction_.get(), &QAction::triggered, this,
            &VideoPlayer::playbackResetSpeedWithControlFlash);

    muteToggleAction_ = makePlaybackShortcut({
        QKeySequence(Qt::SHIFT | Qt::Key_M),
        QKeySequence(Qt::Key_VolumeMute),
    });
    connect(muteToggleAction_.get(), &QAction::triggered, this,
            &VideoPlayer::toggleMuteWithControlFlash);

    seekSmallBackAction_ = makeQtPtr<QAction>(this);
    seekSmallBackAction_->setShortcut(QKeySequence(Qt::Key_Left));
    seekSmallBackAction_->setShortcutContext(Qt::ApplicationShortcut);
    connect(seekSmallBackAction_.get(), &QAction::triggered, this, [this]() {
        revealControls();
        if (videoControlsBar_) videoControlsBar_->flashSeekBackButton();
        onSeekSmallBackward();
    });
    addAction(seekSmallBackAction_.get());

    seekSmallForwardAction_ = makeQtPtr<QAction>(this);
    seekSmallForwardAction_->setShortcut(QKeySequence(Qt::Key_Right));
    seekSmallForwardAction_->setShortcutContext(Qt::ApplicationShortcut);
    connect(seekSmallForwardAction_.get(), &QAction::triggered, this, [this]() {
        revealControls();
        if (videoControlsBar_) videoControlsBar_->flashSeekForwardButton();
        onSeekSmallForward();
    });
    addAction(seekSmallForwardAction_.get());

    seekBigBackAction_ = makeQtPtr<QAction>(this);
    seekBigBackAction_->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_Left));
    seekBigBackAction_->setShortcutContext(Qt::ApplicationShortcut);
    connect(seekBigBackAction_.get(), &QAction::triggered, this, [this]() {
        revealControls();
        if (videoControlsBar_) videoControlsBar_->flashSeekBackButton();
        onSeekBigBackward();
    });
    addAction(seekBigBackAction_.get());

    seekBigForwardAction_ = makeQtPtr<QAction>(this);
    seekBigForwardAction_->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_Right));
    seekBigForwardAction_->setShortcutContext(Qt::ApplicationShortcut);
    connect(seekBigForwardAction_.get(), &QAction::triggered, this, [this]() {
        revealControls();
        if (videoControlsBar_) videoControlsBar_->flashSeekForwardButton();
        onSeekBigForward();
    });
    addAction(seekBigForwardAction_.get());

    updatePlaybackShortcutActionStates();
}

void VideoPlayer::setControlsVisible(bool visible) {
    controlsChromeEnabled_ = visible;
    if (videoView_) videoView_->setVisible(visible);
    if (telestrationSurface_) telestrationSurface_->setToolsVisible(visible);
    if (videoTimelineBar_) videoTimelineBar_->setVisible(visible);

    if (!visible) {
        if (controlsIdleTimer_) controlsIdleTimer_->stop();
        if (controlsFadeAnimation_ && controlsFadeAnimation_->state() == QAbstractAnimation::Running) {
            controlsFadeAnimation_->stop();
        }
        if (videoControlsBar_) {
            videoControlsBar_->hide();
            videoControlsBar_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        }
        applyControlsOverlayOpacity(kControlsOverlayActiveOpacity);
        return;
    }

    revealControls();
}

void VideoPlayer::setControlsEnabled(bool enabled) {
    mediaControlsEnabled_ = enabled;
    if (videoControlsBar_) videoControlsBar_->setEnabledForMedia(enabled);
    if (videoView_) videoView_->setEnabled(enabled);
    if (telestrationSurface_) telestrationSurface_->setToolsEnabled(enabled);
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

void VideoPlayer::setPlaybackShortcutFocusGate(bool allowed) {
    playbackShortcutFocusGate_ = allowed;
    updatePlaybackShortcutActionStates();
}

void VideoPlayer::updatePlaybackShortcutActionStates() {
    const bool shortcutsOn =
        mediaControlsEnabled_ && playbackKeyboardShortcutsEnabled_ && playbackShortcutFocusGate_;
    if (seekSmallBackAction_) seekSmallBackAction_->setEnabled(shortcutsOn);
    if (seekSmallForwardAction_) seekSmallForwardAction_->setEnabled(shortcutsOn);
    if (seekBigBackAction_) seekBigBackAction_->setEnabled(shortcutsOn);
    if (seekBigForwardAction_) seekBigForwardAction_->setEnabled(shortcutsOn);
    if (togglePlayPauseAction_) togglePlayPauseAction_->setEnabled(shortcutsOn);
    if (slowerPlaybackAction_) slowerPlaybackAction_->setEnabled(shortcutsOn);
    if (fasterPlaybackAction_) fasterPlaybackAction_->setEnabled(shortcutsOn);
    if (resetSpeedAction_) resetSpeedAction_->setEnabled(shortcutsOn);
    if (muteToggleAction_) muteToggleAction_->setEnabled(shortcutsOn);
}

void VideoPlayer::seekByMs(qint64 deltaMs) {
    cancelScrubSeekGate();
    const qint64 dur  = player_->duration();
    const qint64 pos  = player_->position();
    const qint64 next = pos + deltaMs;

    const qint64 target = (dur > 0) ? std::clamp(next, qint64{0}, dur) : std::max<qint64>(0, next);
    player_->setPosition(target);
}

void VideoPlayer::enqueueScrubSeek(qint64 positionMs) {
    if (!player_) return;
    const qint64 clampedPositionMs = clampedMediaPositionMs(player_, positionMs);

    if (scrubSeekInFlight_) {
        if (clampedPositionMs == scrubSeekInFlightTargetMs_) {
            // The newest target is the seek already running. Do not abort it.
            scrubSeekPendingMs_ = -1;
            resumeScrubPlaybackIfRequested();
            return;
        }
        scrubSeekPendingMs_ = clampedPositionMs;
        return;
    }

    if (scrubSeekIssuePosted_) {
        scrubSeekPendingMs_ = clampedPositionMs;
        return;
    }

    issueScrubSeek(clampedPositionMs);
}

void VideoPlayer::issueScrubSeek(qint64 positionMs) {
    if (!player_) return;
    const qint64 clampedPositionMs = clampedMediaPositionMs(player_, positionMs);
    if (scrubSeekInFlight_) {
        scrubSeekPendingMs_ = clampedPositionMs;
        return;
    }

    scrubSeekPendingMs_ = -1;
    scrubSeekInFlight_ = true;
    scrubSeekInFlightTargetMs_ = clampedPositionMs;
    scrubSeekWatchingForFrame_ = false;
    scrubSeekPositionAtIssueMs_ = player_->position();
    scrubSeekFrameStartAtIssueUs_ = -1;
    if (QVideoSink* videoSink = player_->videoSink()) {
        scrubSeekFrameStartAtIssueUs_ = videoSink->videoFrame().startTime();
    }
    if (scrubSeekTimeoutTimer_) {
        scrubSeekTimeoutTimer_->start(kScrubSeekPresentTimeoutMs);
    }

    player_->setPosition(clampedPositionMs);
    if (!scrubSeekInFlight_ || scrubSeekInFlightTargetMs_ != clampedPositionMs) {
        return;
    }

    scrubSeekWatchingForFrame_ = true;
    resumeScrubPlaybackIfRequested();
    if (!scrubSeekInFlight_ || scrubSeekInFlightTargetMs_ != clampedPositionMs) {
        return;
    }

    // Already on the requested frame: nothing will signal completion.
    if (absoluteDifference(player_->position(), clampedPositionMs) <= kScrubSeekPresentToleranceMs) {
        QTimer::singleShot(0, this, [this, clampedPositionMs]() {
            if (!scrubSeekInFlight_ || scrubSeekInFlightTargetMs_ != clampedPositionMs) return;
            finishScrubSeekInFlight();
        });
    }
}

void VideoPlayer::finishScrubSeekInFlight() {
    if (!scrubSeekInFlight_) return;
    scrubSeekInFlight_ = false;
    scrubSeekWatchingForFrame_ = false;
    scrubSeekInFlightTargetMs_ = -1;
    scrubSeekPositionAtIssueMs_ = -1;
    scrubSeekFrameStartAtIssueUs_ = -1;
    if (scrubSeekTimeoutTimer_) scrubSeekTimeoutTimer_->stop();

    if (scrubSeekPendingMs_ >= 0) {
        schedulePendingScrubSeek();
        return;
    }
    resumeScrubPlaybackIfRequested();
}

void VideoPlayer::schedulePendingScrubSeek() {
    if (scrubSeekIssuePosted_) return;
    scrubSeekIssuePosted_ = true;
    QTimer::singleShot(0, this, [this]() {
        scrubSeekIssuePosted_ = false;
        if (!player_ || scrubSeekInFlight_) return;
        if (scrubSeekPendingMs_ < 0) {
            resumeScrubPlaybackIfRequested();
            return;
        }
        const qint64 nextPositionMs = scrubSeekPendingMs_;
        scrubSeekPendingMs_ = -1;
        issueScrubSeek(nextPositionMs);
    });
}

void VideoPlayer::resumeScrubPlaybackIfRequested() {
    if (!scrubResumePlayback_ || !player_) return;
    scrubResumePlayback_ = false;
    if (player_->playbackState() != QMediaPlayer::PlayingState) {
        player_->play();
    }
}

void VideoPlayer::cancelScrubSeekGate() {
    scrubSeekInFlight_ = false;
    scrubSeekWatchingForFrame_ = false;
    scrubResumePlayback_ = false;
    scrubSeekInFlightTargetMs_ = -1;
    scrubSeekPendingMs_ = -1;
    scrubSeekPositionAtIssueMs_ = -1;
    scrubSeekFrameStartAtIssueUs_ = -1;
    if (scrubSeekTimeoutTimer_) scrubSeekTimeoutTimer_->stop();
}

void VideoPlayer::noteScrubSeekPosition(qint64 positionMs) {
    if (!scrubSeekInFlight_) return;
    if (absoluteDifference(positionMs, scrubSeekInFlightTargetMs_) > kScrubSeekPresentToleranceMs) {
        return;
    }
    finishScrubSeekInFlight();
}

void VideoPlayer::noteScrubSeekFrame(qint64 frameStartUs) {
    if (!player_ || !scrubSeekInFlight_ || !scrubSeekWatchingForFrame_) return;
    if (frameStartUs < 0 || frameStartUs == scrubSeekFrameStartAtIssueUs_) return;

    const qint64 framePositionMs = frameStartUs / 1000;
    const qint64 targetPositionMs = scrubSeekInFlightTargetMs_;
    if (absoluteDifference(framePositionMs, targetPositionMs) <= kScrubSeekPresentToleranceMs) {
        finishScrubSeekInFlight();
        return;
    }

    // A long-GOP seek presents the nearest keyframe, which can be far from the
    // requested time. Count that frame once it is closer to the target than
    // the picture we left, and ignore a stale frame still showing the old one.
    const qint64 distanceFromIssueMs =
        absoluteDifference(scrubSeekPositionAtIssueMs_, targetPositionMs);
    const qint64 playheadPositionMs = player_->position();
    const bool frameMovedTowardTarget =
        absoluteDifference(framePositionMs, targetPositionMs) < distanceFromIssueMs;
    const bool playheadMovedTowardTarget =
        playheadPositionMs != scrubSeekPositionAtIssueMs_
        && absoluteDifference(playheadPositionMs, targetPositionMs) < distanceFromIssueMs;
    if (frameMovedTowardTarget || playheadMovedTowardTarget) {
        finishScrubSeekInFlight();
    }
}

void VideoPlayer::onPlayClicked() { player_->play(); }
void VideoPlayer::onPauseClicked() { player_->pause(); }
void VideoPlayer::onSeekSmallBackward() { seekByMs(-kSeekSmallMs); }
void VideoPlayer::onSeekSmallForward() { seekByMs(+kSeekSmallMs); }
void VideoPlayer::onSeekBigBackward() { seekByMs(-kSeekBigMs); }
void VideoPlayer::onSeekBigForward() { seekByMs(+kSeekBigMs); }

void VideoPlayer::applyPlaybackRate(double rate) {
    if (!player_) return;
    playbackRate_ = PlaybackRates::snap(rate);
    player_->setPlaybackRate(playbackRate_);
    if (videoControlsBar_) videoControlsBar_->setPlaybackRate(playbackRate_);
}

void VideoPlayer::onPlaybackRateRequested(double rate) {
    applyPlaybackRate(rate);
}

void VideoPlayer::onMuteToggled(bool muted) {
    audioOutput_->setMuted(muted);
    if (videoControlsBar_) videoControlsBar_->setMuted(muted);
    emit muteStateChanged(muted);
}

void VideoPlayer::toggleMuteWithControlFlash() {
    if (!videoControlsBar_ || !mediaControlsEnabled_) return;
    emit muteToolbarFlashRequested();
    videoControlsBar_->toggleMute();
}

void VideoPlayer::onTogglePlayPause() {
    if (!player_) return;
    const auto state = player_->playbackState();
    (state == QMediaPlayer::PlayingState) ? player_->pause() : player_->play();
}

void VideoPlayer::togglePlayPauseWithControlFlash() {
    if (!player_) return;
    revealControls();
    const auto state = player_->playbackState();
    if (videoControlsBar_) {
        videoControlsBar_->flashPlayPauseButton();
    }
    onTogglePlayPause();
}

bool VideoPlayer::isPlaying() const {
    return player_ && player_->playbackState() == QMediaPlayer::PlayingState;
}

void VideoPlayer::playWithControlFlash() {
    if (!player_) return;
    if (player_->playbackState() == QMediaPlayer::PlayingState) return;
    revealControls();
    if (videoControlsBar_) videoControlsBar_->flashPlayPauseButton();
    onPlayClicked();
}

void VideoPlayer::pauseWithControlFlash() {
    if (!player_) return;
    if (player_->playbackState() != QMediaPlayer::PlayingState) return;
    revealControls();
    if (videoControlsBar_) videoControlsBar_->flashPlayPauseButton();
    onPauseClicked();
}

void VideoPlayer::playbackSlowerWithControlFlash() {
    revealControls();
    if (videoControlsBar_) videoControlsBar_->flashSpeedometer();
    applyPlaybackRate(PlaybackRates::slower(playbackRate_));
}

void VideoPlayer::playbackFasterWithControlFlash() {
    revealControls();
    if (videoControlsBar_) videoControlsBar_->flashSpeedometer();
    applyPlaybackRate(PlaybackRates::faster(playbackRate_));
}

void VideoPlayer::playbackResetSpeedWithControlFlash() {
    revealControls();
    if (videoControlsBar_) videoControlsBar_->flashSpeedometer();
    applyPlaybackRate(PlaybackRates::kResetRate);
}

void VideoPlayer::setPlaybackRateAndPlay(double rate) {
    applyPlaybackRate(rate);
    player_->play();
}

void VideoPlayer::setupPlaybackReliabilityHooks() {
    playbackStallTimer_ = new QTimer(this);
    playbackStallTimer_->setInterval(3500);
    connect(playbackStallTimer_, &QTimer::timeout, this, &VideoPlayer::onPlaybackStallTimeout);

    if (QGuiApplication::instance() != nullptr) {
        connect(qGuiApp, &QGuiApplication::applicationStateChanged, this,
                [this](Qt::ApplicationState state) {
                    if (state != Qt::ApplicationActive || !player_ || loadedSourcePath_.isEmpty()) return;
                    if (userRequestedPlaying_ &&
                        player_->playbackState() != QMediaPlayer::PlayingState) {
                        player_->play();
                    }
                    resetPlaybackStallWatchdog();
                    pipelineReloadsWithoutProgress_ = 0;
                });
    }

    connect(player_, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error error, const QString& /*errorString*/) {
                recoverFromPlaybackBackendError(error);
            });

    if (mediaDevices_) {
        connect(mediaDevices_, &QMediaDevices::audioOutputsChanged,
                this, &VideoPlayer::onAudioOutputsChanged);
    }
}

void VideoPlayer::updateStallMonitorForPlaybackState(QMediaPlayer::PlaybackState state) {
    if (!playbackStallTimer_) return;
    userRequestedPlaying_ = (state == QMediaPlayer::PlayingState);
    if (state == QMediaPlayer::PlayingState) {
        // Resample on the next tick so a recovery play() is not compared against the
        // frozen pre-nudge position. Do not clear consecutivePlaybackStallTicks_ here:
        // nudge/play would wipe the counter and the reload path would never run.
        lastStallCheckPositionMs_ = -1;
        playbackStallTimer_->start();
    } else {
        playbackStallTimer_->stop();
    }
}

void VideoPlayer::resetPlaybackStallWatchdog() {
    consecutivePlaybackStallTicks_ = 0;
    lastStallCheckPositionMs_ = -1;
}

void VideoPlayer::onPlaybackStallTimeout() {
    if (!player_ || loadedSourcePath_.isEmpty()) return;
    if (player_->playbackState() != QMediaPlayer::PlayingState) return;

    const qint64 durationMs = player_->duration();
    const qint64 positionMs = player_->position();
    if (durationMs <= 0) return;

    if (player_->mediaStatus() == QMediaPlayer::EndOfMedia ||
        isNearPlaybackEnd(positionMs, durationMs)) {
        resetPlaybackStallWatchdog();
        pipelineReloadsWithoutProgress_ = 0;
        return;
    }

    if (lastStallCheckPositionMs_ < 0) {
        lastStallCheckPositionMs_ = positionMs;
        return;
    }

    if (positionMs != lastStallCheckPositionMs_) {
        resetPlaybackStallWatchdog();
        pipelineReloadsWithoutProgress_ = 0;
        lastStallCheckPositionMs_ = positionMs;
        return;
    }

    if (pipelineReloadsWithoutProgress_ >= kMaxPipelineReloadsWithoutProgress) return;

    ++consecutivePlaybackStallTicks_;
    if (consecutivePlaybackStallTicks_ >= kStallTicksBeforeReload) {
        reloadCurrentMediaFromDisk();
    } else {
        nudgePlaybackAfterBackendStall();
    }
}

void VideoPlayer::recoverFromPlaybackBackendError(QMediaPlayer::Error error) {
    if (error == QMediaPlayer::NoError || !player_ || loadedSourcePath_.isEmpty()) return;

    const qint64 durationMs = player_->duration();
    const qint64 positionMs = player_->position();
    if (durationMs <= 0) return;
    if (player_->mediaStatus() == QMediaPlayer::EndOfMedia ||
        isNearPlaybackEnd(positionMs, durationMs)) {
        resetPlaybackStallWatchdog();
        pipelineReloadsWithoutProgress_ = 0;
        return;
    }

    if (pipelineReloadsWithoutProgress_ >= kMaxPipelineReloadsWithoutProgress) return;

    if (consecutivePlaybackStallTicks_ == 0) {
        consecutivePlaybackStallTicks_ = 1;
        nudgePlaybackAfterBackendStall();
        return;
    }
    reloadCurrentMediaFromDisk();
}

void VideoPlayer::nudgePlaybackAfterBackendStall() {
    if (!player_ || loadedSourcePath_.isEmpty()) return;

    const qint64 positionMs = player_->position();
    const qint64 durationMs = player_->duration();
    if (isNearPlaybackEnd(positionMs, durationMs)) {
        resetPlaybackStallWatchdog();
        pipelineReloadsWithoutProgress_ = 0;
        return;
    }

    // pause() emits playbackStateChanged synchronously, and
    // updateStallMonitorForPlaybackState() would otherwise clear userRequestedPlaying_
    // before we can resume. Snapshot intent the same way scrub does.
    const bool wasPlayingBeforeNudge =
        userRequestedPlaying_ || player_->playbackState() == QMediaPlayer::PlayingState;
    qint64 bumpedPositionMs = positionMs + 1;
    if (durationMs > 0) {
        bumpedPositionMs = std::min(bumpedPositionMs, durationMs);
    }

    cancelScrubSeekGate();
    player_->pause();
    player_->setPosition(bumpedPositionMs);
    userRequestedPlaying_ = wasPlayingBeforeNudge;
    if (wasPlayingBeforeNudge) player_->play();
}

void VideoPlayer::reloadCurrentMediaFromDisk() {
    if (!player_ || loadedSourcePath_.isEmpty()) return;

    const qint64 resumePositionMs = player_->position();
    const qint64 durationMs = player_->duration();
    if (isNearPlaybackEnd(resumePositionMs, durationMs)) {
        resetPlaybackStallWatchdog();
        pipelineReloadsWithoutProgress_ = 0;
        return;
    }
    if (pipelineReloadsWithoutProgress_ >= kMaxPipelineReloadsWithoutProgress) {
        resetPlaybackStallWatchdog();
        return;
    }

    ++pipelineReloadsWithoutProgress_;
    reloadCurrentMediaFromDisk(resumePositionMs, playbackRate_, userRequestedPlaying_);
}

void VideoPlayer::setMediaSourceFromPath(const QString& sourcePath,
                                         qint64 resumePositionMs,
                                         double rate,
                                         bool resumePlaying) {
    if (!player_ || sourcePath.isEmpty()) return;
    cancelScrubSeekGate();

    pendingMediaSession_.active = true;
    pendingMediaSession_.resumePositionMs = resumePositionMs;
    pendingMediaSession_.playbackRate = rate;
    pendingMediaSession_.resumePlaying = resumePlaying;

    player_->stop();
    player_->setSource(QUrl());
    player_->setSource(QUrl::fromLocalFile(sourcePath));
}

void VideoPlayer::onMediaStatusChanged(QMediaPlayer::MediaStatus status) {
    if (pendingMediaSession_.active) {
        switch (status) {
            case QMediaPlayer::LoadedMedia:
            case QMediaPlayer::BufferedMedia:
                finalizePendingMediaSession();
                break;
            case QMediaPlayer::InvalidMedia:
                pendingMediaSession_.active = false;
                break;
            default:
                break;
        }
    }

    if (controlsChromeEnabled_ &&
        (status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::BufferedMedia)) {
        QTimer::singleShot(0, this, [this]() {
            if (controlsChromeEnabled_) {
                revealControls();
            }
        });
    }

    switch (status) {
        case QMediaPlayer::NoMedia:
        case QMediaPlayer::InvalidMedia:
        case QMediaPlayer::LoadedMedia:
        case QMediaPlayer::BufferedMedia:
        case QMediaPlayer::EndOfMedia:
            emit playbackMediaSettled();
            break;
        default:
            break;
    }
}

void VideoPlayer::finalizePendingMediaSession() {
    if (!pendingMediaSession_.active || !player_) return;

    const PendingMediaSession session = pendingMediaSession_;
    pendingMediaSession_.active = false;

    playbackRate_ = PlaybackRates::snap(session.playbackRate);
    player_->setPlaybackRate(playbackRate_);
    if (videoControlsBar_) videoControlsBar_->setPlaybackRate(playbackRate_);

    const qint64 duration = player_->duration();
    const qint64 resumePositionMs = duration > 0
        ? std::clamp(session.resumePositionMs, qint64{0}, duration)
        : std::max<qint64>(0, session.resumePositionMs);
    player_->setPosition(resumePositionMs);
    userRequestedPlaying_ = session.resumePlaying;
    if (session.resumePlaying) player_->play();

    if (controlsChromeEnabled_) {
        revealControls();
    }

    // Reset stall watchdog so the freshly rebuilt pipeline is not flagged as stalled
    // due to stale position samples from before the reload.
    resetPlaybackStallWatchdog();
}

void VideoPlayer::reloadCurrentMediaFromDisk(qint64 resumePositionMs, double rate, bool resumePlaying) {
    if (!player_ || loadedSourcePath_.isEmpty()) return;
    setMediaSourceFromPath(loadedSourcePath_, resumePositionMs, rate, resumePlaying);
}

void VideoPlayer::registerSystemPowerObservers() {
    avaInstallSystemPowerObservers(this,
                                   &VideoPlayer::systemWillSleepCallback,
                                   &VideoPlayer::systemDidWakeCallback);
}

void VideoPlayer::unregisterSystemPowerObservers() {
    avaRemoveSystemPowerObservers(this);
}

void VideoPlayer::systemWillSleepCallback(void* context) {
    if (auto* self = static_cast<VideoPlayer*>(context)) {
        self->onSystemWillSleep();
    }
}

void VideoPlayer::systemDidWakeCallback(void* context) {
    if (auto* self = static_cast<VideoPlayer*>(context)) {
        self->onSystemDidWake();
    }
}

void VideoPlayer::onSystemWillSleep() {
    if (!player_ || loadedSourcePath_.isEmpty()) return;

    // Capture the playback snapshot now while the pipeline is still healthy. After wake,
    // QMediaPlayer's reported state on macOS is unreliable (position() may stop advancing
    // while playbackState() lies, or play()/setPosition() silently no-op) so we cannot
    // reconstruct what the user wanted from a freshly woken player.
    const auto state = player_->playbackState();
    sleepRecoveryWasPlaying_ = userRequestedPlaying_ || state == QMediaPlayer::PlayingState;
    sleepRecoveryPositionMs_ = player_->position();
    sleepRecoveryRate_ = playbackRate_;
    sleepRecoveryPending_ = true;
}

void VideoPlayer::onSystemDidWake() {
    // Will-sleep captures the snapshot while the pipeline is still healthy. If did-wake
    // arrives first (observer installed mid-sleep), capture now so recovery never reads
    // live player fields that were only initialized in the constructor.
    if (!sleepRecoveryPending_) {
        onSystemWillSleep();
    }
    if (!sleepRecoveryPending_ || !player_ || loadedSourcePath_.isEmpty()) {
        sleepRecoveryPending_ = false;
        return;
    }

    const qint64 targetPositionMs = sleepRecoveryPositionMs_;
    const double targetRate = sleepRecoveryRate_;
    const bool resumePlaying = sleepRecoveryWasPlaying_;
    sleepRecoveryPending_ = false;
    pipelineReloadsWithoutProgress_ = 0;

    // Defer the rebuild briefly so CoreAudio and the AVFoundation render pipeline have time
    // to come back online after wake before we hand them a new source. Reloading immediately
    // sometimes results in the new pipeline also coming up in a half-initialised state.
    constexpr int kSleepRecoveryReloadDelayMs = 500;
    QTimer::singleShot(kSleepRecoveryReloadDelayMs, this,
                       [this, targetPositionMs, targetRate, resumePlaying]() {
                           if (!player_ || loadedSourcePath_.isEmpty()) return;
                           reloadCurrentMediaFromDisk(targetPositionMs, targetRate, resumePlaying);
                       });
}

void VideoPlayer::releaseLoadedMedia() {
    if (!player_) return;
    cancelScrubSeekGate();

    const bool hadMedia = !loadedSourcePath_.isEmpty() || !player_->source().isEmpty();
    userRequestedPlaying_ = false;
    pendingMediaSession_.active = false;
    sleepRecoveryPending_ = false;
    pipelineReloadsWithoutProgress_ = 0;
    resetPlaybackStallWatchdog();
    if (playbackStallTimer_) playbackStallTimer_->stop();
    if (!hadMedia) return;

    avaEndPlaybackUserActivity();
    loadedSourcePath_.clear();
    wasPlayingBeforeScrub_ = false;
    player_->stop();
    if (!player_->source().isEmpty()) {
        player_->setSource(QUrl());
    }
    if (videoTimelineBar_) videoTimelineBar_->reset();
}

QString VideoPlayer::activeMediaFilePath() const {
    if (!loadedSourcePath_.isEmpty()) return loadedSourcePath_;
    if (!player_ || !player_->source().isLocalFile()) return {};
    return player_->source().toLocalFile();
}

void VideoPlayer::loadVideoFromFile(const QString& filePath) {
    if (filePath.isEmpty()) return;

    if (!loadedSourcePath_.isEmpty() && loadedSourcePath_ != filePath) {
        avaEndPlaybackUserActivity();
    }

    setControlsVisible(true);
    if (videoControlsBar_) videoControlsBar_->setEnabledForMedia(false);
    
    // Reset timeline UI immediately; durationChanged will set real range later
    cancelScrubSeekGate();
    wasPlayingBeforeScrub_ = false;
    resetPlaybackStallWatchdog();
    pipelineReloadsWithoutProgress_ = 0;
    
    if (videoTimelineBar_) videoTimelineBar_->reset();
    audioOutput_->setMuted(false);
    if (videoControlsBar_) videoControlsBar_->setMuted(false);
    emit muteStateChanged(false);
    
    playbackRate_ = PlaybackRates::kResetRate;
    if (videoControlsBar_) videoControlsBar_->setPlaybackRate(playbackRate_);
    
    loadedSourcePath_ = filePath;
    avaBeginPlaybackUserActivity();

    setControlsEnabled(true);
    setMediaSourceFromPath(filePath, 0, 1.0, true);
}

void VideoPlayer::onAudioOutputsChanged() {
    if (!audioOutput_ || !player_ || loadedSourcePath_.isEmpty()) return;

    const QAudioDevice defaultDevice = mediaDevices_
        ? mediaDevices_->defaultAudioOutput()
        : QAudioDevice();
    if (defaultDevice.isNull()) return;
    if (audioOutput_->device() == defaultDevice) return;

    audioOutput_->setDevice(defaultDevice);
    resetPlaybackStallWatchdog();

    if (!userRequestedPlaying_) return;
    if (player_->playbackState() == QMediaPlayer::PlayingState) return;
    player_->play();
}

bool VideoPlayer::eventFilter(QObject* obj, QEvent* event) {
    if (obj == overlayPositionHost_ &&
        (event->type() == QEvent::Move || event->type() == QEvent::Resize)) {
        updateControlsOverlayGeometry();
    }
    const bool onPicture = obj == videoView_ || obj == videoViewport_;
    if (onPicture) {
        if (event->type() == QEvent::Show || event->type() == QEvent::Resize) {
            layoutVideoPicture();
            if (event->type() == QEvent::Show) {
                QTimer::singleShot(0, this, [this]() {
                    if (controlsChromeEnabled_) {
                        revealControls();
                    }
                });
            }
        } else if (event->type() == QEvent::MouseMove || event->type() == QEvent::Enter) {
            revealControls();
        }
        if (telestrationSurface_ && telestrationSurface_->handleViewportEvent(obj, event)) {
            return true;
        }
        if (event->type() == QEvent::MouseButtonPress) {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                onTogglePlayPause();
                return true;
            }
        }
    } else if (obj == videoControlsBar_ &&
               (event->type() == QEvent::MouseMove || event->type() == QEvent::Enter)) {
        revealControls();
    }
    return QWidget::eventFilter(obj, event);
}
