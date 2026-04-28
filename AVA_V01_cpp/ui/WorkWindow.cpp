#include "WorkWindow.h"
#include "../style/StyleProps.h"
#include "../components/VideoPlayer.h"
#include "../components/GameControls.h"
#include "../components/Scoreboard.h"
#include "../state/EventDefaults.h"
#include "../state/TagSession.h"
#include "StatsWindow.h"
#include "GameSetupWindow.h"
#include "../i18n/AppLocale.h"
#include "../i18n/LocaleNotifier.h"
#include "../export/ExportDialog.h"
#include "../export/VideoConcatenator.h"

#include "VideoControlsBar.h"
#include "TimelineBar.h"

#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QStackedWidget>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTemporaryDir>
#include <QToolButton>
#include <QMenu>
#include <QVideoWidget>
#include <QAbstractItemView>
#include <QHeaderView>
#include <QTableWidget>
#include <QTableWidgetItem>
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
#include <QFont>
#include <QModelIndex>
#include <QSplitter>
#include <QHBoxLayout>
#include <QScrollBar>
#include <QApplication>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QTextEdit>

namespace {

bool isTextInteractionFocusWidget(QWidget* widget) {
    if (!widget) return false;
    if (qobject_cast<QLineEdit*>(widget)) return true;
    if (qobject_cast<QAbstractSpinBox*>(widget)) return true;
    if (qobject_cast<QPlainTextEdit*>(widget)) return true;
    if (qobject_cast<QTextEdit*>(widget)) return true;
    if (qobject_cast<QComboBox*>(widget)) return true;
    return false;
}

void detachWidgetFromParent(QWidget* widget) {
    if (!widget) return;
    if (qobject_cast<QSplitter*>(widget->parentWidget())) {
        widget->setParent(nullptr);
        return;
    }
    if (QWidget* parent = widget->parentWidget()) {
        if (QLayout* layout = parent->layout()) {
            layout->removeWidget(widget);
        }
    }
}

QString formatTimestampMs(qint64 ms) {
    if (ms < 0) {
        ms = 0;
    }
    const qint64 totalSeconds = ms / 1000;
    const qint64 hours = totalSeconds / 3600;
    const qint64 minutes = (totalSeconds / 60) % 60;
    const qint64 seconds = totalSeconds % 60;

    if (hours > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QChar('0'))
            .arg(seconds, 2, 10, QChar('0'));
    }

    const qint64 minutesTotal = totalSeconds / 60;
    return QStringLiteral("%1:%2")
        .arg(minutesTotal, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'));
}

QColor foregroundForTeamBackground(const QColor& backgroundColor) {
    const double red = backgroundColor.redF();
    const double green = backgroundColor.greenF();
    const double blue = backgroundColor.blueF();
    const double luminance = 0.2126 * red + 0.7152 * green + 0.0722 * blue;
    return luminance > 0.55 ? QColor(20, 20, 20) : QColor(252, 252, 252);
}

void paintTeamCellForTag(QTableWidgetItem* teamItem, const TagSession::GameTag& tag, TagSession* session) {
    if (!teamItem) return;
    if (!session) {
        teamItem->setBackground(QBrush());
        teamItem->setForeground(QBrush());
        return;
    }
    QString hex;
    if (tag.team == QStringLiteral("Home")) {
        hex = session->homeTeamColor();
    } else if (tag.team == QStringLiteral("Away")) {
        hex = session->awayTeamColor();
    } else {
        teamItem->setBackground(QBrush());
        teamItem->setForeground(QBrush());
        return;
    }
    QString hexClean = hex.trimmed();
    if (hexClean.isEmpty()) {
        teamItem->setBackground(QBrush());
        teamItem->setForeground(QBrush());
        return;
    }
    if (!hexClean.startsWith(QLatin1Char('#'))) {
        hexClean.prepend(QLatin1Char('#'));
    }
    const QColor backgroundColor(hexClean);
    if (!backgroundColor.isValid()) {
        teamItem->setBackground(QBrush());
        teamItem->setForeground(QBrush());
        return;
    }
    teamItem->setBackground(QBrush(backgroundColor));
    teamItem->setForeground(QBrush(foregroundForTeamBackground(backgroundColor)));
}

/// Removes segments that duplicate the session team labels (embedded in follow-up strings from GameControls).
QString followUpForEventColumn(const QString& followUpEvent, const TagSession* session) {
    if (!session || followUpEvent.isEmpty()) {
        return followUpEvent;
    }
    return AppLocale::followUpPathWithoutTeamSegments(followUpEvent, session->homeTeamName(), session->awayTeamName());
}
} // namespace

WorkWindow::WorkWindow(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_StyledBackground, true);
    buildUi();
    wireSignals();
    applyTaggingLayout();
    applyUiStrings();
}

WorkWindow::~WorkWindow() {
    cleanupPendingConcatenation();
    cleanupConcatenatedVideo();
}

bool WorkWindow::shouldDeliverPlaybackKeyboardToVideoPlayer(QWidget* focusWidget) const {
    if (!focusWidget) return false;
    if (focusWidget->window() != window()) return false;
    if (!isAncestorOf(focusWidget)) return false;
    if (!contentStack_ || contentStack_->currentIndex() != 1) return false;
    if (mode_ == Mode::Analyzing && notesEdit_ && focusWidget == notesEdit_) return false;
    if (isTextInteractionFocusWidget(focusWidget)) return false;
    if (!videoPlayer_ || !videoPlayer_->isMediaKeyboardControlActive()) return false;
    return true;
}

void WorkWindow::onApplicationFocusWidgetChanged(QWidget* /*oldFocus*/, QWidget* newFocus) {
    if (!videoPlayer_ || !videoPlayer_->controlsBar()) {
        return;
    }
    const bool allowPlaybackShortcuts = shouldDeliverPlaybackKeyboardToVideoPlayer(newFocus);
    videoPlayer_->controlsBar()->setPlaybackShortcutFocusGate(allowPlaybackShortcuts);
}

void WorkWindow::refreshPlaybackShortcutFocusGate() {
    onApplicationFocusWidgetChanged(nullptr, QApplication::focusWidget());
}

void WorkWindow::setConcatenatedVideoTempDir(QTemporaryDir* dir) {
    cleanupConcatenatedVideo();
    concatenatedVideoTempDir_ = dir;
}

void WorkWindow::setPendingConcatenation(VideoConcatenator* concatenator) {
    cleanupPendingConcatenation();
    pendingConcatenator_ = concatenator;
}

void WorkWindow::cleanupConcatenatedVideo() {
    if (concatenatedVideoTempDir_) {
        delete concatenatedVideoTempDir_;
        concatenatedVideoTempDir_ = nullptr;
    }
}

void WorkWindow::cleanupPendingConcatenation() {
    delete pendingConcatenator_;
    pendingConcatenator_ = nullptr;
}

