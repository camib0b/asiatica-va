#include "WorkWindow.h"
#include "../style/StyleProps.h"
#include "VideoControlsBar.h"
#include "TimelineBar.h"

#include <QtGlobal>
#include <QLabel>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QAudioOutput>
#include <QFileDialog>
#include <QMediaPlayer>
#include <QUrl>
#include <QVideoWidget>
#include <QAction>
#include <QKeySequence>
#include <QApplication>
#include <QToolButton>
#include <QMenu>
#include <algorithm>


namespace {
    constexpr double kMinRate  = 0.25;
    constexpr double kMaxRate  = 4.0;
    constexpr double kRateStep = 0.25;
  
    constexpr qint64 kSeekSmallMs = 250;
    constexpr qint64 kSeekBigMs   = 3000;
  
} // namespace

WorkWindow::WorkWindow(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_StyledBackground, true);
    buildUi();
    wireSignals();
    buildKeyboardShortcuts();
    setControlsEnabled(false);
}

void WorkWindow::buildUi() {
    setObjectName("AppRoot");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(12);

    // header:
    headerLabel_ = new QLabel("this is ava", this);
    headerLabel_->setWordWrap(true);
    Style::setRole(headerLabel_, "h1");

    // video-file-management button
    videoMenuButton_ = new QToolButton(this);
    videoMenuButton_->setText("Video Manager");
    videoMenuButton_->setMinimumWidth(100);
    Style::setVariant(videoMenuButton_, "ghost");
    Style::setSize(videoMenuButton_, "sm");
    videoMenuButton_->setPopupMode(QToolButton::InstantPopup);
    videoMenuButton_->setCursor(Qt::PointingHandCursor);

    videoMenu_ = new QMenu(videoMenuButton_);
    replaceVideoAction_ = videoMenu_->addAction("Replace video with another one");
    discardVideoAction_ = videoMenu_->addAction("Close current video");
    videoMenuButton_->setMenu(videoMenu_);

    // video widget:
    videoWidget_ = new QVideoWidget(this);
    videoWidget_->setMinimumHeight(360);
    videoWidget_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    
    videoControlsBar_ = new VideoControlsBar(this);
    videoTimelineBar_ = new TimelineBar(this);

    // media player and audio output:
    player_ = new QMediaPlayer(this);
    audioOutput_ = new QAudioOutput(this);
    player_->setAudioOutput(audioOutput_);
    player_->setVideoOutput(videoWidget_);
    
    // Header row: left (header), right (Video menu)
    auto* headerRow = new QWidget(this);
    auto* headerRowLayout = new QHBoxLayout(headerRow);
    headerRowLayout->setContentsMargins(0, 0, 0, 0);
    headerRowLayout->setSpacing(12);
    
    auto* headerTextCol = new QWidget(headerRow);
    auto* headerTextLayout = new QVBoxLayout(headerTextCol);
    headerTextLayout->setContentsMargins(0, 0, 0, 0);
    headerTextLayout->setSpacing(6);
    
    headerTextLayout->addWidget(headerLabel_);
    
    headerRowLayout->addWidget(headerTextCol, /*stretch=*/1);
    headerRowLayout->addWidget(videoMenuButton_, /*stretch=*/0, Qt::AlignTop | Qt::AlignRight);
    
    // the rest of the layout, stacked vertically:
    layout->addWidget(headerRow);
    layout->addWidget(videoControlsBar_);
    layout->addWidget(videoTimelineBar_);
    layout->addWidget(videoWidget_, /*stretch=*/1);

    // initial visibility: hidden until video is loaded
    if (videoWidget_) videoWidget_->hide();
    if (videoControlsBar_) videoControlsBar_->hide();
    if (videoTimelineBar_) videoTimelineBar_->hide();
}

