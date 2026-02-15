#include "WorkWindow.h"
#include "../style/StyleProps.h"
#include "../components/VideoPlayer.h"
#include "../components/GameControls.h"
#include "../state/TagSession.h"
#include "StatsWindow.h"
#include "GameSetupWindow.h"

#include "VideoControlsBar.h"
#include "TimelineBar.h"

#include <QLabel>
#include <QStackedWidget>
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
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QIcon>
#include <QFrame>
#include <QBoxLayout>
#include <QTimer>
#include <QDialog>
#include <QVBoxLayout>

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
    applyTaggingLayout();
}

void WorkWindow::setTagSession(TagSession* session) {
    if (tagSession_ == session) return;
    if (tagSession_) disconnect(tagSession_, nullptr, this, nullptr);

    tagSession_ = session;
    if (statsWindow_) statsWindow_->setTagSession(tagSession_);
    if (statsOverlay_) statsOverlay_->setTagSession(tagSession_);

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
        flashNewTagRow();
    });
    connect(tagSession_, &TagSession::tagNoteChanged, this, [this](int) { loadNoteForSelectedTag(); });
}

void WorkWindow::setMode(Mode m) {
    if (mode_ == m) return;
    mode_ = m;
    if (m == Mode::Tagging)
        applyTaggingLayout();
    else
        applyAnalyzingLayout();
    if (modeTaggingBtn_) modeTaggingBtn_->setChecked(m == Mode::Tagging);
    if (modeAnalyzingBtn_) modeAnalyzingBtn_->setChecked(m == Mode::Analyzing);
}

TagSession::GameTag WorkWindow::currentTagContext() const {
    TagSession::GameTag ctx;
    ctx.period = contextPeriod_;
    ctx.team = contextTeam_;
    ctx.situation = contextSituation_;
    return ctx;
}