void WorkWindow::applyUiStrings() {
    if (modeTaggingBtn_) {
        modeTaggingBtn_->setText(AppLocale::trUi("mode.tagging"));
        modeTaggingBtn_->setToolTip(AppLocale::trUi("tooltip.mode_tagging"));
    }
    if (modeAnalyzingBtn_) {
        modeAnalyzingBtn_->setText(AppLocale::trUi("mode.analyzing"));
        modeAnalyzingBtn_->setToolTip(AppLocale::trUi("tooltip.mode_analyzing"));
    }
    if (videoMenuButton_) videoMenuButton_->setToolTip(AppLocale::trUi("tooltip.video_menu"));
    if (replaceVideoAction_) replaceVideoAction_->setText(AppLocale::trUi("menu.replace_video"));
    if (discardVideoAction_) discardVideoAction_->setText(AppLocale::trUi("menu.close_video"));
    if (exportClipsAction_) exportClipsAction_->setText(AppLocale::trUi("menu.export_clips"));
    if (tagsHeaderLabel_) tagsHeaderLabel_->setText(AppLocale::trUi("tags.header"));
    if (tagsFilterButton_) tagsFilterButton_->setText(AppLocale::trUi("tags.filter"));
    if (tagsRemoveFiltersButton_) tagsRemoveFiltersButton_->setText(AppLocale::trUi("tags.remove_filters"));
    if (undoLastTagButton_) {
        undoLastTagButton_->setText(AppLocale::trUi("tags.undo"));
        undoLastTagButton_->setToolTip(AppLocale::trUi("tags.undo_tooltip"));
    }
    if (notesEdit_) notesEdit_->setPlaceholderText(AppLocale::trUi("tags.note_placeholder"));
    if (tagsTable_) {
        tagsTable_->setHorizontalHeaderLabels({AppLocale::trUi("tags.col_time"), AppLocale::trUi("tags.col_team"),
                                               AppLocale::trUi("tags.col_event")});
    }
    if (statsOverlayAction_) statsOverlayAction_->setToolTip(AppLocale::trUi("stats_overlay.tooltip"));
    if (statsOverlayDialog_) statsOverlayDialog_->setWindowTitle(AppLocale::trUi("stats.overlay_title"));
    if (gameSetupWidget_) gameSetupWidget_->applyUiStrings();
    if (statsWindow_) statsWindow_->applyUiStrings();
}

void WorkWindow::onApplicationLanguageChanged() {
    applyUiStrings();
    if (gameControls_) gameControls_->applyUiLanguage();
    if (statsOverlay_) statsOverlay_->applyUiStrings();
    if (videoPlayer_ && videoPlayer_->controlsBar()) videoPlayer_->controlsBar()->applyUiStrings();
    rebuildFilterMenu();
    rebuildTagsList();
    updateFilterIndicator();
}

