#include "WorkWindow.h"
#include "../style/StyleProps.h"
#include "../components/VideoPlayer.h"
#include "../components/GameControls.h"
#include "../state/TagSession.h"
#include "StatsWindow.h"

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
#include <QListWidgetItem>
#include <QAction>
#include <QKeySequence>
#include <QBrush>
#include <QColor>

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
    if (tagSession_ == session) return;
    if (tagSession_) disconnect(tagSession_, nullptr, this, nullptr);

    tagSession_ = session;
    if (statsWindow_) statsWindow_->setTagSession(tagSession_);

    rebuildFilterMenu();
    rebuildTagsList();

    if (!tagSession_) return;

    connect(tagSession_, &TagSession::cleared, this, [this]() {
        allowedMainEvents_.clear();
        rebuildFilterMenu();
        rebuildTagsList();
    });

    connect(tagSession_, &TagSession::statsChanged, this, [this]() {
        rebuildFilterMenu();
        rebuildTagsList();
    });

    connect(tagSession_, &TagSession::tagAdded, this, [this](const TagSession::GameTag&) {
        rebuildFilterMenu();
        rebuildTagsList();
    });
}

void WorkWindow::buildUi() {
    setObjectName("AppRoot");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(12);

    // header:
    // headerLabel_ = new QLabel("this is ava", this);
    // headerLabel_->setWordWrap(true);
    // Style::setRole(headerLabel_, "h1");

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
    statsWindow_ = new StatsWindow(this);
    statsWindow_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    statsWindow_->setMinimumHeight(180);
    
    // Video controls and timeline (from VideoPlayer)
    auto* videoControlsBar = videoPlayer_->controlsBar();
    auto* videoTimelineRow = videoPlayer_->timelineBar();
    
    // Wrap video controls bar with Video Manager button on the right
    auto* videoControlsRow = new QWidget(this);
    auto* videoControlsLayout = new QHBoxLayout(videoControlsRow);
    videoControlsLayout->setContentsMargins(0, 0, 0, 0);
    videoControlsLayout->setSpacing(12);
    videoControlsLayout->addWidget(videoControlsBar, /*stretch=*/1);
    videoControlsLayout->addWidget(videoMenuButton_, /*stretch=*/0, Qt::AlignRight | Qt::AlignVCenter);
    
    // Video widget (+ tags) and GameControls (+ stats) side by side
    auto* videoGameRow = new QWidget(this);
    auto* videoGameLayout = new QHBoxLayout(videoGameRow);
    videoGameLayout->setContentsMargins(0, 0, 0, 0);
    videoGameLayout->setSpacing(12);

    auto* videoAndTagsCol = new QWidget(videoGameRow);
    auto* videoAndTagsLayout = new QVBoxLayout(videoAndTagsCol);
    videoAndTagsLayout->setContentsMargins(0, 0, 0, 0);
    videoAndTagsLayout->setSpacing(8);

    videoAndTagsLayout->addWidget(videoPlayer_->videoWidget(), /*stretch=*/1);

    auto* tagsHeaderRow = new QWidget(videoAndTagsCol);
    auto* tagsHeaderLayout = new QHBoxLayout(tagsHeaderRow);
    tagsHeaderLayout->setContentsMargins(0, 0, 0, 0);
    tagsHeaderLayout->setSpacing(8);

    tagsHeaderLabel_ = new QLabel("Tags", tagsHeaderRow);
    Style::setRole(tagsHeaderLabel_, "h3");

    tagsFilterButton_ = new QToolButton(tagsHeaderRow);
    tagsFilterButton_->setText("Filter");
    Style::setVariant(tagsFilterButton_, "ghost");
    Style::setSize(tagsFilterButton_, "sm");
    tagsFilterButton_->setPopupMode(QToolButton::InstantPopup);
    tagsFilterButton_->setCursor(Qt::PointingHandCursor);

    tagsRemoveFiltersButton_ = new QToolButton(tagsHeaderRow);
    tagsRemoveFiltersButton_->setText("Remove filters");
    Style::setVariant(tagsRemoveFiltersButton_, "ghost");
    Style::setSize(tagsRemoveFiltersButton_, "sm");
    tagsRemoveFiltersButton_->setCursor(Qt::PointingHandCursor);
    tagsRemoveFiltersButton_->hide();

    tagsFilterMenu_ = new QMenu(tagsFilterButton_);
    tagsFilterButton_->setMenu(tagsFilterMenu_);

    tagsFilterIndicator_ = new QLabel(tagsHeaderRow);
    tagsFilterIndicator_->setWordWrap(false);
    Style::setRole(tagsFilterIndicator_, "muted");
    tagsFilterIndicator_->hide();

    tagsHeaderLayout->addWidget(tagsHeaderLabel_, /*stretch=*/0);
    tagsHeaderLayout->addWidget(tagsFilterIndicator_, /*stretch=*/1);
    tagsHeaderLayout->addWidget(tagsRemoveFiltersButton_, /*stretch=*/0, Qt::AlignRight);
    tagsHeaderLayout->addWidget(tagsFilterButton_, /*stretch=*/0, Qt::AlignRight);

    tagsList_ = new QListWidget(videoAndTagsCol);
    tagsList_->setMinimumHeight(160);
    tagsList_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    videoAndTagsLayout->addWidget(tagsHeaderRow);
    videoAndTagsLayout->addWidget(tagsList_);

    videoGameLayout->addWidget(videoAndTagsCol, /*stretch=*/2);

    auto* rightCol = new QWidget(videoGameRow);
    auto* rightLayout = new QVBoxLayout(rightCol);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(12);
    rightLayout->addWidget(gameControls_, /*stretch=*/0);
    rightLayout->addWidget(statsWindow_, /*stretch=*/1);

    videoGameLayout->addWidget(rightCol, /*stretch=*/1);
    
    // the rest of the layout, stacked vertically:
    layout->addWidget(videoControlsRow);
    layout->addWidget(videoTimelineRow);
    layout->addWidget(videoGameRow, /*stretch=*/1);

    // initial visibility: hidden until video is loaded
    if (videoPlayer_) videoPlayer_->setControlsVisible(false);
    if (gameControls_) gameControls_->hide();
    if (statsWindow_) statsWindow_->hide();
    if (tagsHeaderLabel_) tagsHeaderLabel_->hide();
    if (tagsFilterButton_) tagsFilterButton_->hide();
    if (tagsRemoveFiltersButton_) tagsRemoveFiltersButton_->hide();
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
        if (!videoPlayer_) return;

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
    });

    connect(tagsList_, &QListWidget::itemActivated, this, &WorkWindow::onTagItemActivated);

    connect(statsWindow_, &StatsWindow::filterByPathRequested, this, &WorkWindow::onFilterByPathRequested);
    connect(tagsRemoveFiltersButton_, &QToolButton::clicked, this, &WorkWindow::onRemoveFilters);

    // Highlight tags when playhead is within ±2s
    connect(videoPlayer_, &VideoPlayer::positionChangedMs, this, &WorkWindow::onPlayheadPositionChanged);
    
    // Backspace to delete selected tag
    auto* deleteTagAction = new QAction(this);
    deleteTagAction->setShortcut(QKeySequence(Qt::Key_Backspace));
    deleteTagAction->setShortcutContext(Qt::WidgetShortcut);
    connect(deleteTagAction, &QAction::triggered, this, &WorkWindow::onDeleteSelectedTag);
    tagsList_->addAction(deleteTagAction);
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
    if (tagsFilterButton_) tagsFilterButton_->show();
    updateFilterButtonsVisibility();
    if (tagsList_) tagsList_->show();
    updateFilterIndicator();
    if (statsWindow_) {
        statsWindow_->setTagSession(tagSession_);
        statsWindow_->show();
    }

    rebuildFilterMenu();
    rebuildTagsList();
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
    if (tagsFilterButton_) tagsFilterButton_->hide();
    if (tagsRemoveFiltersButton_) tagsRemoveFiltersButton_->hide();
    if (tagsList_) tagsList_->hide();
    if (statsWindow_) statsWindow_->hide();

    emit videoClosed();
}