void WorkWindow::wireSignals() {
    connect(videoControlsBar_, &VideoControlsBar::playRequested, this, &WorkWindow::onPlayClicked);
    connect(videoControlsBar_, &VideoControlsBar::pauseRequested, this, &WorkWindow::onPauseClicked);
    connect(videoControlsBar_, &VideoControlsBar::slowerRequested, this, &WorkWindow::onSlowerClicked);
    connect(videoControlsBar_, &VideoControlsBar::fasterRequested, this, &WorkWindow::onFasterClicked);
    connect(videoControlsBar_, &VideoControlsBar::resetSpeedRequested, this, &WorkWindow::onResetSpeedClicked);
    connect(videoControlsBar_, &VideoControlsBar::muteToggled, this, &WorkWindow::onMuteToggled);

    connect(videoControlsBar_, &VideoControlsBar::seekRequestedMs, this, [this](qint64 deltaMs) {
        seekByMs(deltaMs);
    });

    // play pause button sensible to state changes:
    connect(player_, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState state) {
        if (videoControlsBar_) videoControlsBar_->setPlaying(state == QMediaPlayer::PlayingState);
    });  
        
    // for file-managing button:
    connect(replaceVideoAction_, &QAction::triggered, this, &WorkWindow::onReplaceVideo);
    connect(discardVideoAction_, &QAction::triggered, this, &WorkWindow::onDiscardVideo);

    // Player -> timeline widget
    connect(player_, &QMediaPlayer::durationChanged, this, [this](qint64 dur) {
        durationMs_ = dur;
        if (videoTimelineBar_) videoTimelineBar_->setDurationMs(dur);
    });

    connect(player_, &QMediaPlayer::positionChanged, this, [this](qint64 pos) {
        if (videoTimelineBar_) videoTimelineBar_->setPositionMs(pos);
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
}

void WorkWindow::buildKeyboardShortcuts() {
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
  
    slowerAction_ = makeAction(QKeySequence(Qt::Key_BraceLeft), &WorkWindow::onSlowerClicked);
    fasterAction_ = makeAction(QKeySequence(Qt::Key_BraceRight), &WorkWindow::onFasterClicked);
    resetSpeedAction_ = makeAction(QKeySequence(Qt::Key_Backslash), &WorkWindow::onResetSpeedClicked);
  
    togglePlayPauseAction_ = makeAction(QKeySequence(Qt::Key_Space), &WorkWindow::onTogglePlayPause);
    
    // Arrows: small seek
    seekSmallBackAction_ = makeAction(QKeySequence(Qt::Key_Left), &WorkWindow::onSeekSmallBackward);
    seekSmallForwardAction_ = makeAction(QKeySequence(Qt::Key_Right), &WorkWindow::onSeekSmallForward);
    
    // Shift + Arrows: big seek
    seekBigBackAction_ = makeAction(QKeySequence(Qt::SHIFT | Qt::Key_Left), &WorkWindow::onSeekBigBackward);
    seekBigForwardAction_ = makeAction(QKeySequence(Qt::SHIFT | Qt::Key_Right), &WorkWindow::onSeekBigForward);
    
    slowerAction_->setEnabled(false);
    fasterAction_->setEnabled(false);
    resetSpeedAction_->setEnabled(false);
    togglePlayPauseAction_->setEnabled(false);
    seekSmallBackAction_->setEnabled(false);
    seekSmallForwardAction_->setEnabled(false);
    seekBigBackAction_->setEnabled(false);
    seekBigForwardAction_->setEnabled(false);
}

void WorkWindow::setControlsVisible(bool visible) {
    if (videoControlsBar_) videoControlsBar_->setVisible(visible);
    if (videoTimelineBar_) videoTimelineBar_->setVisible(visible);
    if (videoWidget_) videoWidget_->setVisible(visible);
}

void WorkWindow::setControlsEnabled(bool enabled) {
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

void WorkWindow::seekByMs(qint64 deltaMs) {
    const qint64 dur  = player_->duration();
    const qint64 pos  = player_->position();
    const qint64 next = pos + deltaMs;

    const qint64 target = (dur > 0) ? std::clamp(next, qint64{0}, dur) : std::max<qint64>(0, next);
    player_->setPosition(target);
}

void WorkWindow::onPlayClicked() { player_->play(); }
void WorkWindow::onPauseClicked() { player_->pause(); }
void WorkWindow::onSeekSmallBackward() { seekByMs(-kSeekSmallMs); }
void WorkWindow::onSeekSmallForward() { seekByMs(+kSeekSmallMs); }
void WorkWindow::onSeekBigBackward() { seekByMs(-kSeekBigMs); }
void WorkWindow::onSeekBigForward() { seekByMs(+kSeekBigMs); }

void WorkWindow::onSlowerClicked() {
    playbackRate_ = std::max(kMinRate, playbackRate_ - kRateStep);
    player_->setPlaybackRate(playbackRate_);
    if (videoControlsBar_) videoControlsBar_->setPlaybackRate(playbackRate_);
}

void WorkWindow::onResetSpeedClicked() {
    playbackRate_ = 1.0;
    player_->setPlaybackRate(playbackRate_);
    if (videoControlsBar_) videoControlsBar_->setPlaybackRate(playbackRate_);
}

void WorkWindow::onFasterClicked() {
    playbackRate_ = std::min(kMaxRate, playbackRate_ + kRateStep);
    player_->setPlaybackRate(playbackRate_);
    if (videoControlsBar_) videoControlsBar_->setPlaybackRate(playbackRate_);
}

void WorkWindow::onMuteToggled(bool muted) {
    audioOutput_->setMuted(muted);
    if (videoControlsBar_) videoControlsBar_->setMuted(muted); 
}

void WorkWindow::onTogglePlayPause() {
    if (!player_) return;
    const auto state = player_->playbackState();
    (state == QMediaPlayer::PlayingState) ? player_->pause() : player_->play();
}

void WorkWindow::setPlaybackRateAndPlay(double rate) {
    playbackRate_ = rate;
    player_->setPlaybackRate(playbackRate_);
    if (videoControlsBar_) videoControlsBar_->setPlaybackRate(playbackRate_);
    player_->play();
}

QString WorkWindow::formatMs(qint64 ms) {
    if (ms < 0) ms = 0;
    const qint64 hours = ms / 3600000;
    ms %= 3600000;
    const qint64 minutes = ms / 60000;
    ms %= 60000;
    const qint64 seconds = ms / 1000;
    const qint64 millis = ms % 1000;
  
    if (hours > 0) {
      return QString("%1:%2:%3.%4")
          .arg(hours, 1, 10)
          .arg(minutes, 2, 10, QChar('0'))
          .arg(seconds, 2, 10, QChar('0'))
          .arg(millis, 3, 10, QChar('0'));
    }
    return QString("%1:%2.%3")
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'))
        .arg(millis, 3, 10, QChar('0'));
}

QString WorkWindow::promptForVideoFile() {
    return QFileDialog::getOpenFileName(
        this,
        "Select a video file",
        QString(),
        "Video files (*.mp4 *.mov *.m4v *.mkv *.avi);;All files (*.*)"
    );
}

void WorkWindow::loadVideoFromFile(const QString& filePath) {
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

void WorkWindow::onReplaceVideo() {
    const QString filePath = promptForVideoFile();
    if (filePath.isEmpty()) return;
    loadVideoFromFile(filePath);
}

void WorkWindow::onDiscardVideo() {
    emit videoClosed();
}