void WorkWindow::setTagSession(TagSession* session) {
    if (tagSession_ == session) return;
    if (tagSession_) disconnect(tagSession_, nullptr, this, nullptr);

    tagSession_ = session;
    if (statsWindow_) statsWindow_->setTagSession(tagSession_);
    if (statsOverlay_) statsOverlay_->setTagSession(tagSession_);
    if (scoreboard_) scoreboard_->setTagSession(tagSession_);

    rebuildFilterMenu();
    rebuildTagsList();

    if (!tagSession_) {
        if (gameControls_) gameControls_->setSessionTeamNames(QString(), QString());
        return;
    }

    if (gameControls_) {
        gameControls_->setSessionTeamNames(tagSession_->homeTeamName(), tagSession_->awayTeamName());
        gameControls_->setInitialTeamSide(true);
    }

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
    if (mode_ == Mode::Tagging && m == Mode::Analyzing) {
        captureTaggingModeUiStateForRestore();
    }
    mode_ = m;
    if (m == Mode::Tagging)
        applyTaggingLayout();
    else
        applyAnalyzingLayout();
    if (modeTaggingBtn_) modeTaggingBtn_->setChecked(m == Mode::Tagging);
    if (modeAnalyzingBtn_) modeAnalyzingBtn_->setChecked(m == Mode::Analyzing);
    refreshPlaybackShortcutFocusGate();
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
    mainContentLayout->setContentsMargins(12, 12, 12, 12);
    // Tight spacing between timeline and video/content so vertical space is not wasted above the video.
    mainContentLayout->setSpacing(4);

    // Top row: mode toggle | video controls | settings icon
    auto* topRow = new QWidget(mainContentContainer_);
    auto* topLayout = new QHBoxLayout(topRow);
    topLayout->setContentsMargins(0, 0, 0, 0);
    topLayout->setSpacing(12);

    modeTaggingBtn_ = new QToolButton(topRow);
    modeTaggingBtn_->setObjectName(QStringLiteral("WorkModeTaggingButton"));
    modeTaggingBtn_->setCheckable(true);
    modeTaggingBtn_->setChecked(true);
    Style::setVariant(modeTaggingBtn_, "ghost");
    Style::setSize(modeTaggingBtn_, "sm");
    modeTaggingBtn_->setCursor(Qt::PointingHandCursor);

    modeAnalyzingBtn_ = new QToolButton(topRow);
    modeAnalyzingBtn_->setObjectName(QStringLiteral("WorkModeAnalyzingButton"));
    modeAnalyzingBtn_->setCheckable(true);
    modeAnalyzingBtn_->setChecked(false);
    Style::setVariant(modeAnalyzingBtn_, "ghost");
    Style::setSize(modeAnalyzingBtn_, "sm");
    modeAnalyzingBtn_->setCursor(Qt::PointingHandCursor);

    auto* modeToggleGroup = new QWidget(topRow);
    modeToggleGroup->setObjectName(QStringLiteral("WorkModeToggleGroup"));
    auto* modeToggleLayout = new QHBoxLayout(modeToggleGroup);
    modeToggleLayout->setContentsMargins(3, 3, 3, 3);
    modeToggleLayout->setSpacing(4);
    modeToggleLayout->addWidget(modeTaggingBtn_);
    modeToggleLayout->addWidget(modeAnalyzingBtn_);
    topLayout->addWidget(modeToggleGroup);
    topLayout->addSpacing(16);

    videoPlayer_ = new VideoPlayer(this);
    // Children (video surface, controls, timeline) are reparented into WorkWindow layouts; the shell
    // widget must stay hidden or it paints an empty rectangle at (0,0) over the mode toggle row.
    videoPlayer_->hide();
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
    videoMenuButton_->setMinimumWidth(36);
    Style::setVariant(videoMenuButton_, "ghost");
    Style::setSize(videoMenuButton_, "sm");
    videoMenuButton_->setPopupMode(QToolButton::InstantPopup);
    videoMenuButton_->setCursor(Qt::PointingHandCursor);
    videoMenu_ = new QMenu(videoMenuButton_);
    replaceVideoAction_ = videoMenu_->addAction(QString());
    discardVideoAction_ = videoMenu_->addAction(QString());
    videoMenu_->addSeparator();
    exportClipsAction_ = videoMenu_->addAction(QString());
    videoMenuButton_->setMenu(videoMenu_);
    videoControlsLayout->addWidget(videoMenuButton_, 0, Qt::AlignRight | Qt::AlignVCenter);

    topLayout->addWidget(videoControlsRow_, 1);

    mainContentLayout->addWidget(topRow);

    videoTimelineRow_ = new QWidget(mainContentContainer_);
    videoTimelineRow_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* timelineLayout = new QHBoxLayout(videoTimelineRow_);
    timelineLayout->setContentsMargins(0, 0, 0, 0);
    timelineLayout->addWidget(videoPlayer_->timelineBar(), 1);
    // Timeline row uses only its natural height; all extra vertical space goes to the video/content area below.
    mainContentLayout->addWidget(videoTimelineRow_, 0);

    contentArea_ = new QWidget(mainContentContainer_);
    contentLayout_ = new QVBoxLayout(contentArea_);
    contentLayout_->setContentsMargins(0, 0, 0, 0);
    contentLayout_->setSpacing(6);
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
    taggingVideoCol_->setObjectName(QStringLiteral("TaggingVideoCol"));
    taggingVideoCol_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    taggingVideoCol_->setAttribute(Qt::WA_StyledBackground, true);
    taggingVideoCol_->setStyleSheet(QStringLiteral("#TaggingVideoCol { background-color: #FFFFFF; }"));
    auto* taggingVideoLayout = new QVBoxLayout(taggingVideoCol_);
    taggingVideoLayout->setContentsMargins(0, 0, 0, 0);
    taggingMainLayout->addWidget(taggingVideoCol_, 1);
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

    tagsHeaderRow_ = new QWidget(tagsSection_);
    auto* tagsHeaderLayout = new QHBoxLayout(tagsHeaderRow_);
    tagsHeaderLayout->setContentsMargins(0, 0, 0, 0);
    tagsHeaderLayout->setSpacing(8);

    tagsHeaderLabel_ = new QLabel(tagsHeaderRow_);
    Style::setRole(tagsHeaderLabel_, "h3");

    tagsFilterButton_ = new QToolButton(tagsHeaderRow_);
    Style::setVariant(tagsFilterButton_, "ghost");
    Style::setSize(tagsFilterButton_, "sm");
    tagsFilterButton_->setPopupMode(QToolButton::InstantPopup);
    tagsFilterButton_->setCursor(Qt::PointingHandCursor);
    tagsRemoveFiltersButton_ = new QToolButton(tagsHeaderRow_);
    Style::setVariant(tagsRemoveFiltersButton_, "ghost");
    Style::setSize(tagsRemoveFiltersButton_, "sm");
    tagsRemoveFiltersButton_->setCursor(Qt::PointingHandCursor);
    tagsRemoveFiltersButton_->hide();
    tagsFilterMenu_ = new QMenu(tagsFilterButton_);
    tagsFilterButton_->setMenu(tagsFilterMenu_);
    tagsFilterIndicator_ = new QLabel(tagsHeaderRow_);
    tagsFilterIndicator_->setWordWrap(false);
    Style::setRole(tagsFilterIndicator_, "muted");
    tagsFilterIndicator_->hide();

    undoLastTagButton_ = new QToolButton(tagsHeaderRow_);
    Style::setVariant(undoLastTagButton_, "ghost");
    Style::setSize(undoLastTagButton_, "sm");
    undoLastTagButton_->setCursor(Qt::PointingHandCursor);

    tagsHeaderLayout->addWidget(tagsHeaderLabel_, 0);
    tagsHeaderLayout->addStretch(1);
    tagsHeaderLayout->addWidget(tagsFilterIndicator_, 0);
    tagsHeaderLayout->addWidget(undoLastTagButton_, 0);
    tagsHeaderLayout->addWidget(tagsRemoveFiltersButton_, 0);
    tagsHeaderLayout->addWidget(tagsFilterButton_, 0);

    tagsTable_ = new QTableWidget(tagsSection_);
    tagsTable_->setObjectName("TagsTable");
    tagsTable_->setColumnCount(3);
    tagsTable_->setHorizontalHeaderLabels({AppLocale::trUi("tags.col_time"), AppLocale::trUi("tags.col_team"),
                                           AppLocale::trUi("tags.col_event")});
    tagsTable_->verticalHeader()->hide();
    tagsTable_->setShowGrid(false);
    tagsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    tagsTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    tagsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tagsTable_->setWordWrap(false);
    tagsTable_->horizontalHeader()->setStretchLastSection(true);
    tagsTable_->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    tagsTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    tagsTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    tagsTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    tagsTable_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    {
        QFont tagsFont = tagsTable_->font();
        if (tagsFont.pointSizeF() > 0) {
            tagsFont.setPointSizeF(qMax(6.0, tagsFont.pointSizeF() - 1.0));
        } else if (tagsFont.pixelSize() > 0) {
            tagsFont.setPixelSize(qMax(10, tagsFont.pixelSize() - 2));
        }
        tagsTable_->setFont(tagsFont);
        tagsTable_->horizontalHeader()->setFont(tagsFont);
        tagsTable_->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
        const int compactRow = qMax(28, tagsTable_->fontMetrics().height() + 10);
        tagsTable_->verticalHeader()->setDefaultSectionSize(compactRow);
    }
    const int rowHeight = qMax(28, tagsTable_->fontMetrics().height() + 10);
    const int headerH = qMax(32, tagsTable_->horizontalHeader()->sizeHint().height());
    tagsTable_->setMinimumHeight(rowHeight * 2 + headerH);
    tagsTable_->setMaximumHeight(QWIDGETSIZE_MAX);

    tagsSectionLayout->addWidget(tagsHeaderRow_);
    tagsSectionLayout->addWidget(tagsTable_, 1);

    taggingVideoTagsSplitter_ = new QSplitter(Qt::Vertical, this);
    taggingVideoTagsSplitter_->setObjectName(QStringLiteral("TaggingVideoTagsSplitter"));
    taggingVideoTagsSplitter_->setChildrenCollapsible(false);
    taggingVideoTagsSplitter_->setHandleWidth(6);
    taggingVideoTagsSplitter_->setAttribute(Qt::WA_StyledBackground, true);
    taggingVideoTagsSplitter_->setStyleSheet(
        QStringLiteral("#TaggingVideoTagsSplitter { background-color: #FFFFFF; }"));

    gameControls_ = new GameControls(this);
    scoreboard_ = new Scoreboard(this);
    statsWindow_ = new StatsWindow(this);
    statsWindow_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    statsWindow_->setMinimumHeight(180);

    notesEdit_ = new QPlainTextEdit(this);
    notesEdit_->setMaximumHeight(120);
    Style::setRole(notesEdit_, "muted");

    analyzingTagsControlsSplitter_ = new QSplitter(Qt::Horizontal, this);
    analyzingTagsControlsSplitter_->setObjectName(QStringLiteral("WorkAnalyzingTagsControlsSplitter"));
    analyzingTagsControlsSplitter_->setChildrenCollapsible(false);
    analyzingTagsControlsSplitter_->setHandleWidth(6);

    analyzingLeftSplitter_ = new QSplitter(Qt::Vertical, this);
    analyzingLeftSplitter_->setObjectName(QStringLiteral("WorkAnalyzingLeftSplitter"));
    analyzingLeftSplitter_->setChildrenCollapsible(false);
    analyzingLeftSplitter_->setHandleWidth(6);

    analyzingRightSplitter_ = new QSplitter(Qt::Vertical, this);
    analyzingRightSplitter_->setObjectName(QStringLiteral("WorkAnalyzingRightSplitter"));
    analyzingRightSplitter_->setChildrenCollapsible(false);
    analyzingRightSplitter_->setHandleWidth(6);

    analyzingMainSplitter_ = new QSplitter(Qt::Horizontal, this);
    analyzingMainSplitter_->setObjectName(QStringLiteral("WorkAnalyzingMainSplitter"));
    analyzingMainSplitter_->setChildrenCollapsible(false);
    analyzingMainSplitter_->setHandleWidth(8);
    analyzingMainSplitter_->addWidget(analyzingLeftSplitter_);
    analyzingMainSplitter_->addWidget(analyzingRightSplitter_);

    for (QSplitter* splitter :
         {analyzingMainSplitter_, analyzingLeftSplitter_, analyzingRightSplitter_, analyzingTagsControlsSplitter_}) {
        splitter->setAttribute(Qt::WA_StyledBackground, true);
        splitter->setStyleSheet(QStringLiteral("background-color: #FFFFFF;"));
    }
    analyzingMainSplitter_->hide();

    if (videoPlayer_) videoPlayer_->setControlsVisible(false);
    if (gameControls_) gameControls_->hide();
    if (scoreboard_) scoreboard_->hide();
    if (statsWindow_) statsWindow_->hide();
    if (tagsHeaderRow_) tagsHeaderRow_->hide();
    if (tagsTable_) tagsTable_->hide();
    if (modeTaggingBtn_) modeTaggingBtn_->hide();
    if (modeAnalyzingBtn_) modeAnalyzingBtn_->hide();
}