void WorkWindow::buildUi() {
    setObjectName("AppRoot");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    contentStack_ = new QStackedWidget(this);
    mainContentContainer_ = new QWidget(this);
    auto* mainContentLayout = new QVBoxLayout(mainContentContainer_);
    mainContentLayout->setContentsMargins(16, 16, 16, 16);
    mainContentLayout->setSpacing(8);

    // Top row: mode toggle | video controls | settings icon
    auto* topRow = new QWidget(mainContentContainer_);
    auto* topLayout = new QHBoxLayout(topRow);
    topLayout->setContentsMargins(0, 0, 0, 0);
    topLayout->setSpacing(12);

    modeTaggingBtn_ = new QToolButton(topRow);
    modeTaggingBtn_->setText("Tagging");
    modeTaggingBtn_->setCheckable(true);
    modeTaggingBtn_->setChecked(true);
    Style::setVariant(modeTaggingBtn_, "ghost");
    Style::setSize(modeTaggingBtn_, "sm");
    modeTaggingBtn_->setCursor(Qt::PointingHandCursor);
    modeTaggingBtn_->setToolTip("Eyes on video, hands on keyboard (M)");

    modeAnalyzingBtn_ = new QToolButton(topRow);
    modeAnalyzingBtn_->setText("Analyzing");
    modeAnalyzingBtn_->setCheckable(true);
    modeAnalyzingBtn_->setChecked(false);
    Style::setVariant(modeAnalyzingBtn_, "ghost");
    Style::setSize(modeAnalyzingBtn_, "sm");
    modeAnalyzingBtn_->setCursor(Qt::PointingHandCursor);
    modeAnalyzingBtn_->setToolTip("Stats and notes (M)");

    topLayout->addWidget(modeTaggingBtn_);
    topLayout->addWidget(modeAnalyzingBtn_);
    topLayout->addSpacing(16);

    videoPlayer_ = new VideoPlayer(this);
    auto* videoControlsBar = videoPlayer_->controlsBar();
    videoControlsRow_ = new QWidget(this);
    auto* videoControlsLayout = new QHBoxLayout(videoControlsRow_);
    videoControlsLayout->setContentsMargins(0, 0, 0, 0);
    videoControlsLayout->setSpacing(8);
    videoControlsLayout->addWidget(videoControlsBar, 1);
    videoMenuButton_ = new QToolButton(this);
    QIcon settingsIcon = QIcon::fromTheme("preferences-system");
    if (!settingsIcon.isNull()) {
        videoMenuButton_->setIcon(settingsIcon);
        videoMenuButton_->setText(QString());
    } else {
        videoMenuButton_->setText(QString::fromUtf8("\u2699")); // gear
    }
    videoMenuButton_->setToolTip("Video Manager");
    videoMenuButton_->setMinimumWidth(36);
    Style::setVariant(videoMenuButton_, "ghost");
    Style::setSize(videoMenuButton_, "sm");
    videoMenuButton_->setPopupMode(QToolButton::InstantPopup);
    videoMenuButton_->setCursor(Qt::PointingHandCursor);
    videoMenu_ = new QMenu(videoMenuButton_);
    replaceVideoAction_ = videoMenu_->addAction("Replace video with another one");
    discardVideoAction_ = videoMenu_->addAction("Close current video");
    videoMenuButton_->setMenu(videoMenu_);
    videoControlsLayout->addWidget(videoMenuButton_, 0, Qt::AlignRight | Qt::AlignVCenter);

    topLayout->addWidget(videoControlsRow_, 1);

    mainContentLayout->addWidget(topRow);

    videoTimelineRow_ = new QWidget(mainContentContainer_);
    auto* timelineLayout = new QHBoxLayout(videoTimelineRow_);
    timelineLayout->setContentsMargins(0, 0, 0, 0);
    timelineLayout->addWidget(videoPlayer_->timelineBar());
    mainContentLayout->addWidget(videoTimelineRow_);

    contentArea_ = new QWidget(mainContentContainer_);
    contentLayout_ = new QVBoxLayout(contentArea_);
    contentLayout_->setContentsMargins(0, 0, 0, 0);
    contentLayout_->setSpacing(8);
    mainContentLayout->addWidget(contentArea_, 1);

    gameSetupWidget_ = new GameSetupWindow(this);
    contentStack_->addWidget(gameSetupWidget_);
    contentStack_->addWidget(mainContentContainer_);
    contentStack_->setCurrentIndex(0);

    layout->addWidget(contentStack_);

    // Tagging layout wrappers
    taggingMainRow_ = new QWidget(this);
    auto* taggingMainLayout = new QHBoxLayout(taggingMainRow_);
    taggingMainLayout->setContentsMargins(0, 0, 0, 0);
    taggingMainLayout->setSpacing(12);
    taggingVideoCol_ = new QWidget(this);
    taggingVideoCol_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* taggingVideoLayout = new QVBoxLayout(taggingVideoCol_);
    taggingVideoLayout->setContentsMargins(0, 0, 0, 0);
    taggingMainLayout->addWidget(taggingVideoCol_, 3);
    taggingRightCol_ = new QWidget(this);
    taggingRightCol_->setObjectName("TaggingRightCol");
    auto* taggingRightLayout = new QVBoxLayout(taggingRightCol_);
    taggingRightLayout->setContentsMargins(0, 0, 0, 0);
    taggingMainLayout->addWidget(taggingRightCol_, 0);

    // Tags section (full width in tagging; inside left col in analyzing)
    tagsSection_ = new QWidget(this);
    auto* tagsSectionLayout = new QVBoxLayout(tagsSection_);
    tagsSectionLayout->setContentsMargins(0, 0, 0, 0);
    tagsSectionLayout->setSpacing(6);

    auto* tagsHeaderRow = new QWidget(tagsSection_);
    auto* tagsHeaderLayout = new QHBoxLayout(tagsHeaderRow);
    tagsHeaderLayout->setContentsMargins(0, 0, 0, 0);
    tagsHeaderLayout->setSpacing(8);

    tagsHeaderLabel_ = new QLabel("Tags", tagsHeaderRow);
    Style::setRole(tagsHeaderLabel_, "h3");

    periodQ1_ = new QToolButton(tagsHeaderRow);
    periodQ2_ = new QToolButton(tagsHeaderRow);
    periodQ3_ = new QToolButton(tagsHeaderRow);
    periodQ4_ = new QToolButton(tagsHeaderRow);
    for (auto* b : {periodQ1_, periodQ2_, periodQ3_, periodQ4_}) {
        b->setCheckable(true);
        Style::setVariant(b, "ghost");
        Style::setSize(b, "sm");
        b->setCursor(Qt::PointingHandCursor);
    }
    periodQ1_->setText("Q1");
    periodQ2_->setText("Q2");
    periodQ3_->setText("Q3");
    periodQ4_->setText("Q4");

    teamHome_ = new QToolButton(tagsHeaderRow);
    teamAway_ = new QToolButton(tagsHeaderRow);
    for (auto* b : {teamHome_, teamAway_}) {
        b->setCheckable(true);
        Style::setVariant(b, "ghost");
        Style::setSize(b, "sm");
        b->setCursor(Qt::PointingHandCursor);
    }
    teamHome_->setText("Home");
    teamAway_->setText("Away");

    situationAttacking_ = new QToolButton(tagsHeaderRow);
    situationDefending_ = new QToolButton(tagsHeaderRow);
    for (auto* b : {situationAttacking_, situationDefending_}) {
        b->setCheckable(true);
        Style::setVariant(b, "ghost");
        Style::setSize(b, "sm");
        b->setCursor(Qt::PointingHandCursor);
    }
    situationAttacking_->setText("Attacking");
    situationDefending_->setText("Defending");

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

    undoLastTagButton_ = new QToolButton(tagsHeaderRow);
    undoLastTagButton_->setText("Undo");
    undoLastTagButton_->setToolTip("Ctrl+Z  Remove most recent tag");
    Style::setVariant(undoLastTagButton_, "ghost");
    Style::setSize(undoLastTagButton_, "sm");
    undoLastTagButton_->setCursor(Qt::PointingHandCursor);

    tagsHeaderLayout->addWidget(tagsHeaderLabel_, 0);
    tagsHeaderLayout->addWidget(periodQ1_, 0);
    tagsHeaderLayout->addWidget(periodQ2_, 0);
    tagsHeaderLayout->addWidget(periodQ3_, 0);
    tagsHeaderLayout->addWidget(periodQ4_, 0);
    tagsHeaderLayout->addSpacing(4);
    tagsHeaderLayout->addWidget(teamHome_, 0);
    tagsHeaderLayout->addWidget(teamAway_, 0);
    tagsHeaderLayout->addSpacing(4);
    tagsHeaderLayout->addWidget(situationAttacking_, 0);
    tagsHeaderLayout->addWidget(situationDefending_, 0);
    tagsHeaderLayout->addStretch(1);
    tagsHeaderLayout->addWidget(tagsFilterIndicator_, 0);
    tagsHeaderLayout->addWidget(undoLastTagButton_, 0);
    tagsHeaderLayout->addWidget(tagsRemoveFiltersButton_, 0);
    tagsHeaderLayout->addWidget(tagsFilterButton_, 0);

    tagsList_ = new QListWidget(tagsSection_);
    tagsList_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    const int rowHeight = tagsList_->sizeHintForRow(0) > 0 ? tagsList_->sizeHintForRow(0) : 24;
    tagsList_->setMaximumHeight(rowHeight * 3 + 8);
    tagsList_->setMinimumHeight(rowHeight * 2);

    tagsSectionLayout->addWidget(tagsHeaderRow);
    tagsSectionLayout->addWidget(tagsList_);

    gameControls_ = new GameControls(this);
    statsWindow_ = new StatsWindow(this);
    statsWindow_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    statsWindow_->setMinimumHeight(180);

    notesEdit_ = new QPlainTextEdit(this);
    notesEdit_->setPlaceholderText("Note for selected tag…");
    notesEdit_->setMaximumHeight(120);
    Style::setRole(notesEdit_, "muted");

    analyzingMainRow_ = new QWidget(this);
    auto* analyzingMainLayout = new QHBoxLayout(analyzingMainRow_);
    analyzingMainLayout->setContentsMargins(0, 0, 0, 0);
    analyzingMainLayout->setSpacing(12);
    analyzingLeftCol_ = new QWidget(this);
    auto* analyzingLeftLayout = new QVBoxLayout(analyzingLeftCol_);
    analyzingLeftLayout->setContentsMargins(0, 0, 0, 0);
    analyzingLeftLayout->setSpacing(8);
    analyzingRightCol_ = new QWidget(this);
    auto* analyzingRightLayout = new QVBoxLayout(analyzingRightCol_);
    analyzingRightLayout->setContentsMargins(0, 0, 0, 0);
    analyzingRightLayout->setSpacing(8);
    analyzingMainLayout->addWidget(analyzingLeftCol_, 1);
    analyzingMainLayout->addWidget(analyzingRightCol_, 1);

    if (videoPlayer_) videoPlayer_->setControlsVisible(false);
    if (gameControls_) gameControls_->hide();
    if (statsWindow_) statsWindow_->hide();
    if (tagsHeaderLabel_) tagsHeaderLabel_->hide();
    if (tagsFilterButton_) tagsFilterButton_->hide();
    if (tagsRemoveFiltersButton_) tagsRemoveFiltersButton_->hide();
    if (undoLastTagButton_) undoLastTagButton_->hide();
    if (tagsList_) tagsList_->hide();
    if (modeTaggingBtn_) modeTaggingBtn_->hide();
    if (modeAnalyzingBtn_) modeAnalyzingBtn_->hide();
}