void WorkWindow::onTagItemActivated(QListWidgetItem* item) {
    if (!item || !videoPlayer_) return;
    const qint64 posMs = item->data(Qt::UserRole).toLongLong();
    videoPlayer_->seekToMs(posMs);
}

namespace {
constexpr qint64 kPlayheadNearToleranceMs = 2000;
const QColor kTagNearPlayheadColor(147, 197, 253); // light blue, lighter than selection
} // namespace

void WorkWindow::onPlayheadPositionChanged(qint64 positionMs) {
    updateTagPlayheadHighlight(positionMs);
}

void WorkWindow::updateTagPlayheadHighlight(qint64 positionMs) {
    if (!tagsList_) return;
    for (int row = 0; row < tagsList_->count(); ++row) {
        auto* item = tagsList_->item(row);
        if (!item) continue;
        const qint64 tagMs = item->data(Qt::UserRole).toLongLong();
        const qint64 diff = (tagMs > positionMs) ? (tagMs - positionMs) : (positionMs - tagMs);
        if (diff <= kPlayheadNearToleranceMs) {
            item->setBackground(QBrush(kTagNearPlayheadColor));
        } else {
            item->setBackground(QBrush());
        }
    }
}

void WorkWindow::onDeleteSelectedTag() {
    if (!tagsList_ || !tagSession_) return;
    
    auto* item = tagsList_->currentItem();
    if (!item) return;
    
    // Get the stored TagSession index directly from the item
    const QVariant tagIndexVar = item->data(Qt::UserRole + 3);
    if (!tagIndexVar.isValid()) return;
    
    const int tagIndex = tagIndexVar.toInt();
    if (tagIndex < 0 || tagIndex >= tagSession_->tags().size()) return;
    
    tagSession_->removeTag(tagIndex);
    rebuildTagsList();
}