void WorkWindow::applyTaggingLayout() {
    mode_ = Mode::Tagging;
    if (analyzingMainSplitter_) analyzingMainSplitter_->hide();

    QWidget* timeline = videoPlayer_ ? videoPlayer_->timelineBar() : nullptr;
    detachWidgetFromParent(videoPlayer_ ? videoPlayer_->videoWidget() : nullptr);
    if (timeline && timeline->parentWidget() != videoTimelineRow_) {
        detachWidgetFromParent(timeline);
    }
    detachWidgetFromParent(tagsSection_);
    detachWidgetFromParent(gameControls_);
    detachWidgetFromParent(scoreboard_);
    detachWidgetFromParent(analyzingTagsControlsSplitter_);
    detachWidgetFromParent(statsWindow_);
    if (notesEdit_) detachWidgetFromParent(notesEdit_);

    if (videoTimelineRow_) {
        videoTimelineRow_->show();
        if (timeline && timeline->parentWidget() != videoTimelineRow_ && videoTimelineRow_->layout()) {
            if (auto* rowLayout = qobject_cast<QHBoxLayout*>(videoTimelineRow_->layout())) {
                rowLayout->addWidget(timeline, 1);
            } else {
                videoTimelineRow_->layout()->addWidget(timeline);
            }
        }
    }

    if (videoPlayer_ && videoPlayer_->controlsBar())
        videoPlayer_->controlsBar()->setObjectName("VideoControlsBarSlim");

    QWidget* vw = videoPlayer_->videoWidget();
    static_cast<QBoxLayout*>(taggingVideoCol_->layout())->addWidget(vw, 1);
    auto* rightLayout = static_cast<QBoxLayout*>(taggingRightCol_->layout());
    if (gameControls_) {
        // Analyzing mode lowers minimum width; restore so tagging labels are not clipped.
        gameControls_->setMinimumWidth(GameControls::kMinimumPanelWidthPx);
        gameControls_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    }
    rightLayout->addWidget(gameControls_, 1);
    rightLayout->addWidget(scoreboard_, 0);

    while (QLayoutItem* item = contentLayout_->takeAt(0)) {
        delete item;  // widget stays in tree; do not setParent(nullptr)
    }

    if (taggingVideoTagsSplitter_) {
        while (taggingVideoTagsSplitter_->count() > 0) {
            taggingVideoTagsSplitter_->widget(0)->setParent(nullptr);
        }
        taggingVideoTagsSplitter_->addWidget(taggingMainRow_);
        taggingVideoTagsSplitter_->addWidget(tagsSection_);
        taggingVideoTagsSplitter_->setStretchFactor(0, 3);
        taggingVideoTagsSplitter_->setStretchFactor(1, 2);
        contentLayout_->addWidget(taggingVideoTagsSplitter_, 1);
    } else {
        contentLayout_->addWidget(taggingMainRow_, 1);
        contentLayout_->addWidget(tagsSection_, 0);
    }

    if (tagsHeaderRow_) tagsHeaderRow_->hide();

    const int rh = qMax(20, tagsTable_->fontMetrics().height() + 4);
    const int headerH = tagsTable_->horizontalHeader()->sizeHint().height();
    tagsTable_->setMinimumHeight(rh * 2 + headerH);
    tagsTable_->setMaximumHeight(QWIDGETSIZE_MAX);
    if (tagsSection_) tagsSection_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    statsWindow_->hide();
    if (notesEdit_) notesEdit_->hide();

    if (gameControls_) gameControls_->show();
    if (scoreboard_) scoreboard_->show();

    if (taggingVideoTagsSplitter_) {
        taggingVideoTagsSplitter_->show();
    }
    // applyAnalyzingLayout() hides this row; a parent splitter show() does not un-hide explicit child hides.
    if (taggingMainRow_) {
        taggingMainRow_->show();
    }

    QTimer::singleShot(0, this, [this]() { restoreTaggingModeUiStateAfterLayout(); });

    // Keep tag list in sync with session after layout change
    rebuildTagsList();
}

void WorkWindow::applyAnalyzingLayout() {
    mode_ = Mode::Analyzing;
    if (taggingMainRow_) taggingMainRow_->hide();
    if (taggingVideoTagsSplitter_) taggingVideoTagsSplitter_->hide();
    if (videoPlayer_ && videoPlayer_->controlsBar())
        videoPlayer_->controlsBar()->setObjectName(""); // normal height

    while (QLayoutItem* item = contentLayout_->takeAt(0)) {
        delete item;  // widget stays in tree; do not setParent(nullptr)
    }

    QWidget* vw = videoPlayer_->videoWidget();
    QWidget* timeline = videoPlayer_ ? videoPlayer_->timelineBar() : nullptr;
    if (!vw || !timeline || !analyzingMainSplitter_ || !analyzingLeftSplitter_ || !analyzingRightSplitter_ ||
        !analyzingTagsControlsSplitter_) {
        rebuildTagsList();
        return;
    }

    detachWidgetFromParent(vw);
    detachWidgetFromParent(tagsSection_);
    detachWidgetFromParent(gameControls_);
    detachWidgetFromParent(scoreboard_);
    detachWidgetFromParent(statsWindow_);
    if (notesEdit_) detachWidgetFromParent(notesEdit_);
    detachWidgetFromParent(timeline);
    if (scoreboard_) scoreboard_->hide();

    if (videoTimelineRow_) videoTimelineRow_->hide();

    while (analyzingTagsControlsSplitter_->count() > 0) {
        analyzingTagsControlsSplitter_->widget(0)->setParent(nullptr);
    }
    while (analyzingLeftSplitter_->count() > 0) {
        analyzingLeftSplitter_->widget(0)->setParent(nullptr);
    }
    while (analyzingRightSplitter_->count() > 0) {
        analyzingRightSplitter_->widget(0)->setParent(nullptr);
    }

    timeline->setMinimumHeight(44);
    timeline->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tagsSection_->setMinimumWidth(160);
    tagsSection_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    gameControls_->setMinimumWidth(GameControls::kMinimumPanelWidthPx);
    gameControls_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    analyzingTagsControlsSplitter_->addWidget(tagsSection_);
    analyzingTagsControlsSplitter_->addWidget(gameControls_);

    analyzingLeftSplitter_->addWidget(vw);
    analyzingLeftSplitter_->addWidget(timeline);
    analyzingLeftSplitter_->addWidget(analyzingTagsControlsSplitter_);

    analyzingRightSplitter_->addWidget(statsWindow_);
    if (notesEdit_) analyzingRightSplitter_->addWidget(notesEdit_);

    analyzingMainSplitter_->setStretchFactor(0, 2);
    analyzingMainSplitter_->setStretchFactor(1, 1);
    analyzingLeftSplitter_->setStretchFactor(0, 5);
    analyzingLeftSplitter_->setStretchFactor(1, 0);
    analyzingLeftSplitter_->setStretchFactor(2, 3);
    analyzingTagsControlsSplitter_->setStretchFactor(0, 1);
    analyzingTagsControlsSplitter_->setStretchFactor(1, 1);
    analyzingRightSplitter_->setStretchFactor(0, 3);
    analyzingRightSplitter_->setStretchFactor(1, 1);

    contentLayout_->addWidget(analyzingMainSplitter_, 1);

    statsWindow_->show();
    if (notesEdit_) notesEdit_->show();
    tagsTable_->setMaximumHeight(QWIDGETSIZE_MAX);

    if (tagsHeaderRow_) tagsHeaderRow_->show();

    if (analyzingMainSplitter_) analyzingMainSplitter_->show();

    QTimer::singleShot(0, this, [this]() { applyAnalyzingSplitterGeometry(); });

    // Keep tag list in sync with session after layout change
    rebuildTagsList();
}

