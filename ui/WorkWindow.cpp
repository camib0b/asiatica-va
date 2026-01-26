#include "WorkWindow.h"
#include "../style/StyleProps.h"
#include "../components/VideoPlayer.h"
#include "../components/GameControls.h"
#include "../state/TagSession.h"

#include "VideoControlsBar.h"
#include "TimelineBar.h"

#include <QLabel>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QToolButton>
#include <QMenu>
#include <QVideoWidget>
#include <QListWidget>

namespace {
QString formatTimestampMs(qint64 ms) {
    if (ms < 0) ms = 0;
    const qint64 totalSeconds = ms / 1000;
    const qint64 millis = ms % 1000;
    const qint64 hours = totalSeconds / 3600;
    const qint64 minutes = (totalSeconds / 60) % 60;
    const qint64 seconds = totalSeconds % 60;

    if (hours > 0) {
        return QString("%1:%2:%3.%4")
            .arg(hours, 2, 10, QChar('0'))
            .arg(minutes, 2, 10, QChar('0'))
            .arg(seconds, 2, 10, QChar('0'))
            .arg(millis, 3, 10, QChar('0'));
    }

    return QString("%1:%2.%3")
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'))
        .arg(millis, 3, 10, QChar('0'));
}
} // namespace

WorkWindow::WorkWindow(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_StyledBackground, true);
    buildUi();
    wireSignals();
}

void WorkWindow::setTagSession(TagSession* session) {
    tagSession_ = session;
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

    // Video player component (manages video widget and player logic)
    videoPlayer_ = new VideoPlayer(this);
    gameControls_ = new GameControls(this);
    
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
    
    // Video controls and timeline (from VideoPlayer)
    auto* videoControlsRow = videoPlayer_->controlsBar();
    auto* videoTimelineRow = videoPlayer_->timelineBar();
    
    // Video widget (+ tags) and GameControls side by side
    auto* videoGameRow = new QWidget(this);
    auto* videoGameLayout = new QHBoxLayout(videoGameRow);
    videoGameLayout->setContentsMargins(0, 0, 0, 0);
    videoGameLayout->setSpacing(12);

    auto* videoAndTagsCol = new QWidget(videoGameRow);
    auto* videoAndTagsLayout = new QVBoxLayout(videoAndTagsCol);
    videoAndTagsLayout->setContentsMargins(0, 0, 0, 0);
    videoAndTagsLayout->setSpacing(8);

    videoAndTagsLayout->addWidget(videoPlayer_->videoWidget(), /*stretch=*/1);

    tagsHeaderLabel_ = new QLabel("Tags", videoAndTagsCol);
    Style::setRole(tagsHeaderLabel_, "h3");

    tagsList_ = new QListWidget(videoAndTagsCol);
    tagsList_->setMinimumHeight(160);
    tagsList_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    videoAndTagsLayout->addWidget(tagsHeaderLabel_);
    videoAndTagsLayout->addWidget(tagsList_);

    videoGameLayout->addWidget(videoAndTagsCol, /*stretch=*/2);
    videoGameLayout->addWidget(gameControls_, /*stretch=*/1);
    
    // the rest of the layout, stacked vertically:
    layout->addWidget(headerRow);
    layout->addWidget(videoControlsRow);
    layout->addWidget(videoTimelineRow);
    layout->addWidget(videoGameRow, /*stretch=*/1);

    // initial visibility: hidden until video is loaded
    if (videoPlayer_) videoPlayer_->setControlsVisible(false);
    if (gameControls_) gameControls_->hide();
    if (tagsHeaderLabel_) tagsHeaderLabel_->hide();
    if (tagsList_) tagsList_->hide();
}

void WorkWindow::wireSignals() {
    // Video file management
    connect(replaceVideoAction_, &QAction::triggered, this, &WorkWindow::onReplaceVideo);
    connect(discardVideoAction_, &QAction::triggered, this, &WorkWindow::onDiscardVideo);
    
    // Connect VideoPlayer's videoClosed signal to WorkWindow's signal
    connect(videoPlayer_, &VideoPlayer::videoClosed, this, &WorkWindow::videoClosed);

    // GameControls -> capture timestamp and store tags
    connect(gameControls_, &GameControls::mainEventPressed, this, [this](const QString& mainEvent) {
        if (!videoPlayer_) return;
        pendingMainEvent_ = mainEvent;
        pendingTimestampMs_ = videoPlayer_->currentPositionMs();
        hasPendingTag_ = true;
    });

    connect(gameControls_, &GameControls::gameEventMarked, this, [this](const QString& mainEvent, const QString& followUpEvent) {
        if (!videoPlayer_ || !tagsList_) return;

        qint64 timestampMs = videoPlayer_->currentPositionMs();
        if (hasPendingTag_ && pendingMainEvent_ == mainEvent) {
            timestampMs = pendingTimestampMs_;
        }

        hasPendingTag_ = false;
        pendingMainEvent_.clear();
        pendingTimestampMs_ = 0;

        if (tagSession_) {
            tagSession_->addTag(TagSession::GameTag{mainEvent, followUpEvent, timestampMs});
        }

        QString eventText = mainEvent;
        if (!followUpEvent.isEmpty()) eventText += " → " + followUpEvent;
        const QString rowText = QString("%1  %2").arg(formatTimestampMs(timestampMs), eventText);

        auto* item = new QListWidgetItem(rowText, tagsList_);
        item->setData(Qt::UserRole, timestampMs);

        tagsList_->scrollToBottom();
    });

    connect(tagsList_, &QListWidget::itemActivated, this, &WorkWindow::onTagItemActivated);
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

    if (tagSession_) tagSession_->clear();
    hasPendingTag_ = false;
    pendingMainEvent_.clear();
    pendingTimestampMs_ = 0;
    if (tagsList_) tagsList_->clear();
    
    if (videoPlayer_) {
        videoPlayer_->loadVideoFromFile(filePath);
        videoPlayer_->setControlsVisible(true);
    }
    
    if (gameControls_) {
        gameControls_->show();
    }

    if (tagsHeaderLabel_) tagsHeaderLabel_->show();
    if (tagsList_) tagsList_->show();
}

void WorkWindow::onReplaceVideo() {
    const QString filePath = promptForVideoFile();
    if (filePath.isEmpty()) return;
    loadVideoFromFile(filePath);
}

void WorkWindow::onDiscardVideo() {
    if (videoPlayer_) {
        videoPlayer_->setControlsVisible(false);
    }
    if (gameControls_) {
        gameControls_->hide();
    }

    if (tagSession_) tagSession_->clear();
    hasPendingTag_ = false;
    pendingMainEvent_.clear();
    pendingTimestampMs_ = 0;
    if (tagsList_) tagsList_->clear();
    if (tagsHeaderLabel_) tagsHeaderLabel_->hide();
    if (tagsList_) tagsList_->hide();

    emit videoClosed();
}

void WorkWindow::onTagItemActivated(QListWidgetItem* item) {
    if (!item || !videoPlayer_) return;
    const qint64 posMs = item->data(Qt::UserRole).toLongLong();
    videoPlayer_->seekToMs(posMs);
}