void WorkWindow::applyTaggingLayout() {
    mode_ = Mode::Tagging;
    if (videoPlayer_ && videoPlayer_->controlsBar())
        videoPlayer_->controlsBar()->setObjectName("VideoControlsBarSlim");

    QWidget* vw = videoPlayer_->videoWidget();
    if (QWidget* p = vw->parentWidget())
        if (QLayout* L = p->layout()) L->removeWidget(vw);
    static_cast<QBoxLayout*>(taggingVideoCol_->layout())->addWidget(vw, 1);

    if (QWidget* p = gameControls_->parentWidget())
        if (QLayout* L = p->layout()) L->removeWidget(gameControls_);
    static_cast<QBoxLayout*>(taggingRightCol_->layout())->addWidget(gameControls_, 0);

    while (QLayoutItem* item = contentLayout_->takeAt(0)) {
        delete item;  // widget stays in tree; do not setParent(nullptr)
    }
    contentLayout_->addWidget(taggingMainRow_, 1);
    if (QWidget* p = tagsSection_->parentWidget())
        if (QLayout* L = p->layout()) L->removeWidget(tagsSection_);
    contentLayout_->addWidget(tagsSection_, 0);

    const int rh = tagsList_->sizeHintForRow(0) > 0 ? tagsList_->sizeHintForRow(0) : 24;
    tagsList_->setMaximumHeight(rh * 3 + 8);

    statsWindow_->hide();
    if (notesEdit_) notesEdit_->hide();

    // Keep tag list in sync with session after layout change
    rebuildTagsList();
}