void WorkWindow::applyAnalyzingSplitterGeometry() {
    if (mode_ != Mode::Analyzing || !analyzingMainSplitter_) return;

    const int totalW = analyzingMainSplitter_->width();
    if (totalW >= 120) {
        const int leftW = qMax(200, totalW * 2 / 3);
        const int rightW = qMax(160, totalW - leftW);
        analyzingMainSplitter_->setSizes({leftW, rightW});
    }

    const int leftH = analyzingLeftSplitter_ ? analyzingLeftSplitter_->height() : 0;
    if (analyzingLeftSplitter_ && leftH >= 120) {
        const int handleTotal = analyzingLeftSplitter_->handleWidth() * 2;
        const int inner = leftH - handleTotal;
        const int videoH = qMax(140, inner * 45 / 100);
        const int timelineH = qMax(44, inner * 12 / 100);
        const int tagsControlsRowH = qMax(120, inner - videoH - timelineH);
        analyzingLeftSplitter_->setSizes({videoH, timelineH, tagsControlsRowH});
    }

    const int tagsControlsRowW = analyzingTagsControlsSplitter_ ? analyzingTagsControlsSplitter_->width() : 0;
    if (analyzingTagsControlsSplitter_ && tagsControlsRowW >= 120) {
        const int tagsW = qMax(200, tagsControlsRowW * 1 / 2);
        const int controlsW = qMax(200, tagsControlsRowW - tagsW);
        analyzingTagsControlsSplitter_->setSizes({tagsW, controlsW});
    }

    const int rightH = analyzingRightSplitter_ ? analyzingRightSplitter_->height() : 0;
    if (analyzingRightSplitter_ && rightH >= 100) {
        const int handle = analyzingRightSplitter_->handleWidth();
        const int inner = rightH - handle;
        const int statsH = qMax(180, inner * 72 / 100);
        const int notesH = qMax(80, inner - statsH);
        analyzingRightSplitter_->setSizes({statsH, notesH});
    }
}

void WorkWindow::applyTaggingSplitterGeometry() {
    if (mode_ != Mode::Tagging || !taggingVideoTagsSplitter_) return;
    const int h = taggingVideoTagsSplitter_->height();
    if (h < 100) return;
    const int handle = taggingVideoTagsSplitter_->handleWidth();
    const int inner = h - handle;
    const int topH = qMax(160, inner * 58 / 100);
    const int bottomH = qMax(120, inner - topH);
    taggingVideoTagsSplitter_->setSizes({topH, bottomH});
}

void WorkWindow::captureTaggingModeUiStateForRestore() {
    if (!taggingVideoTagsSplitter_ || taggingVideoTagsSplitter_->count() != 2) {
        return;
    }
    preservedTaggingVideoTagsSplitterSizes_ = taggingVideoTagsSplitter_->sizes();
    hasPreservedTaggingUiState_ = true;
    if (tagsTable_) {
        preservedTagsTableVerticalScrollValue_ = tagsTable_->verticalScrollBar()->value();
        preservedTagsTableHorizontalScrollValue_ = tagsTable_->horizontalScrollBar()->value();
    }
}

void WorkWindow::restoreTaggingModeUiStateAfterLayout() {
    if (mode_ != Mode::Tagging) return;
    if (hasPreservedTaggingUiState_ && preservedTaggingVideoTagsSplitterSizes_.size() == 2 &&
        taggingVideoTagsSplitter_) {
        const int h = taggingVideoTagsSplitter_->height();
        if (h >= 100) {
            taggingVideoTagsSplitter_->setSizes(preservedTaggingVideoTagsSplitterSizes_);
        }
    } else {
        applyTaggingSplitterGeometry();
    }
    if (tagsTable_ && hasPreservedTaggingUiState_) {
        tagsTable_->verticalScrollBar()->setValue(preservedTagsTableVerticalScrollValue_);
        tagsTable_->horizontalScrollBar()->setValue(preservedTagsTableHorizontalScrollValue_);
    }
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

    connect(exportClipsAction_, &QAction::triggered, this, &WorkWindow::onExportClips);

    // GameControls -> capture timestamp and store tags
    connect(gameControls_, &GameControls::mainEventPressed, this, [this](const QString& mainEvent) {
        if (!videoPlayer_) return;
        pendingMainEvent_ = mainEvent;
        pendingTimestampMs_ = videoPlayer_->currentPositionMs();
        hasPendingTag_ = true;
    });

    connect(gameControls_, &GameControls::teamSideSelected, this, [this](bool isHome) {
        contextTeam_ = isHome ? QStringLiteral("Home") : QStringLiteral("Away");
    });

    if (gameControls_) {
        connect(gameControls_, &GameControls::gameStartRequested, this,
                &WorkWindow::onGameStartRequested);
        connect(gameControls_, &GameControls::nextQuarterRequested, this,
                &WorkWindow::onNextQuarterRequested);
    }

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
            // Refresh the period context from GameControls so every newly tagged event lands in the
            // correct quarter even if the user has not interacted with WorkWindow in between.
            if (gameControls_) {
                const QString currentPeriod = gameControls_->currentPeriodName();
                if (!currentPeriod.isEmpty()) {
                    contextPeriod_ = currentPeriod;
                }
            }
            TagSession::GameTag tag;
            tag.mainEvent = mainEvent;
            tag.followUpEvent = followUpEvent;
            tag.positionMs = timestampMs;
            TagSession::GameTag ctx = currentTagContext();
            tag.period = ctx.period;
            tag.situation = ctx.situation;
            if (gameControls_) {
                const QString sideKey = gameControls_->selectedTeamSideKey();
                tag.team = sideKey.isEmpty() ? ctx.team : sideKey;
                if (!sideKey.isEmpty()) {
                    contextTeam_ = sideKey;
                }
            } else {
                tag.team = ctx.team;
            }
            // tag.startMs / tag.endMs left at 0 so TagSession::addTag seeds them from EventDefaults.
            tagSession_->addTag(tag);
        }
    });

    connect(tagsTable_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) { onTagTableSeekToRow(row); });
    connect(tagsTable_, &QAbstractItemView::activated, this, [this](const QModelIndex& index) {
        if (index.isValid()) {
            onTagTableSeekToRow(index.row());
        }
    });
    connect(tagsTable_, &QTableWidget::itemSelectionChanged, this, &WorkWindow::onTagSelectionChanged);

    connect(modeTaggingBtn_, &QToolButton::clicked, this, &WorkWindow::onModeToggled);
    connect(modeAnalyzingBtn_, &QToolButton::clicked, this, &WorkWindow::onModeToggled);

    if (notesEdit_)
        connect(notesEdit_, &QPlainTextEdit::textChanged, this, &WorkWindow::onNoteTextChanged);

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

    statsOverlayAction_ = new QAction(this);
    statsOverlayAction_->setShortcut(QKeySequence(Qt::Key_Comma));
    statsOverlayAction_->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(statsOverlayAction_, &QAction::triggered, this, &WorkWindow::showStatsOverlay);
    addAction(statsOverlayAction_);

    connect(&LocaleNotifier::instance(), &LocaleNotifier::languageChanged, this,
            &WorkWindow::onApplicationLanguageChanged);

    noteDebounceTimer_ = new QTimer(this);
    noteDebounceTimer_->setSingleShot(true);
    connect(noteDebounceTimer_, &QTimer::timeout, this, &WorkWindow::saveNoteDebounceFired);

    // Debounce playhead-driven table scans: scoreboard is O(log n), but row highlighting is O(rows).
    playheadSideEffectsDebounceTimer_ = new QTimer(this);
    playheadSideEffectsDebounceTimer_->setSingleShot(true);
    playheadSideEffectsDebounceTimer_->setInterval(200);
    connect(playheadSideEffectsDebounceTimer_, &QTimer::timeout, this, [this]() {
        onPlayheadPositionChanged(lastPlayheadPositionForSideEffectsMs_);
    });
    connect(videoPlayer_, &VideoPlayer::positionChangedMs, this, [this](qint64 positionMs) {
        lastPlayheadPositionForSideEffectsMs_ = positionMs;
        playheadSideEffectsDebounceTimer_->start();
    });
    
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

    if (QApplication* application = qobject_cast<QApplication*>(QApplication::instance())) {
        connect(application, &QApplication::focusChanged, this, &WorkWindow::onApplicationFocusWidgetChanged);
    }
    refreshPlaybackShortcutFocusGate();
}