void WorkWindow::onSelectAllFilters() {
    for (auto it = filterActionByMainEvent_.begin(); it != filterActionByMainEvent_.end(); ++it) {
        it.value()->setChecked(true);
    }
    rebuildTagsList();
    updateFilterIndicator();
    updateFilterButtonsVisibility();
}

void WorkWindow::onSelectNoFilters() {
    for (auto it = filterActionByMainEvent_.begin(); it != filterActionByMainEvent_.end(); ++it) {
        it.value()->setChecked(false);
    }
    rebuildTagsList();
}

void WorkWindow::onFilterActionToggled(bool /*checked*/) {
    rebuildTagsList();
    updateFilterIndicator();
    updateFilterButtonsVisibility();
}

void WorkWindow::onFilterByPathRequested(const QString& mainEvent, const QString& followUpEvent) {
    activeFilterPathMainEvent_ = mainEvent;
    activeFilterPathFollowUp_ = followUpEvent;
    rebuildTagsList();
    updateFilterIndicator();
    updateFilterButtonsVisibility();
}

void WorkWindow::onRemoveFilters() {
    activeFilterPathMainEvent_.clear();
    activeFilterPathFollowUp_.clear();
    onSelectAllFilters();
    updateFilterButtonsVisibility();
}

bool WorkWindow::isMainEventAllowed(const QString& mainEvent) const {
    auto it = filterActionByMainEvent_.find(mainEvent);
    if (it == filterActionByMainEvent_.end()) return true; // no filter entry yet -> allow
    return it.value()->isChecked();
}

bool WorkWindow::isTagAllowed(const QString& mainEvent, const QString& followUpEvent) const {
    if (!activeFilterPathMainEvent_.isEmpty()) {
        if (mainEvent != activeFilterPathMainEvent_) return false;
        if (activeFilterPathFollowUp_.isEmpty()) return true;
        return followUpEvent == activeFilterPathFollowUp_
            || followUpEvent.startsWith(activeFilterPathFollowUp_ + " → ");
    }
    return isMainEventAllowed(mainEvent);
}

bool WorkWindow::hasAnyFilterActive() const {
    if (!activeFilterPathMainEvent_.isEmpty()) return true;
    for (auto it = filterActionByMainEvent_.cbegin(); it != filterActionByMainEvent_.cend(); ++it) {
        if (!it.value()->isChecked()) return true;
    }
    return false;
}

void WorkWindow::updateFilterButtonsVisibility() {
    if (!tagsRemoveFiltersButton_) return;
    if (hasAnyFilterActive()) {
        tagsRemoveFiltersButton_->show();
    } else {
        tagsRemoveFiltersButton_->hide();
    }
}