void WorkWindow::applyAnalyzingLayout() {
    mode_ = Mode::Analyzing;
    if (videoPlayer_ && videoPlayer_->controlsBar())
        videoPlayer_->controlsBar()->setObjectName(""); // normal height

    while (QLayoutItem* item = contentLayout_->takeAt(0)) {
        delete item;  // widget stays in tree; do not setParent(nullptr)
    }

    QWidget* vw = videoPlayer_->videoWidget();
    if (QWidget* p = vw->parentWidget())
        if (QLayout* L = p->layout()) L->removeWidget(vw);
    static_cast<QBoxLayout*>(analyzingLeftCol_->layout())->addWidget(vw, 1);

    if (QWidget* p = tagsSection_->parentWidget())
        if (QLayout* L = p->layout()) L->removeWidget(tagsSection_);
    static_cast<QBoxLayout*>(analyzingLeftCol_->layout())->addWidget(tagsSection_, 1);

    if (QWidget* p = gameControls_->parentWidget())
        if (QLayout* L = p->layout()) L->removeWidget(gameControls_);
    static_cast<QBoxLayout*>(analyzingLeftCol_->layout())->addWidget(gameControls_, 0);

    if (QWidget* p = statsWindow_->parentWidget())
        if (QLayout* L = p->layout()) L->removeWidget(statsWindow_);
    static_cast<QBoxLayout*>(analyzingRightCol_->layout())->addWidget(statsWindow_, 1);

    if (notesEdit_) {
        if (QWidget* p = notesEdit_->parentWidget())
            if (QLayout* L = p->layout()) L->removeWidget(notesEdit_);
        static_cast<QBoxLayout*>(analyzingRightCol_->layout())->addWidget(notesEdit_, 0);
    }

    contentLayout_->addWidget(analyzingMainRow_, 1);

    statsWindow_->show();
    if (notesEdit_) notesEdit_->show();
    tagsList_->setMaximumHeight(QWIDGETSIZE_MAX);

    // Keep tag list in sync with session after layout change
    rebuildTagsList();
}