void WorkWindow::showTeamSetupForVideo(const QString& filePath) {
    if (!gameSetupWidget_ || !contentStack_) return;
    gameSetupWidget_->setVideoPath(filePath);
    gameSetupWidget_->setTeamDefaults(QString(), QString(), QString(), QString());
    gameSetupWidget_->setMetadataDefaults(QString(), QDate::currentDate(), QString(), QString());
    contentStack_->setCurrentIndex(0);
    gameSetupWidget_->setInitialFocus();
    refreshPlaybackShortcutFocusGate();
}

void WorkWindow::onTeamSetupConfirmed(const QString& filePath,
                                       const QString& homeName, const QString& awayName,
                                       const QString& homeColor, const QString& awayColor,
                                       const QString& competitionName,
                                       const QDate& gameDate,
                                       const QString& homeAbbrev,
                                       const QString& awayAbbrev) {
    if (pendingConcatenator_) {
        const bool concatOk = pendingConcatenator_->waitWithProgress(this);
        if (!concatOk) {
            const QString errorMsg = pendingConcatenator_->errorMessage();
            cleanupPendingConcatenation();
            cleanupConcatenatedVideo();
            if (!errorMsg.isEmpty()) {
                QMessageBox::warning(this, AppLocale::trUi("app.title"), errorMsg);
            }
            emit videoClosed();
            return;
        }
        delete pendingConcatenator_;
        pendingConcatenator_ = nullptr;
    }

    if (tagSession_) {
        tagSession_->setGameTeams(homeName, awayName, homeColor, awayColor);
        tagSession_->setGameMetadata(competitionName, gameDate, homeAbbrev, awayAbbrev);
    }
    if (contentStack_) contentStack_->setCurrentIndex(1);
    loadVideoFromFile(filePath);
}

void WorkWindow::onGameStartRequested() {
    if (!videoPlayer_ || !tagSession_) return;
    const qint64 anchorMs = videoPlayer_->currentPositionMs();
    tagSession_->setGameStartAnchor(anchorMs);
    tagSession_->setCurrentQuarter(0, anchorMs);
    contextPeriod_ = QStringLiteral("Q1");

    // Insert the start-anchor instance: a 2-second window starting at the anchor moment.
    TagSession::GameTag anchorTag;
    anchorTag.mainEvent = QString::fromLatin1(EventDefaults::TimeCodes::kStartAnchor);
    anchorTag.positionMs = anchorMs;
    anchorTag.startMs = anchorMs;
    anchorTag.endMs = anchorMs + 2000;
    anchorTag.period = QStringLiteral("Q1");
    anchorTag.intervalManuallyEdited = true; // anchor span is fixed; default-table changes must not move it
    tagSession_->addTag(anchorTag);
}

void WorkWindow::onNextQuarterRequested() {
    if (!videoPlayer_ || !tagSession_) return;
    if (tagSession_->currentQuarterIndex() < 0) return;

    const int closingIndex = tagSession_->currentQuarterIndex();
    const qint64 quarterStartMs = tagSession_->currentQuarterStartMs();
    const qint64 nowMs = videoPlayer_->currentPositionMs();
    const qint64 endMs = nowMs >= quarterStartMs ? nowMs : quarterStartMs;

    static const QString kQuarterCodes[4] = {
        QString::fromLatin1(EventDefaults::TimeCodes::kQuarter1),
        QString::fromLatin1(EventDefaults::TimeCodes::kQuarter2),
        QString::fromLatin1(EventDefaults::TimeCodes::kQuarter3),
        QString::fromLatin1(EventDefaults::TimeCodes::kQuarter4),
    };

    if (closingIndex < 0 || closingIndex > 3) return;

    TagSession::GameTag quarterTag;
    quarterTag.mainEvent = kQuarterCodes[closingIndex];
    quarterTag.positionMs = quarterStartMs;
    quarterTag.startMs = quarterStartMs;
    quarterTag.endMs = endMs;
    quarterTag.period = kQuarterCodes[closingIndex];
    quarterTag.intervalManuallyEdited = true; // quarter span is anchored to user clicks
    tagSession_->addTag(quarterTag);

    const int nextIndex = closingIndex + 1;
    if (nextIndex >= 4) {
        tagSession_->clearCurrentQuarter();
        tagSession_->setQuarterPhase(TagSession::QuarterPhase::GameEnded);
        contextPeriod_.clear();
    } else {
        tagSession_->setCurrentQuarter(nextIndex, nowMs);
        contextPeriod_ = kQuarterCodes[nextIndex];
    }
}

void WorkWindow::onTeamSetupCancelled() {
    cleanupPendingConcatenation();
    cleanupConcatenatedVideo();
    emit videoClosed();
}