void WorkWindow::rebuildFilterMenu() {
    if (!tagsFilterMenu_) return;

    // Preserve checked state
    QHash<QString, bool> prevChecked;
    for (auto it = filterActionByMainEvent_.cbegin(); it != filterActionByMainEvent_.cend(); ++it) {
        prevChecked.insert(it.key(), it.value()->isChecked());
    }

    tagsFilterMenu_->clear();
    filterActionByMainEvent_.clear();

    auto* selectAll = tagsFilterMenu_->addAction("Select all");
    auto* selectNone = tagsFilterMenu_->addAction("Select none");
    connect(selectAll, &QAction::triggered, this, &WorkWindow::onSelectAllFilters);
    connect(selectNone, &QAction::triggered, this, &WorkWindow::onSelectNoFilters);
    tagsFilterMenu_->addSeparator();

    if (!tagSession_) return;
    QStringList mains = tagSession_->mainEventCounts().keys();
    mains.sort(Qt::CaseInsensitive);

    for (const QString& mainEvent : mains) {
        auto* act = tagsFilterMenu_->addAction(mainEvent);
        act->setCheckable(true);
        act->setChecked(prevChecked.contains(mainEvent) ? prevChecked.value(mainEvent) : true);
        connect(act, &QAction::toggled, this, &WorkWindow::onFilterActionToggled);
        filterActionByMainEvent_.insert(mainEvent, act);
    }
}

void WorkWindow::updateFilterIndicator() {
    if (!tagsFilterIndicator_) return;

    if (!activeFilterPathMainEvent_.isEmpty()) {
        QString pathText = activeFilterPathMainEvent_;
        if (!activeFilterPathFollowUp_.isEmpty()) pathText += " → " + activeFilterPathFollowUp_;
        tagsFilterIndicator_->setText("Filtered by: " + pathText);
        tagsFilterIndicator_->show();
        return;
    }

    QStringList activeFilters;
    for (auto it = filterActionByMainEvent_.cbegin(); it != filterActionByMainEvent_.cend(); ++it) {
        if (it.value()->isChecked()) {
            activeFilters.append(it.key());
        }
    }

    if (activeFilters.isEmpty() || activeFilters.size() == filterActionByMainEvent_.size()) {
        tagsFilterIndicator_->hide();
        return;
    }

    activeFilters.sort(Qt::CaseInsensitive);
    const QString text = "Filtered by: " + activeFilters.join(", ");
    tagsFilterIndicator_->setText(text);
    tagsFilterIndicator_->show();
}

void WorkWindow::rebuildTagsList() {
    if (!tagsList_) return;
    tagsList_->clear();
    if (!tagSession_) return;

    // Collect (tag, tagSessionIndex) for tags that pass the filter
    struct TagEntry {
        TagSession::GameTag tag;
        int tagSessionIndex;
    };
    QVector<TagEntry> entries;
    int tagSessionIndex = 0;
    for (const auto& tag : tagSession_->tags()) {
        if (isTagAllowed(tag.mainEvent, tag.followUpEvent)) {
            entries.append({tag, tagSessionIndex});
        }
        tagSessionIndex++;
    }

    // Sort by timestamp so the list is always chronological
    std::sort(entries.begin(), entries.end(), [](const TagEntry& a, const TagEntry& b) {
        return a.tag.positionMs < b.tag.positionMs;
    });

    for (const auto& e : entries) {
        const auto& tag = e.tag;
        QString eventText = tag.mainEvent;
        if (!tag.followUpEvent.isEmpty()) eventText += " → " + tag.followUpEvent;
        const QString rowText = QString("%1  %2").arg(formatTimestampMs(tag.positionMs), eventText);

        auto* item = new QListWidgetItem(rowText);
        item->setData(Qt::UserRole, tag.positionMs);
        item->setData(Qt::UserRole + 1, tag.mainEvent);
        item->setData(Qt::UserRole + 2, tag.followUpEvent);
        item->setData(Qt::UserRole + 3, e.tagSessionIndex); // Store the actual TagSession index for delete
        tagsList_->addItem(item);
    }

    tagsList_->scrollToBottom();
    updateFilterIndicator();
    updateFilterButtonsVisibility();
    if (videoPlayer_) {
        updateTagPlayheadHighlight(videoPlayer_->currentPositionMs());
    }
}