void WorkWindow::wireSignals() {
    if (gameSetupWidget_) {
        connect(gameSetupWidget_, &GameSetupWindow::teamSetupConfirmed, this, &WorkWindow::onTeamSetupConfirmed);
        connect(gameSetupWidget_, &GameSetupWindow::cancelled, this, &WorkWindow::onTeamSetupCancelled);
    }
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
            TagSession::GameTag tag;
            tag.mainEvent = mainEvent;
            tag.followUpEvent = followUpEvent;
            tag.positionMs = timestampMs;
            TagSession::GameTag ctx = currentTagContext();
            tag.period = ctx.period;
            tag.team = ctx.team;
            tag.situation = ctx.situation;
            tagSession_->addTag(tag);
        }
    });

    connect(gameControls_, &GameControls::possessionChanged, this, [this](const QString& team) {
        contextTeam_ = team;
        quickFilterTeam_ = team;
        if (teamHome_) teamHome_->setChecked(team == "Home");
        if (teamAway_) teamAway_->setChecked(team == "Away");
        rebuildTagsList();
    });

    connect(tagsList_, &QListWidget::itemActivated, this, &WorkWindow::onTagItemActivated);
    connect(tagsList_, &QListWidget::currentItemChanged, this, &WorkWindow::onTagSelectionChanged);

    connect(modeTaggingBtn_, &QToolButton::clicked, this, &WorkWindow::onModeToggled);
    connect(modeAnalyzingBtn_, &QToolButton::clicked, this, &WorkWindow::onModeToggled);

    if (notesEdit_)
        connect(notesEdit_, &QPlainTextEdit::textChanged, this, &WorkWindow::onNoteTextChanged);

    connect(periodQ1_, &QToolButton::clicked, this, &WorkWindow::onQuickFilterPeriodClicked);
    connect(periodQ2_, &QToolButton::clicked, this, &WorkWindow::onQuickFilterPeriodClicked);
    connect(periodQ3_, &QToolButton::clicked, this, &WorkWindow::onQuickFilterPeriodClicked);
    connect(periodQ4_, &QToolButton::clicked, this, &WorkWindow::onQuickFilterPeriodClicked);
    connect(teamHome_, &QToolButton::clicked, this, &WorkWindow::onQuickFilterTeamClicked);
    connect(teamAway_, &QToolButton::clicked, this, &WorkWindow::onQuickFilterTeamClicked);
    connect(situationAttacking_, &QToolButton::clicked, this, &WorkWindow::onQuickFilterSituationClicked);
    connect(situationDefending_, &QToolButton::clicked, this, &WorkWindow::onQuickFilterSituationClicked);

    connect(statsWindow_, &StatsWindow::filterByPathRequested, this, &WorkWindow::onFilterByPathRequested);
    connect(tagsRemoveFiltersButton_, &QToolButton::clicked, this, &WorkWindow::onRemoveFilters);
    connect(undoLastTagButton_, &QToolButton::clicked, this, &WorkWindow::onUndoLastTag);

    auto* modeAction = new QAction(this);
    modeAction->setShortcut(QKeySequence(Qt::Key_M));
    modeAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(modeAction, &QAction::triggered, this, [this]() {
        setMode(mode_ == Mode::Tagging ? Mode::Analyzing : Mode::Tagging);
    });
    addAction(modeAction);

    auto* statsOverlayAction = new QAction(this);
    statsOverlayAction->setShortcut(QKeySequence(Qt::Key_Comma));
    statsOverlayAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    statsOverlayAction->setToolTip("Stats overlay (,)");
    connect(statsOverlayAction, &QAction::triggered, this, &WorkWindow::showStatsOverlay);
    addAction(statsOverlayAction);

    noteDebounceTimer_ = new QTimer(this);
    noteDebounceTimer_->setSingleShot(true);
    connect(noteDebounceTimer_, &QTimer::timeout, this, &WorkWindow::saveNoteDebounceFired);

    // Highlight tags when playhead is within ±2s
    connect(videoPlayer_, &VideoPlayer::positionChangedMs, this, &WorkWindow::onPlayheadPositionChanged);
    
    // Backspace to delete selected tag
    auto* deleteTagAction = new QAction(this);
    deleteTagAction->setShortcut(QKeySequence(Qt::Key_Backspace));
    deleteTagAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(deleteTagAction, &QAction::triggered, this, &WorkWindow::onDeleteSelectedTag);
    addAction(deleteTagAction);

    // Ctrl+Z to undo last tag (remove most recent)
    auto* undoTagAction = new QAction(this);
    undoTagAction->setShortcut(QKeySequence::Undo);
    undoTagAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(undoTagAction, &QAction::triggered, this, &WorkWindow::onUndoLastTag);
    addAction(undoTagAction);
}