void WorkWindow::loadVideoFromFile(const QString& filePath) {
    if (filePath.isEmpty()) return;

    sourceVideoPath_ = filePath;
    hasPreservedTaggingUiState_ = false;
    preservedTaggingVideoTagsSplitterSizes_.clear();

    if (tagSession_) tagSession_->clear();
    hasPendingTag_ = false;
    pendingMainEvent_.clear();
    pendingTimestampMs_ = 0;
    contextPeriod_.clear();
    if (tagsTable_) tagsTable_->setRowCount(0);

    if (videoPlayer_) {
        videoPlayer_->loadVideoFromFile(filePath);
        videoPlayer_->setControlsVisible(true);
    }

    if (gameControls_) {
        gameControls_->resetGameTimeState();
        gameControls_->show();
        if (tagSession_) {
            gameControls_->setSessionTeamNames(tagSession_->homeTeamName(), tagSession_->awayTeamName());
            gameControls_->setInitialTeamSide(true);
        }
        contextTeam_ = "Home";
    }
    if (scoreboard_) {
        scoreboard_->setTagSession(tagSession_);
        scoreboard_->show();
    }
    if (modeTaggingBtn_) modeTaggingBtn_->show();
    if (modeAnalyzingBtn_) modeAnalyzingBtn_->show();

    if (mode_ == Mode::Analyzing) {
        if (tagsHeaderRow_) tagsHeaderRow_->show();
        if (tagsHeaderLabel_) tagsHeaderLabel_->show();
        if (tagsFilterButton_) tagsFilterButton_->show();
        if (undoLastTagButton_) undoLastTagButton_->show();
        updateFilterButtonsVisibility();
    } else {
        if (tagsHeaderRow_) tagsHeaderRow_->hide();
    }
    if (tagsTable_) tagsTable_->show();
    updateFilterIndicator();
    if (statsWindow_) {
        statsWindow_->setTagSession(tagSession_);
        if (mode_ == Mode::Analyzing) statsWindow_->show();
    }

    rebuildFilterMenu();
    rebuildTagsList();
    refreshPlaybackShortcutFocusGate();
}

void WorkWindow::onReplaceVideo() {
    QStringList filePaths = VideoConcatenator::selectVideoFiles(this);
    if (filePaths.isEmpty()) return;

    if (filePaths.size() == 1) {
        cleanupPendingConcatenation();
        cleanupConcatenatedVideo();
        loadVideoFromFile(filePaths.first());
        return;
    }

    filePaths.sort(Qt::CaseInsensitive);
    if (!VideoConcatenator::showFileOrderDialog(filePaths, this)) return;

    auto* tempDir = new QTemporaryDir();
    if (!tempDir->isValid()) {
        delete tempDir;
        QMessageBox::warning(this,
                             AppLocale::trUi("app.title"),
                             AppLocale::trUi("concat.error_failed"));
        return;
    }

    auto* concatenator = new VideoConcatenator(this);
    concatenator->startConcatenation(filePaths, tempDir->path());

    if (!concatenator->waitWithProgress(this)) {
        const QString errorMsg = concatenator->errorMessage();
        delete concatenator;
        delete tempDir;
        if (!errorMsg.isEmpty()) {
            QMessageBox::warning(this, AppLocale::trUi("app.title"), errorMsg);
        }
        return;
    }

    const QString outputPath = concatenator->outputPath();
    delete concatenator;

    cleanupPendingConcatenation();
    cleanupConcatenatedVideo();
    concatenatedVideoTempDir_ = tempDir;
    loadVideoFromFile(outputPath);
}

void WorkWindow::onDiscardVideo() {
    hasPreservedTaggingUiState_ = false;
    preservedTaggingVideoTagsSplitterSizes_.clear();

    if (videoPlayer_) videoPlayer_->setControlsVisible(false);
    if (gameControls_) {
        gameControls_->resetGameTimeState();
        gameControls_->hide();
    }
    if (scoreboard_) scoreboard_->hide();
    if (modeTaggingBtn_) modeTaggingBtn_->hide();
    if (modeAnalyzingBtn_) modeAnalyzingBtn_->hide();

    if (tagSession_) tagSession_->clear();
    hasPendingTag_ = false;
    pendingMainEvent_.clear();
    pendingTimestampMs_ = 0;
    contextPeriod_.clear();
    if (tagsTable_) tagsTable_->setRowCount(0);
    if (tagsHeaderRow_) tagsHeaderRow_->hide();
    if (tagsTable_) tagsTable_->hide();
    if (statsWindow_) statsWindow_->hide();

    cleanupConcatenatedVideo();
    emit videoClosed();
    refreshPlaybackShortcutFocusGate();
}