QString WorkWindow::promptForVideoFile() {
    return QFileDialog::getOpenFileName(
        this,
        "Select a video file",
        QString(),
        "Video files (*.mp4 *.mov *.m4v *.mkv *.avi);;All files (*.*)"
    );
}

void WorkWindow::showTeamSetupForVideo(const QString& filePath) {
    if (!gameSetupWidget_ || !contentStack_) return;
    gameSetupWidget_->setVideoPath(filePath);
    gameSetupWidget_->setTeamDefaults(QString(), QString(), QString(), QString());
    contentStack_->setCurrentIndex(0);
    gameSetupWidget_->setInitialFocus();
}

void WorkWindow::onTeamSetupConfirmed(const QString& filePath,
                                       const QString& homeName, const QString& awayName,
                                       const QString& homeColor, const QString& awayColor) {
    if (tagSession_) tagSession_->setGameTeams(homeName, awayName, homeColor, awayColor);
    if (contentStack_) contentStack_->setCurrentIndex(1);
    loadVideoFromFile(filePath);
}

void WorkWindow::onTeamSetupCancelled() {
    emit videoClosed();
}

void WorkWindow::applyTeamButtonColor(QToolButton* button, const QString& hexColor) {
    if (!button) return;
    if (hexColor.trimmed().isEmpty() || !QColor(hexColor).isValid()) {
        button->setStyleSheet(QString());
        return;
    }
    button->setStyleSheet(QString("QToolButton { border-left: 3px solid %1; }").arg(hexColor.trimmed()));
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
        if (tagSession_) {
            gameControls_->setTeamDisplayNames(tagSession_->homeTeamName(), tagSession_->awayTeamName());
        }
        contextTeam_ = gameControls_->possessionTeam();
        quickFilterTeam_ = contextTeam_;
        const QString homeLabel = (tagSession_ && !tagSession_->homeTeamName().isEmpty())
            ? tagSession_->homeTeamName() : QString("Home");
        const QString awayLabel = (tagSession_ && !tagSession_->awayTeamName().isEmpty())
            ? tagSession_->awayTeamName() : QString("Away");
        if (teamHome_) {
            teamHome_->setText(homeLabel);
            applyTeamButtonColor(teamHome_, tagSession_ ? tagSession_->homeTeamColor() : QString());
        }
        if (teamAway_) {
            teamAway_->setText(awayLabel);
            applyTeamButtonColor(teamAway_, tagSession_ ? tagSession_->awayTeamColor() : QString());
        }
        if (teamHome_) teamHome_->setChecked(contextTeam_ == "Home");
        if (teamAway_) teamAway_->setChecked(contextTeam_ == "Away");
    }
    if (modeTaggingBtn_) modeTaggingBtn_->show();
    if (modeAnalyzingBtn_) modeAnalyzingBtn_->show();

    if (tagsHeaderLabel_) tagsHeaderLabel_->show();
    if (tagsFilterButton_) tagsFilterButton_->show();
    if (undoLastTagButton_) undoLastTagButton_->show();
    updateFilterButtonsVisibility();
    if (tagsList_) tagsList_->show();
    updateFilterIndicator();
    if (statsWindow_) {
        statsWindow_->setTagSession(tagSession_);
        if (mode_ == Mode::Analyzing) statsWindow_->show();
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
    if (videoPlayer_) videoPlayer_->setControlsVisible(false);
    if (gameControls_) gameControls_->hide();
    if (modeTaggingBtn_) modeTaggingBtn_->hide();
    if (modeAnalyzingBtn_) modeAnalyzingBtn_->hide();

    if (tagSession_) tagSession_->clear();
    hasPendingTag_ = false;
    pendingMainEvent_.clear();
    pendingTimestampMs_ = 0;
    if (tagsList_) tagsList_->clear();
    if (tagsHeaderLabel_) tagsHeaderLabel_->hide();
    if (tagsFilterButton_) tagsFilterButton_->hide();
    if (tagsRemoveFiltersButton_) tagsRemoveFiltersButton_->hide();
    if (undoLastTagButton_) undoLastTagButton_->hide();
    if (tagsList_) tagsList_->hide();
    if (statsWindow_) statsWindow_->hide();

    emit videoClosed();
}