void WorkWindow::onExportClips() {
    if (!tagSession_ || tagSession_->tags().isEmpty() || sourceVideoPath_.isEmpty()) return;

    const qint64 duration = videoPlayer_ ? videoPlayer_->durationMs() : 0;
    auto* dialog = new ExportDialog(tagSession_, sourceVideoPath_, duration, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setModal(true);
    if (videoPlayer_) {
        videoPlayer_->setPlaybackKeyboardShortcutsEnabled(false);
    }
    connect(dialog, &QDialog::finished, this, [this](int) {
        if (videoPlayer_) {
            videoPlayer_->setPlaybackKeyboardShortcutsEnabled(true);
        }
    });
    dialog->show();
}

void WorkWindow::onModeToggled() {
    auto* btn = qobject_cast<QToolButton*>(sender());
    if (!btn) return;
    if (btn == modeTaggingBtn_) {
        setMode(Mode::Tagging);
    } else if (btn == modeAnalyzingBtn_) {
        setMode(Mode::Analyzing);
    }
    if (modeTaggingBtn_) modeTaggingBtn_->setChecked(mode_ == Mode::Tagging);
    if (modeAnalyzingBtn_) modeAnalyzingBtn_->setChecked(mode_ == Mode::Analyzing);
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
    if (!notesEdit_ || !tagsTable_) return;
    auto* item = currentTagKeyItem();
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
    auto* item = currentTagKeyItem();
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
        statsOverlayDialog_->setWindowTitle(AppLocale::trUi("stats.overlay_title"));
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
    auto* item = currentTagKeyItem();
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

void WorkWindow::onTagTableSeekToRow(int row) {
    if (row < 0 || !videoPlayer_ || !tagsTable_) return;
    QTableWidgetItem* keyItem = tagsTable_->item(row, 0);
    if (!keyItem) return;
    videoPlayer_->seekToMs(keyItem->data(Qt::UserRole).toLongLong());
}

QTableWidgetItem* WorkWindow::currentTagKeyItem() const {
    if (!tagsTable_) return nullptr;
    const int row = tagsTable_->currentRow();
    return row >= 0 ? tagsTable_->item(row, 0) : nullptr;
}

void WorkWindow::setTagTableRowBackground(int row, const QBrush& brush) {
    if (!tagsTable_ || row < 0) return;
    const bool clearHighlight = (brush.style() == Qt::NoBrush);

    if (QTableWidgetItem* timeItem = tagsTable_->item(row, 0)) {
        if (clearHighlight) {
            timeItem->setBackground(QBrush());
        } else {
            timeItem->setBackground(brush);
        }
    }
    if (QTableWidgetItem* eventItem = tagsTable_->item(row, 2)) {
        if (clearHighlight) {
            eventItem->setBackground(QBrush());
        } else {
            eventItem->setBackground(brush);
        }
    }
    if (QTableWidgetItem* teamItem = tagsTable_->item(row, 1)) {
        const QVariant tagIndexVar = tagsTable_->item(row, 0) ? tagsTable_->item(row, 0)->data(Qt::UserRole + 3) : QVariant();
        if (tagSession_ && tagIndexVar.isValid()) {
            const int tagIndex = tagIndexVar.toInt();
            if (tagIndex >= 0 && tagIndex < tagSession_->tags().size()) {
                paintTeamCellForTag(teamItem, tagSession_->tags().at(tagIndex), tagSession_);
                return;
            }
        }
        paintTeamCellForTag(teamItem, TagSession::GameTag{}, tagSession_);
    }
}

QString WorkWindow::displayTeamForTag(const TagSession::GameTag& tag) const {
    if (!tagSession_) {
        if (tag.team == QStringLiteral("Home")) return QStringLiteral("Home");
        if (tag.team == QStringLiteral("Away")) return QStringLiteral("Away");
        return tag.team.isEmpty() ? QStringLiteral("—") : tag.team;
    }
    if (tag.team == QStringLiteral("Home")) {
        const QString n = tagSession_->homeTeamName();
        return n.isEmpty() ? QStringLiteral("Home") : n;
    }
    if (tag.team == QStringLiteral("Away")) {
        const QString n = tagSession_->awayTeamName();
        return n.isEmpty() ? QStringLiteral("Away") : n;
    }
    return tag.team.isEmpty() ? QStringLiteral("—") : tag.team;
}

namespace {
constexpr qint64 kPlayheadNearToleranceMs = 2000;
const QColor kTagNearPlayheadColor(147, 197, 253); // light blue, lighter than selection
const QColor kNewTagFlashColor(147, 197, 253);     // same light-blue for new-tag flash
} // namespace

void WorkWindow::flashNewTagRow() {
    if (!tagsTable_ || tagsTable_->rowCount() == 0) return;
    if (newTagFlashTimer_) {
        newTagFlashTimer_->stop();
    } else {
        newTagFlashTimer_ = new QTimer(this);
        newTagFlashTimer_->setSingleShot(true);
        connect(newTagFlashTimer_, &QTimer::timeout, this, &WorkWindow::clearNewTagFlash);
    }
    newTagFlashRow_ = tagsTable_->rowCount() - 1;
    setTagTableRowBackground(newTagFlashRow_, QBrush(kNewTagFlashColor));
    newTagFlashTimer_->start(500);
}

void WorkWindow::clearNewTagFlash() {
    if (newTagFlashRow_ >= 0 && tagsTable_) {
        setTagTableRowBackground(newTagFlashRow_, QBrush());
    }
    newTagFlashRow_ = -1;
    if (videoPlayer_)
        updateTagPlayheadHighlight(videoPlayer_->currentPositionMs());
}

void WorkWindow::onPlayheadPositionChanged(qint64 positionMs) {
    updateTagPlayheadHighlight(positionMs);
    if (scoreboard_) scoreboard_->setCurrentTimestampMs(positionMs);
}

void WorkWindow::updateTagPlayheadHighlight(qint64 positionMs) {
    if (!tagsTable_) return;
    for (int row = 0; row < tagsTable_->rowCount(); ++row) {
        QTableWidgetItem* keyItem = tagsTable_->item(row, 0);
        if (!keyItem) continue;
        const qint64 tagMs = keyItem->data(Qt::UserRole).toLongLong();
        const qint64 diff = (tagMs > positionMs) ? (tagMs - positionMs) : (positionMs - tagMs);
        if (diff <= kPlayheadNearToleranceMs) {
            setTagTableRowBackground(row, QBrush(kTagNearPlayheadColor));
        } else {
            setTagTableRowBackground(row, QBrush());
        }
    }
}

void WorkWindow::onDeleteSelectedTag() {
    if (!tagsTable_ || !tagSession_) return;

    auto* item = currentTagKeyItem();
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
    (void)tag;
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

    auto* selectAll = tagsFilterMenu_->addAction(AppLocale::trUi("filter.select_all"));
    auto* selectNone = tagsFilterMenu_->addAction(AppLocale::trUi("filter.select_none"));
    connect(selectAll, &QAction::triggered, this, &WorkWindow::onSelectAllFilters);
    connect(selectNone, &QAction::triggered, this, &WorkWindow::onSelectNoFilters);
    tagsFilterMenu_->addSeparator();

    if (!tagSession_) return;
    QStringList mains = tagSession_->mainEventCounts().keys();
    mains.sort(Qt::CaseInsensitive);

    for (const QString& mainEvent : mains) {
        auto* act = tagsFilterMenu_->addAction(AppLocale::trEvent(mainEvent));
        act->setCheckable(true);
        act->setChecked(prevChecked.contains(mainEvent) ? prevChecked.value(mainEvent) : true);
        connect(act, &QAction::toggled, this, &WorkWindow::onFilterActionToggled);
        filterActionByMainEvent_.insert(mainEvent, act);
    }
}

void WorkWindow::updateFilterIndicator() {
    if (!tagsFilterIndicator_) return;

    if (!activeFilterPathMainEvent_.isEmpty()) {
        QString pathText = AppLocale::trEvent(activeFilterPathMainEvent_);
        if (!activeFilterPathFollowUp_.isEmpty()) {
            pathText += QStringLiteral(" → ") + AppLocale::translateCompoundPath(activeFilterPathFollowUp_);
        }
        tagsFilterIndicator_->setText(AppLocale::trUi("filter.indicator_path") + pathText);
        tagsFilterIndicator_->show();
        return;
    }

    QStringList activeFilters;
    for (auto it = filterActionByMainEvent_.cbegin(); it != filterActionByMainEvent_.cend(); ++it) {
        if (it.value()->isChecked()) {
            activeFilters.append(AppLocale::trEvent(it.key()));
        }
    }

    if (activeFilters.isEmpty() || activeFilters.size() == filterActionByMainEvent_.size()) {
        tagsFilterIndicator_->hide();
        return;
    }

    activeFilters.sort(Qt::CaseInsensitive);
    const QString text = AppLocale::trUi("filter.indicator_list") + activeFilters.join(QStringLiteral(", "));
    tagsFilterIndicator_->setText(text);
    tagsFilterIndicator_->show();
}

void WorkWindow::rebuildTagsList() {
    if (!tagsTable_ || !tagSession_) return;
    tagsTable_->setRowCount(0);

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

    tagsTable_->setRowCount(entries.size());
    int row = 0;
    for (const auto& e : entries) {
        const auto& tag = e.tag;
        const QString timeText = formatTimestampMs(tag.positionMs);
        const QString teamText = displayTeamForTag(tag);
        const QString eventText =
            AppLocale::trDisplayTagLine(tag.mainEvent, followUpForEventColumn(tag.followUpEvent, tagSession_));

        auto* timeItem = new QTableWidgetItem(timeText);
        timeItem->setData(Qt::UserRole, tag.positionMs);
        timeItem->setData(Qt::UserRole + 1, tag.mainEvent);
        timeItem->setData(Qt::UserRole + 2, tag.followUpEvent);
        timeItem->setData(Qt::UserRole + 3, e.tagSessionIndex);
        timeItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);

        auto* teamItem = new QTableWidgetItem(teamText);
        teamItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        paintTeamCellForTag(teamItem, tag, tagSession_);

        auto* eventItem = new QTableWidgetItem(eventText);
        eventItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);

        tagsTable_->setItem(row, 0, timeItem);
        tagsTable_->setItem(row, 1, teamItem);
        tagsTable_->setItem(row, 2, eventItem);
        ++row;
    }

    tagsTable_->resizeColumnToContents(0);
    tagsTable_->resizeColumnToContents(1);
    tagsTable_->scrollToBottom();
    updateFilterIndicator();
    updateFilterButtonsVisibility();
    if (videoPlayer_) {
        updateTagPlayheadHighlight(videoPlayer_->currentPositionMs());
    }
}