void WorkWindow::onModeToggled() {
    auto* btn = qobject_cast<QToolButton*>(sender());
    if (!btn) return;
    if (btn == modeTaggingBtn_) {
        modeAnalyzingBtn_->setChecked(false);
        setMode(Mode::Tagging);
    } else if (btn == modeAnalyzingBtn_) {
        modeTaggingBtn_->setChecked(false);
        setMode(Mode::Analyzing);
    }
}

void WorkWindow::onQuickFilterPeriodClicked() {
    auto* btn = qobject_cast<QToolButton*>(sender());
    if (!btn) return;
    QString which = btn->isChecked() ? btn->text() : QString();
    if (which == "Q1") { periodQ2_->setChecked(false); periodQ3_->setChecked(false); periodQ4_->setChecked(false); }
    if (which == "Q2") { periodQ1_->setChecked(false); periodQ3_->setChecked(false); periodQ4_->setChecked(false); }
    if (which == "Q3") { periodQ1_->setChecked(false); periodQ2_->setChecked(false); periodQ4_->setChecked(false); }
    if (which == "Q4") { periodQ1_->setChecked(false); periodQ2_->setChecked(false); periodQ3_->setChecked(false); }
    quickFilterPeriod_ = which;
    contextPeriod_ = which;
    rebuildTagsList();
}

void WorkWindow::onQuickFilterTeamClicked() {
    auto* btn = qobject_cast<QToolButton*>(sender());
    if (!btn) return;
    // Filter by internal team id (Home/Away), not display name
    const QString which = btn->isChecked() ? (btn == teamHome_ ? QString("Home") : QString("Away")) : QString();
    if (btn == teamHome_) teamAway_->setChecked(false);
    else teamHome_->setChecked(false);
    quickFilterTeam_ = which;
    contextTeam_ = which;
    rebuildTagsList();
}

void WorkWindow::onQuickFilterSituationClicked() {
    auto* btn = qobject_cast<QToolButton*>(sender());
    if (!btn) return;
    QString which = btn->isChecked() ? btn->text() : QString();
    if (btn == situationAttacking_) situationDefending_->setChecked(false);
    else situationAttacking_->setChecked(false);
    quickFilterSituation_ = which;
    contextSituation_ = which;
    rebuildTagsList();
}

void WorkWindow::onTagSelectionChanged() {
    noteDebounceTimer_->stop();
    if (pendingNoteIndex_ >= 0 && tagSession_) {
        tagSession_->setTagNote(pendingNoteIndex_, pendingNoteText_);
        pendingNoteIndex_ = -1;
    }
    loadNoteForSelectedTag();
}

void WorkWindow::onNoteTextChanged() {
    if (!notesEdit_ || !tagsList_) return;
    auto* item = tagsList_->currentItem();
    if (!item) return;
    QVariant idxVar = item->data(Qt::UserRole + 3);
    if (!idxVar.isValid()) return;
    int idx = idxVar.toInt();
    if (idx < 0 || (tagSession_ && idx >= tagSession_->tags().size())) return;
    pendingNoteIndex_ = idx;
    pendingNoteText_ = notesEdit_->toPlainText();
    noteDebounceTimer_->start(400);
}

void WorkWindow::saveNoteDebounceFired() {
    if (pendingNoteIndex_ >= 0 && tagSession_) {
        tagSession_->setTagNote(pendingNoteIndex_, pendingNoteText_);
        pendingNoteIndex_ = -1;
    }
}

void WorkWindow::syncNoteToSelectedTag() {
    if (!tagSession_ || !notesEdit_) return;
    auto* item = tagsList_->currentItem();
    if (!item) return;
    QVariant idxVar = item->data(Qt::UserRole + 3);
    if (!idxVar.isValid()) return;
    int idx = idxVar.toInt();
    if (idx < 0 || idx >= tagSession_->tags().size()) return;
    tagSession_->setTagNote(idx, notesEdit_->toPlainText());
}

void WorkWindow::showStatsOverlay() {
    if (mode_ != Mode::Tagging) return;
    if (!statsOverlayDialog_) {
        statsOverlayDialog_ = new QDialog(this, Qt::Window | Qt::WindowStaysOnTopHint);
        statsOverlayDialog_->setWindowTitle("Stats — Tag taxonomy");
        statsOverlayDialog_->setAttribute(Qt::WA_DeleteOnClose, false);
        auto* layout = new QVBoxLayout(statsOverlayDialog_);
        layout->setContentsMargins(12, 12, 12, 12);
        statsOverlay_ = new StatsWindow(statsOverlayDialog_);
        statsOverlay_->setTagSession(tagSession_);
        layout->addWidget(statsOverlay_);
        connect(statsOverlay_, &StatsWindow::filterByPathRequested, this, &WorkWindow::onFilterByPathRequested);
    }
    if (statsOverlay_) statsOverlay_->setTagSession(tagSession_);
    statsOverlayDialog_->raise();
    statsOverlayDialog_->show();
}

void WorkWindow::loadNoteForSelectedTag() {
    if (!tagSession_ || !notesEdit_) return;
    auto* item = tagsList_->currentItem();
    notesEdit_->blockSignals(true);
    if (!item) {
        notesEdit_->clear();
        notesEdit_->setEnabled(false);
        notesEdit_->setPlaceholderText("Select a tag to add a note…");
    } else {
        QVariant idxVar = item->data(Qt::UserRole + 3);
        if (idxVar.isValid()) {
            int idx = idxVar.toInt();
            if (idx >= 0 && idx < tagSession_->tags().size()) {
                notesEdit_->setPlainText(tagSession_->tagNote(idx));
                notesEdit_->setEnabled(true);
                notesEdit_->setPlaceholderText("Note for this tag…");
                notesEdit_->blockSignals(false);
                return;
            }
        }
        notesEdit_->clear();
        notesEdit_->setEnabled(true);
        notesEdit_->setPlaceholderText("Select a tag to add a note…");
    }
    notesEdit_->blockSignals(false);
}

void WorkWindow::onTagItemActivated(QListWidgetItem* item) {
    if (!item || !videoPlayer_) return;
    const qint64 posMs = item->data(Qt::UserRole).toLongLong();
    videoPlayer_->seekToMs(posMs);
}

namespace {
constexpr qint64 kPlayheadNearToleranceMs = 2000;
const QColor kTagNearPlayheadColor(147, 197, 253); // light blue, lighter than selection
const QColor kNewTagFlashColor(147, 197, 253);     // same light-blue for new-tag flash
} // namespace

void WorkWindow::flashNewTagRow() {
    if (!tagsList_ || tagsList_->count() == 0) return;
    if (newTagFlashTimer_) {
        newTagFlashTimer_->stop();
    } else {
        newTagFlashTimer_ = new QTimer(this);
        newTagFlashTimer_->setSingleShot(true);
        connect(newTagFlashTimer_, &QTimer::timeout, this, &WorkWindow::clearNewTagFlash);
    }
    newTagFlashRow_ = tagsList_->count() - 1;
    if (QListWidgetItem* item = tagsList_->item(newTagFlashRow_))
        item->setBackground(QBrush(kNewTagFlashColor));
    newTagFlashTimer_->start(500);
}

void WorkWindow::clearNewTagFlash() {
    if (newTagFlashRow_ >= 0 && tagsList_) {
        if (QListWidgetItem* item = tagsList_->item(newTagFlashRow_))
            item->setBackground(QBrush());
    }
    newTagFlashRow_ = -1;
    if (videoPlayer_)
        updateTagPlayheadHighlight(videoPlayer_->currentPositionMs());
}

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

void WorkWindow::onUndoLastTag() {
    if (!tagSession_) return;
    const int n = tagSession_->tags().size();
    if (n == 0) return;
    tagSession_->removeTag(n - 1);
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

bool WorkWindow::isTagAllowedByQuickFilters(const TagSession::GameTag& tag) const {
    if (!quickFilterPeriod_.isEmpty() && tag.period != quickFilterPeriod_) return false;
    if (!quickFilterTeam_.isEmpty() && tag.team != quickFilterTeam_) return false;
    if (!quickFilterSituation_.isEmpty() && tag.situation != quickFilterSituation_) return false;
    return true;
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
    if (!tagsList_ || !tagSession_) return;
    tagsList_->clear();

    // Collect (tag, tagSessionIndex) for tags that pass the filter
    struct TagEntry {
        TagSession::GameTag tag;
        int tagSessionIndex;
    };
    QVector<TagEntry> entries;
    int tagSessionIndex = 0;
    for (const auto& tag : tagSession_->tags()) {
        if (isTagAllowed(tag.mainEvent, tag.followUpEvent) && isTagAllowedByQuickFilters(tag)) {
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
