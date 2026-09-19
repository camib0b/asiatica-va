#include "WorkWindow.h"
#include "ClipDurationSettingsDialog.h"
#include "PresentationPanel.h"
#include "../style/StyleProps.h"
#include "../style/ThemeColors.h"
#include "../components/VideoPlayer.h"
#include "../components/GameControls.h"
#include "../components/MatchNotesEditor.h"
#include "../state/EventDefaults.h"
#include "../state/PresentationQueue.h"
#include "../state/TagSession.h"
#include "StatsWindow.h"
#include "GameSetupWindow.h"
#include "../i18n/AppLocale.h"
#include "../i18n/LocaleNotifier.h"
#include "../export/ClipTrimBar.h"
#include "../export/ExportSettingsDialog.h"
#include "../export/ExportClipBuilder.h"
#include "../export/ExportJobManager.h"
#include "../export/ExportJobsBar.h"
#include "../export/VideoConcatenator.h"
#include "../export/PlaybackVideoPreparer.h"
#include "../export/XmlImporter.h"
#include "../license/LicenseManager.h"
#include "XmlSyncDialog.h"
#include "XmlEventMappingDialog.h"

#include "VideoControlsBar.h"
#include "TimelineBar.h"

#include <QLabel>
#include <QLineEdit>
#include <QFileDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTemporaryDir>
#include <QToolButton>
#include <QFontMetrics>
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
#include <QLayout>
#include <QFileInfo>
#include <QTimer>
#include <QSignalBlocker>
#include <QEvent>
#include <QDialog>
#include <QFont>
#include <QModelIndex>
#include <QSplitter>
#include <QHBoxLayout>
#include <QScrollBar>
#include <QApplication>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QKeyEvent>
#include <QTextEdit>

#include <algorithm>

namespace {

constexpr int kTagMarkMsRole = Qt::UserRole;
constexpr int kTagMainEventRole = Qt::UserRole + 1;
constexpr int kTagFollowUpEventRole = Qt::UserRole + 2;
constexpr int kTagSessionIndexRole = Qt::UserRole + 3;
constexpr int kTagIdRole = Qt::UserRole + 4;

class ScopedTrueFlag {
public:
    explicit ScopedTrueFlag(bool& flag) : flag_(flag), previous_(flag) { flag_ = true; }
    ~ScopedTrueFlag() { flag_ = previous_; }
    ScopedTrueFlag(const ScopedTrueFlag&) = delete;
    ScopedTrueFlag& operator=(const ScopedTrueFlag&) = delete;

private:
    bool& flag_;
    bool previous_;
};

quint64 tagIdFromItem(const QTableWidgetItem* item) {
    if (!item) return 0;
    const QVariant idValue = item->data(kTagIdRole);
    if (!idValue.isValid()) return 0;
    return idValue.toULongLong();
}

int tagSessionIndexFromItem(const QTableWidgetItem* item) {
    if (!item) return -1;
    const QVariant indexValue = item->data(kTagSessionIndexRole);
    if (!indexValue.isValid()) return -1;
    return indexValue.toInt();
}

int rowForTagId(const QTableWidget* table, const quint64 tagId) {
    if (!table || tagId == 0) return -1;
    for (int row = 0; row < table->rowCount(); ++row) {
        if (tagIdFromItem(table->item(row, 0)) == tagId) return row;
    }
    return -1;
}

bool isTextInteractionFocusWidget(const QWidget* widget) {
    if (!widget) return false;
    if (qobject_cast<const QLineEdit*>(widget)) return true;
    if (qobject_cast<const QAbstractSpinBox*>(widget)) return true;
    if (qobject_cast<const QPlainTextEdit*>(widget)) return true;
    if (qobject_cast<const QTextEdit*>(widget)) return true;
    if (qobject_cast<const QComboBox*>(widget)) return true;
    return false;
}

bool removeWidgetFromLayoutTree(QLayout* layout, QWidget* widget) {
    if (!layout || !widget) {
        return false;
    }
    if (layout->indexOf(widget) >= 0) {
        layout->removeWidget(widget);
        return true;
    }
    for (int index = 0; index < layout->count(); ++index) {
        QLayoutItem* item = layout->itemAt(index);
        if (!item) {
            continue;
        }
        if (QLayout* childLayout = item->layout()) {
            if (removeWidgetFromLayoutTree(childLayout, widget)) {
                return true;
            }
        }
    }
    return false;
}

QString formatTimestampMs(qint64 milliseconds) {
    const qint64 clampedMilliseconds = milliseconds < 0 ? 0 : milliseconds;
    const qint64 totalSeconds = clampedMilliseconds / 1000;
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

void paintTeamCellForTag(QTableWidgetItem* teamItem, const TagSession::GameTag& tag,
                         const TagSession* session) {
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
    teamItem->setForeground(QBrush(QColor(9, 9, 11)));
}

/// Removes segments that duplicate the session team labels (embedded in follow-up strings from GameControls).
QString followUpForEventColumn(const QString& followUpEvent, const TagSession* session) {
    if (!session || followUpEvent.isEmpty()) {
        return followUpEvent;
    }
    return AppLocale::followUpPathWithoutTeamSegments(followUpEvent, session->homeTeamName(), session->awayTeamName());
}

void syncWorkModeToggleButtonSizes(QToolButton* taggingButton, QToolButton* analyzingButton,
                                   QToolButton* presentingButton) {
    if (!taggingButton || !analyzingButton || !presentingButton) return;

    int maxWidth = 0;
    for (QToolButton* button : {taggingButton, analyzingButton, presentingButton}) {
        QFont boldFont = button->font();
        boldFont.setWeight(QFont::DemiBold);
        const QFontMetrics metrics(boldFont);
        const int horizontalPadding = 24;
        maxWidth = qMax(maxWidth, metrics.horizontalAdvance(button->text()) + horizontalPadding);
    }

    for (QToolButton* button : {taggingButton, analyzingButton, presentingButton}) {
        button->setMinimumWidth(maxWidth);
        button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }
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
    flushPendingClipNote();
    detachPresentationKeyboardShortcuts();
    disconnectTagSessionSignals();
    tagSession_ = nullptr;
}

bool WorkWindow::shouldDeliverPlaybackKeyboardToVideoPlayer(const QWidget* focusWidget) const {
    if (!focusWidget) return false;
    if (focusWidget->window() != window()) return false;
    if (!isAncestorOf(focusWidget)) return false;
    if (!contentStack_ || contentStack_->currentIndex() != 1) return false;
    if (mode_ == Mode::Analyzing && notesEdit_ && focusWidget == notesEdit_) return false;
    if (isTextInteractionFocusWidget(focusWidget)) return false;
    if (!videoPlayer_ || !videoPlayer_->isMediaKeyboardControlActive()) return false;
    return true;
}

void WorkWindow::onApplicationFocusWidgetChanged(QWidget* /*oldFocus*/, QWidget* newFocus) const {
    if (!videoPlayer_ || !videoPlayer_->controlsBar()) {
        return;
    }
    const bool allowPlaybackShortcuts = shouldDeliverPlaybackKeyboardToVideoPlayer(newFocus);
    videoPlayer_->controlsBar()->setPlaybackShortcutFocusGate(allowPlaybackShortcuts);
}

void WorkWindow::refreshPlaybackShortcutFocusGate() const {
    onApplicationFocusWidgetChanged(nullptr, QApplication::focusWidget());
}

void WorkWindow::setConcatenatedVideoTempDir(std::unique_ptr<QTemporaryDir> dir) {
    concatenatedVideoTempDir_ = std::move(dir);
}

void WorkWindow::setExportDefaultDirectoryFromVideoPath(const QString& videoPath) {
    exportDefaultDirectoryPath_.clear();
    if (videoPath.trimmed().isEmpty()) return;

    const QFileInfo videoInfo(videoPath);
    exportDefaultDirectoryPath_ = videoInfo.absolutePath();
}

void WorkWindow::setPendingConcatenation(std::unique_ptr<VideoConcatenator> concatenator) {
    pendingConcatenator_ = std::move(concatenator);
}

void WorkWindow::releaseTransientResources() {
    cleanupPendingConcatenation();
    cleanupConcatenatedVideo();
    cleanupPlaybackPrepVideo();
}

void WorkWindow::cleanupConcatenatedVideo() {
    concatenatedVideoTempDir_.reset();
}

void WorkWindow::cleanupPlaybackPrepVideo() {
    playbackPrepTempDir_.reset();
}

void WorkWindow::cleanupPendingConcatenation() {
    pendingConcatenator_.reset();
}

void WorkWindow::abortVideoOpen(const QString& errorMessage) {
    if (!errorMessage.isEmpty()) {
        QMessageBox::warning(this, AppLocale::trUi("app.title"), errorMessage);
    }
    onCloseVideo();
}

void WorkWindow::applyUiStrings() const {
    if (modeTaggingBtn_) {
        modeTaggingBtn_->setText(AppLocale::trUi("mode.tagging"));
        modeTaggingBtn_->setToolTip(AppLocale::trUi("tooltip.mode_tagging"));
    }
    if (modeAnalyzingBtn_) {
        modeAnalyzingBtn_->setText(AppLocale::trUi("mode.analyzing"));
        modeAnalyzingBtn_->setToolTip(AppLocale::trUi("tooltip.mode_analyzing"));
    }
    if (modePresentingBtn_) {
        modePresentingBtn_->setText(AppLocale::trUi("mode.presenting"));
        modePresentingBtn_->setToolTip(AppLocale::trUi("tooltip.mode_presenting"));
    }
    syncWorkModeToggleButtonSizes(modeTaggingBtn_, modeAnalyzingBtn_, modePresentingBtn_);
    if (videoMenuButton_) videoMenuButton_->setToolTip(AppLocale::trUi("tooltip.video_menu"));
    if (replaceVideoAction_) replaceVideoAction_->setText(AppLocale::trUi("menu.replace_video"));
    if (closeVideoAction_) closeVideoAction_->setText(AppLocale::trUi("menu.close_video"));
    if (importXmlAction_) importXmlAction_->setText(AppLocale::trUi("menu.import_xml"));
    if (clipDurationSettingsAction_) {
        clipDurationSettingsAction_->setText(AppLocale::trUi("menu.clip_durations"));
    }
    if (tagsHeaderLabel_) tagsHeaderLabel_->setText(AppLocale::trUi("tags.header"));
    if (tagsFilterButton_) tagsFilterButton_->setText(AppLocale::trUi("tags.filter"));
    if (tagsRemoveFiltersButton_) tagsRemoveFiltersButton_->setText(AppLocale::trUi("tags.remove_filters"));
    if (undoLastTagButton_) {
        undoLastTagButton_->setText(AppLocale::trUi("tags.undo"));
        undoLastTagButton_->setToolTip(AppLocale::trUi("tags.undo_tooltip"));
    }
    if (notesEdit_) notesEdit_->setPlaceholderText(AppLocale::trUi("tags.note_placeholder"));
    if (matchNotesLabel_) matchNotesLabel_->setText(AppLocale::trUi("notes.match_title"));
    if (clipNotesLabel_) clipNotesLabel_->setText(AppLocale::trUi("notes.clip_title"));
    if (matchNotesEditor_) matchNotesEditor_->applyUiStrings();
    if (tagsTable_) {
        tagsTable_->setHorizontalHeaderLabels({AppLocale::trUi("tags.col_time"), AppLocale::trUi("tags.col_team"),
                                               AppLocale::trUi("tags.col_event")});
    }
    if (statsOverlayAction_) statsOverlayAction_->setToolTip(AppLocale::trUi("stats_overlay.tooltip"));
    if (statsOverlayDialog_) statsOverlayDialog_->setWindowTitle(AppLocale::trUi("stats.overlay_title"));
    if (gameSetupWidget_) gameSetupWidget_->applyUiStrings();
    if (statsWindow_) statsWindow_->applyUiStrings();
    if (exportJobsBar_) exportJobsBar_->applyUiStrings();
}

void WorkWindow::onApplicationLanguageChanged() {
    applyUiStrings();
    if (gameControls_) gameControls_->applyUiLanguage();
    if (statsOverlay_) statsOverlay_->applyUiStrings();
    if (videoPlayer_ && videoPlayer_->controlsBar()) videoPlayer_->controlsBar()->applyUiStrings();
    if (presentationPanel_) presentationPanel_->applyUiStrings();
    updatePresentationStage();
    rebuildFilterMenu();
    rebuildTagsList();
    updateFilterIndicator();
}

void WorkWindow::disconnectTagSessionSignals() {
    for (const QMetaObject::Connection& connection : tagSessionConnections_) {
        QObject::disconnect(connection);
    }
    tagSessionConnections_.clear();
}

void WorkWindow::syncContextPeriodFromSession() {
    if (!tagSession_) {
        contextPeriod_.clear();
        return;
    }
    // GameEnded leaves currentQuarterIndex_ at -1 (no quarter in progress). New tags
    // after the whistle still belong to Q4, matching periodLabelAtTimestampMs().
    if (tagSession_->quarterPhase() == TagSession::QuarterPhase::GameEnded) {
        contextPeriod_ = EventDefaults::quarterCode(3);
        return;
    }
    const int currentQuarterIndex = tagSession_->currentQuarterIndex();
    if (currentQuarterIndex >= 0) {
        contextPeriod_ = EventDefaults::quarterCode(currentQuarterIndex);
        return;
    }
    contextPeriod_.clear();
}

void WorkWindow::setTagSession(TagSession* session) {
    if (tagSession_ == session) return;

    flushPendingClipNote();
    disconnectTagSessionSignals();

    tagSession_ = session;
    if (statsWindow_) statsWindow_->setTagSession(tagSession_);
    if (statsOverlay_) statsOverlay_->setTagSession(tagSession_);
    if (presentationQueue_) presentationQueue_->setTagSession(tagSession_);
    if (presentationPanel_) presentationPanel_->setTagSession(tagSession_);

    rebuildFilterMenu();
    rebuildTagsList();
    loadMatchNote();

    if (!tagSession_) {
        if (gameControls_) {
            gameControls_->setSessionTeamNames(QString(), QString(), QString(), QString());
        }
        return;
    }

    if (gameControls_) {
        gameControls_->setSessionTeamNames(tagSession_->homeTeamName(), tagSession_->awayTeamName(),
                                           tagSession_->homeTeamColor(), tagSession_->awayTeamColor());
        gameControls_->setInitialTeamSide(true);
    }

    tagSessionConnections_.append(connect(tagSession_, &TagSession::cleared, this, [this]() {
        if (!tagSession_) return;
        discardPendingClipNote();
        rebuildFilterMenu();
        rebuildTagsList();
    }));

    tagSessionConnections_.append(connect(tagSession_, &TagSession::tagsChanged, this, [this]() {
        if (!tagSession_) return;
        rebuildFilterMenu();
        rebuildTagsList();
        if (gameControls_) {
            gameControls_->restoreGamePhase(tagSession_->quarterPhase(),
                                            tagSession_->currentQuarterIndex());
        }
        syncContextPeriodFromSession();
    }));

    tagSessionConnections_.append(connect(tagSession_, &TagSession::tagAdded, this, [this](const TagSession::GameTag&) {
        if (!tagSession_) return;
        rebuildFilterMenu();
        rebuildTagsList();
        flashNewTagRow();
    }));
    tagSessionConnections_.append(connect(tagSession_, &TagSession::tagsImported, this, [this]() {
        if (!tagSession_) return;
        rebuildFilterMenu();
        rebuildTagsList();
        if (gameControls_) {
            gameControls_->restoreGamePhase(tagSession_->quarterPhase(),
                                            tagSession_->currentQuarterIndex());
        }
        syncContextPeriodFromSession();
    }));
    tagSessionConnections_.append(connect(tagSession_, &TagSession::tagNoteChanged, this, [this](int changedIndex) {
        if (suppressClipNoteReload_ || !notesEdit_ || !tagSession_) return;
        if (pendingNoteTagId_ != 0) return;
        if (!tagSession_->isValidTagIndex(changedIndex)) return;
        const quint64 changedTagId = tagSession_->tags().at(changedIndex).id;
        if (selectedTagId() != changedTagId) return;
        const QString noteText = tagSession_->tagNote(changedIndex);
        if (notesEdit_->toPlainText() == noteText) return;
        loadNoteForSelectedTag();
    }));
    tagSessionConnections_.append(connect(tagSession_, &TagSession::matchNoteChanged, this, [this]() {
        if (!tagSession_) return;
        if (matchNotesEditor_ && matchNotesEditor_->hasFocus()) return;
        loadMatchNote();
    }));
    tagSessionConnections_.append(connect(tagSession_, &TagSession::gameMetadataChanged, this, [this]() {
        if (!tagSession_) return;
        if (gameControls_) {
            gameControls_->setSessionTeamNames(tagSession_->homeTeamName(), tagSession_->awayTeamName(),
                                               tagSession_->homeTeamColor(), tagSession_->awayTeamColor());
        }
        rebuildTagsList();
        refreshMatchNoteMentionCandidates();
    }));
}

void WorkWindow::setMode(Mode m) {
    if (m == Mode::Presenting && !LicenseManager::instance().isEntitled()) {
        return;
    }
    if (mode_ == m) return;
    flushPendingClipNote();
    if (mode_ == Mode::Tagging && m != Mode::Tagging) {
        captureTaggingModeUiStateForRestore();
    }
    if (mode_ == Mode::Presenting && m != Mode::Presenting) {
        detachPresentationKeyboardShortcuts();
        presentationAutoPauseArmed_ = false;
    }
    mode_ = m;
    switch (m) {
        case Mode::Tagging: applyTaggingLayout(); break;
        case Mode::Analyzing: applyAnalyzingLayout(); break;
        case Mode::Presenting: applyPresentationLayout(); break;
    }
    if (modeTaggingBtn_) modeTaggingBtn_->setChecked(m == Mode::Tagging);
    if (modeAnalyzingBtn_) modeAnalyzingBtn_->setChecked(m == Mode::Analyzing);
    if (modePresentingBtn_) modePresentingBtn_->setChecked(m == Mode::Presenting);
    refreshPlaybackShortcutFocusGate();
}

TagSession::GameTag WorkWindow::pendingTagPeriodAndTeam() const {
    TagSession::GameTag pendingTag;
    pendingTag.period = contextPeriod_;
    pendingTag.team = contextTeam_;
    return pendingTag;
}

void WorkWindow::buildUi() {
    setObjectName("AppRoot");
    detachedWidgetHost_ = new QWidget(this);
    detachedWidgetHost_->hide();

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

    modePresentingBtn_ = new QToolButton(topRow);
    modePresentingBtn_->setObjectName(QStringLiteral("WorkModePresentingButton"));
    modePresentingBtn_->setCheckable(true);
    modePresentingBtn_->setChecked(false);
    Style::setVariant(modePresentingBtn_, "ghost");
    Style::setSize(modePresentingBtn_, "sm");
    modePresentingBtn_->setCursor(Qt::PointingHandCursor);

    auto* modeToggleGroup = new QWidget(topRow);
    modeToggleGroup->setObjectName(QStringLiteral("WorkModeToggleGroup"));
    auto* modeToggleLayout = new QHBoxLayout(modeToggleGroup);
    modeToggleLayout->setContentsMargins(3, 3, 3, 3);
    modeToggleLayout->setSpacing(4);
    modeToggleLayout->addWidget(modeTaggingBtn_);
    modeToggleLayout->addWidget(modeAnalyzingBtn_);
    modeToggleLayout->addWidget(modePresentingBtn_);
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
    closeVideoAction_ = videoMenu_->addAction(QString());
    videoMenu_->addSeparator();
    importXmlAction_ = videoMenu_->addAction(QString());
    clipDurationSettingsAction_ = videoMenu_->addAction(QString());
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

    exportJobManager_ = new ExportJobManager(this);
    exportJobsBar_ = new ExportJobsBar(exportJobManager_, this);
    layout->addWidget(exportJobsBar_, 0);

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
    statsWindow_ = new StatsWindow(this);
    statsWindow_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    statsWindow_->setMinimumHeight(180);

    notesColumn_ = new QWidget(this);
    notesColumn_->setObjectName(QStringLiteral("MatchNotesColumn"));
    auto* notesColumnLayout = new QVBoxLayout(notesColumn_);
    notesColumnLayout->setContentsMargins(0, 0, 0, 0);
    notesColumnLayout->setSpacing(6);

    matchNotesLabel_ = new QLabel(notesColumn_);
    Style::setRole(matchNotesLabel_, "muted");
    matchNotesEditor_ = new MatchNotesEditor(notesColumn_);
    matchNotesEditor_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    clipNotesLabel_ = new QLabel(notesColumn_);
    Style::setRole(clipNotesLabel_, "muted");
    notesEdit_ = new QPlainTextEdit(notesColumn_);
    notesEdit_->setMaximumHeight(88);
    notesEdit_->setMinimumHeight(64);
    notesEdit_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    Style::setRole(notesEdit_, "muted");

    notesColumnLayout->addWidget(matchNotesLabel_);
    notesColumnLayout->addWidget(matchNotesEditor_, 1);
    notesColumnLayout->addWidget(clipNotesLabel_);
    notesColumnLayout->addWidget(notesEdit_, 0);
    notesColumn_->hide();

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

    buildPresentationUi();

    if (videoPlayer_) videoPlayer_->setControlsVisible(false);
    if (gameControls_) gameControls_->hide();
    if (statsWindow_) statsWindow_->hide();
    if (tagsHeaderRow_) tagsHeaderRow_->hide();
    if (tagsTable_) tagsTable_->hide();
    if (modeTaggingBtn_) modeTaggingBtn_->hide();
    if (modeAnalyzingBtn_) modeAnalyzingBtn_->hide();
    if (modePresentingBtn_) modePresentingBtn_->hide();
}

void WorkWindow::buildPresentationUi() {
    presentationQueue_ = new PresentationQueue(this);

    // Stage column: event banner on top, the (large) video in the middle, clip bar underneath.
    presentationStageColumn_ = new QWidget(this);
    presentationStageColumn_->setObjectName(QStringLiteral("PresentationStageColumn"));
    presentationStageColumn_->setAttribute(Qt::WA_StyledBackground, true);
    presentationStageColumn_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* stageLayout = new QVBoxLayout(presentationStageColumn_);
    stageLayout->setContentsMargins(0, 0, 0, 0);
    stageLayout->setSpacing(8);

    presentationBanner_ = new QWidget(presentationStageColumn_);
    presentationBanner_->setObjectName(QStringLiteral("PresentationBanner"));
    presentationBanner_->setAttribute(Qt::WA_StyledBackground, true);
    auto* bannerLayout = new QVBoxLayout(presentationBanner_);
    bannerLayout->setContentsMargins(12, 6, 12, 6);
    bannerLayout->setSpacing(1);

    presentationEventLabel_ = new QLabel(presentationBanner_);
    presentationEventLabel_->setObjectName(QStringLiteral("PresentationEventLabel"));
    presentationEventLabel_->setWordWrap(true);
    bannerLayout->addWidget(presentationEventLabel_);

    presentationContextLabel_ = new QLabel(presentationBanner_);
    presentationContextLabel_->setObjectName(QStringLiteral("PresentationContextLabel"));
    presentationContextLabel_->setWordWrap(true);
    bannerLayout->addWidget(presentationContextLabel_);

    presentationNoteLabel_ = new QLabel(presentationBanner_);
    presentationNoteLabel_->setObjectName(QStringLiteral("PresentationNoteLabel"));
    presentationNoteLabel_->setWordWrap(true);
    presentationNoteLabel_->hide();
    bannerLayout->addWidget(presentationNoteLabel_);

    stageLayout->addWidget(presentationBanner_, 0);

    presentationClipBar_ = new ClipTrimBar(presentationStageColumn_);
    stageLayout->addWidget(presentationClipBar_, 0);

    presentationPanel_ = new PresentationPanel(this);
    presentationPanel_->setMinimumWidth(260);
    presentationPanel_->setExportEnabled(false);

    presentationSplitter_ = new QSplitter(Qt::Horizontal, this);
    presentationSplitter_->setObjectName(QStringLiteral("PresentationSplitter"));
    // Collapsible on purpose: dragging the handle shut gives the video the whole window.
    presentationSplitter_->setChildrenCollapsible(true);
    presentationSplitter_->setHandleWidth(6);
    presentationSplitter_->setAttribute(Qt::WA_StyledBackground, true);
    presentationSplitter_->addWidget(presentationStageColumn_);
    presentationSplitter_->addWidget(presentationPanel_);
    presentationSplitter_->setStretchFactor(0, 4);
    presentationSplitter_->setStretchFactor(1, 1);
    presentationSplitter_->hide();

    connect(presentationPanel_, &PresentationPanel::selectedTagIndexesChanged, this,
            &WorkWindow::onPresentationSelectionChanged);
    connect(presentationPanel_, &PresentationPanel::clipActivated, this,
            &WorkWindow::onPresentationClipActivated);
    connect(presentationPanel_, &PresentationPanel::currentClipLeadLagEdited, this,
            &WorkWindow::onPresentationLeadLagEdited);
    connect(presentationPanel_, &PresentationPanel::applyLeadLagToAllRequested, this,
            &WorkWindow::onPresentationApplyLeadLagToAll);
    connect(presentationPanel_, &PresentationPanel::showNotesToggled, this,
            &WorkWindow::onPresentationShowNotesToggled);
    connect(presentationPanel_, &PresentationPanel::exportRequested, this,
            &WorkWindow::onPresentationExportRequested);

    connect(presentationQueue_, &PresentationQueue::queueChanged, this,
            &WorkWindow::onPresentationQueueChanged);
    connect(presentationQueue_, &PresentationQueue::currentClipChanged, this,
            &WorkWindow::onPresentationCurrentClipChanged);
    connect(presentationQueue_, &PresentationQueue::clipIntervalChanged, this,
            &WorkWindow::onPresentationClipIntervalChanged);

    connect(presentationClipBar_, &ClipTrimBar::seekRequested, this, [this](qint64 positionMs) {
        if (videoPlayer_) videoPlayer_->seekToMs(positionMs);
    });
    connect(presentationClipBar_, &ClipTrimBar::clipStartChanged, this,
            [this](qint64) { savePresentationClipIntervalFromClipBar(); });
    connect(presentationClipBar_, &ClipTrimBar::clipEndChanged, this,
            [this](qint64) { savePresentationClipIntervalFromClipBar(); });
}

void WorkWindow::detachWidgetFromParent(QWidget* widget) {
    if (!widget || !detachedWidgetHost_ || widget == detachedWidgetHost_) {
        return;
    }

    QWidget* parent = widget->parentWidget();
    if (!parent || parent == detachedWidgetHost_) {
        return;
    }

    if (qobject_cast<QSplitter*>(parent)) {
        widget->setParent(detachedWidgetHost_);
        return;
    }

    if (QLayout* layout = parent->layout()) {
        removeWidgetFromLayoutTree(layout, widget);
    }
    widget->setParent(detachedWidgetHost_);
}

void WorkWindow::applyTaggingLayout() {
    mode_ = Mode::Tagging;
    if (analyzingMainSplitter_) analyzingMainSplitter_->hide();
    if (presentationSplitter_) presentationSplitter_->hide();

    QWidget* timeline = videoPlayer_ ? videoPlayer_->timelineBar() : nullptr;
    detachWidgetFromParent(videoPlayer_ ? videoPlayer_->videoWidget() : nullptr);
    if (timeline && timeline->parentWidget() != videoTimelineRow_) {
        detachWidgetFromParent(timeline);
    }
    detachWidgetFromParent(tagsSection_);
    detachWidgetFromParent(gameControls_);
    detachWidgetFromParent(analyzingTagsControlsSplitter_);
    detachWidgetFromParent(statsWindow_);
    if (notesColumn_) detachWidgetFromParent(notesColumn_);

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

    QWidget* vw = videoPlayer_->videoWidget();
    static_cast<QBoxLayout*>(taggingVideoCol_->layout())->addWidget(vw, 1);
    auto* rightLayout = static_cast<QBoxLayout*>(taggingRightCol_->layout());
    if (gameControls_) {
        // Analyzing mode lowers minimum width; restore so tagging labels are not clipped.
        gameControls_->setMinimumWidth(GameControls::kMinimumPanelWidthPx);
        gameControls_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    }
    rightLayout->addWidget(gameControls_, 1);

    while (QLayoutItem* item = contentLayout_->takeAt(0)) {
        delete item;  // widget stays in tree; do not setParent(nullptr)
    }

    if (taggingVideoTagsSplitter_) {
        while (taggingVideoTagsSplitter_->count() > 0) {
            detachWidgetFromParent(taggingVideoTagsSplitter_->widget(0));
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
    // Presentation mode hides these outright; re-show them after their layout slot is restored.
    if (tagsSection_) tagsSection_->show();

    const int rh = qMax(20, tagsTable_->fontMetrics().height() + 4);
    const int headerH = tagsTable_->horizontalHeader()->sizeHint().height();
    tagsTable_->setMinimumHeight(rh * 2 + headerH);
    tagsTable_->setMaximumHeight(QWIDGETSIZE_MAX);
    if (tagsSection_) tagsSection_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    statsWindow_->hide();
    if (notesColumn_) notesColumn_->hide();

    if (gameControls_) gameControls_->show();

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
    if (presentationSplitter_) presentationSplitter_->hide();
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
    detachWidgetFromParent(statsWindow_);
    if (notesColumn_) detachWidgetFromParent(notesColumn_);
    detachWidgetFromParent(timeline);
    if (videoTimelineRow_) videoTimelineRow_->hide();

    while (analyzingTagsControlsSplitter_->count() > 0) {
        detachWidgetFromParent(analyzingTagsControlsSplitter_->widget(0));
    }
    while (analyzingLeftSplitter_->count() > 0) {
        detachWidgetFromParent(analyzingLeftSplitter_->widget(0));
    }
    while (analyzingRightSplitter_->count() > 0) {
        detachWidgetFromParent(analyzingRightSplitter_->widget(0));
    }

    timeline->setMinimumHeight(44);
    timeline->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tagsSection_->setMinimumWidth(160);
    tagsSection_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    if (notesColumn_) {
        notesColumn_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    analyzingTagsControlsSplitter_->addWidget(tagsSection_);
    if (notesColumn_) analyzingTagsControlsSplitter_->addWidget(notesColumn_);

    analyzingLeftSplitter_->addWidget(vw);
    analyzingLeftSplitter_->addWidget(timeline);
    analyzingLeftSplitter_->addWidget(analyzingTagsControlsSplitter_);

    analyzingRightSplitter_->addWidget(statsWindow_);

    analyzingMainSplitter_->setStretchFactor(0, 2);
    analyzingMainSplitter_->setStretchFactor(1, 1);
    analyzingLeftSplitter_->setStretchFactor(0, 5);
    analyzingLeftSplitter_->setStretchFactor(1, 0);
    analyzingLeftSplitter_->setStretchFactor(2, 3);
    analyzingTagsControlsSplitter_->setStretchFactor(0, 1);
    analyzingTagsControlsSplitter_->setStretchFactor(1, 1);
    analyzingRightSplitter_->setStretchFactor(0, 1);

    contentLayout_->addWidget(analyzingMainSplitter_, 1);

    statsWindow_->show();
    if (notesColumn_) notesColumn_->show();
    // Presentation mode hides these outright; re-show them after their layout slot is restored.
    if (tagsSection_) tagsSection_->show();
    if (gameControls_) gameControls_->hide();
    tagsTable_->setMaximumHeight(QWIDGETSIZE_MAX);

    if (tagsHeaderRow_) tagsHeaderRow_->show();

    if (analyzingMainSplitter_) analyzingMainSplitter_->show();

    QTimer::singleShot(0, this, [this]() { applyAnalyzingSplitterGeometry(); });

    // Keep tag list in sync with session after layout change
    rebuildTagsList();
}

void WorkWindow::applyPresentationLayout() {
    mode_ = Mode::Presenting;
    if (taggingMainRow_) taggingMainRow_->hide();
    if (taggingVideoTagsSplitter_) taggingVideoTagsSplitter_->hide();
    if (analyzingMainSplitter_) analyzingMainSplitter_->hide();
    QWidget* videoWidget = videoPlayer_ ? videoPlayer_->videoWidget() : nullptr;
    QWidget* timeline = videoPlayer_ ? videoPlayer_->timelineBar() : nullptr;
    if (!videoWidget || !presentationSplitter_ || !presentationStageColumn_) {
        return;
    }

    detachWidgetFromParent(videoWidget);
    detachWidgetFromParent(tagsSection_);
    detachWidgetFromParent(gameControls_);
    detachWidgetFromParent(statsWindow_);
    if (notesColumn_) detachWidgetFromParent(notesColumn_);
    detachWidgetFromParent(analyzingTagsControlsSplitter_);
    if (timeline && timeline->parentWidget() != videoTimelineRow_) {
        detachWidgetFromParent(timeline);
    }

    while (QLayoutItem* item = contentLayout_->takeAt(0)) {
        delete item;  // widget stays in tree; do not setParent(nullptr)
    }

    // The full-video timeline stays available so the presenter can roam outside the clip window.
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

    // Banner sits at index 0 and the clip bar last, so the video always lands between them.
    auto* stageLayout = static_cast<QBoxLayout*>(presentationStageColumn_->layout());
    stageLayout->insertWidget(1, videoWidget, 1);

    // These panels have no slot in the presentation layout; without an explicit hide they would
    // paint at their last geometry on top of the stage.
    if (tagsSection_) tagsSection_->hide();
    if (gameControls_) gameControls_->hide();
    if (statsWindow_) statsWindow_->hide();
    if (notesColumn_) notesColumn_->hide();

    contentLayout_->addWidget(presentationSplitter_, 1);
    presentationSplitter_->show();
    presentationStageColumn_->show();
    if (presentationPanel_) presentationPanel_->show();

    if (presentationQueue_ && videoPlayer_) {
        presentationQueue_->setVideoDurationMs(videoPlayer_->durationMs());
    }
    if (presentationPanel_) presentationPanel_->refreshFromSession();
    updatePresentationStage();
    configurePresentationClipBarForCurrentClip();
    attachPresentationKeyboardShortcuts();

    QTimer::singleShot(0, this, [this]() { applyPresentationSplitterGeometry(); });
}

void WorkWindow::applyPresentationSplitterGeometry() {
    if (mode_ != Mode::Presenting || !presentationSplitter_) return;
    const int totalWidth = presentationSplitter_->width();
    if (totalWidth < 160) return;

    const int panelWidth = std::clamp(totalWidth / 4, 260, 420);
    const int stageWidth = qMax(320, totalWidth - panelWidth);
    presentationSplitter_->setSizes({stageWidth, panelWidth});
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

    const int tagsNotesRowW = analyzingTagsControlsSplitter_ ? analyzingTagsControlsSplitter_->width() : 0;
    if (analyzingTagsControlsSplitter_ && tagsNotesRowW >= 120) {
        const int tagsW = qMax(200, tagsNotesRowW / 2);
        const int notesW = qMax(200, tagsNotesRowW - tagsW);
        analyzingTagsControlsSplitter_->setSizes({tagsW, notesW});
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
        connect(gameSetupWidget_, &GameSetupWindow::gameSetupConfirmed, this, &WorkWindow::onGameSetupConfirmed);
        connect(gameSetupWidget_, &GameSetupWindow::cancelled, this, &WorkWindow::onGameSetupCancelled);
    }
    // Video file management
    connect(replaceVideoAction_, &QAction::triggered, this, &WorkWindow::onReplaceVideo);
    connect(closeVideoAction_, &QAction::triggered, this, &WorkWindow::onCloseVideo);
    
    // Connect VideoPlayer's videoClosed signal to WorkWindow's signal
    connect(videoPlayer_, &VideoPlayer::videoClosed, this, &WorkWindow::videoClosed);

    connect(importXmlAction_, &QAction::triggered, this, &WorkWindow::onImportXml);
    connect(clipDurationSettingsAction_, &QAction::triggered, this,
            &WorkWindow::onClipDurationSettings);

    // GameControls -> capture timestamp and store tags
    connect(gameControls_, &GameControls::mainEventTimestampCaptured, this, [this](const QString& mainEvent) {
        if (!LicenseManager::instance().isEntitled()) return;
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

    connect(gameControls_, &GameControls::tagCommitted, this, [this](const QString& mainEvent, const QString& followUpEvent) {
        if (!LicenseManager::instance().isEntitled()) return;
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
            tag.markMs = timestampMs;
            TagSession::GameTag pendingPeriodAndTeam = pendingTagPeriodAndTeam();
            tag.period = pendingPeriodAndTeam.period;
            if (gameControls_) {
                const QString sideKey = gameControls_->selectedTeamSideKey();
                tag.team = sideKey.isEmpty() ? pendingPeriodAndTeam.team : sideKey;
                if (!sideKey.isEmpty()) {
                    contextTeam_ = sideKey;
                }
            } else {
                tag.team = pendingPeriodAndTeam.team;
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
    connect(modePresentingBtn_, &QToolButton::clicked, this, &WorkWindow::onModeToggled);

    if (notesEdit_) {
        notesEdit_->installEventFilter(this);
        connect(notesEdit_, &QPlainTextEdit::textChanged, this, &WorkWindow::onNoteTextChanged);
    }
    if (matchNotesEditor_) {
        connect(matchNotesEditor_, &MatchNotesEditor::textChanged, this,
                &WorkWindow::onMatchNoteTextChanged);
        connect(matchNotesEditor_, &MatchNotesEditor::tagMentionActivated, this,
                &WorkWindow::onMatchNoteTagMentionActivated);
    }

    connect(statsWindow_, &StatsWindow::filterByEventPathRequested, this, &WorkWindow::onFilterByEventPathRequested);
    connect(tagsRemoveFiltersButton_, &QToolButton::clicked, this, &WorkWindow::onRemoveFilters);
    connect(undoLastTagButton_, &QToolButton::clicked, this, &WorkWindow::onUndoLastTag);

    auto* modeAction = new QAction(this);
    modeAction->setShortcut(QKeySequence(Qt::Key_M));
    modeAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(modeAction, &QAction::triggered, this, [this]() {
        Mode next = nextModeInCycle(mode_);
        if (next == Mode::Presenting && !LicenseManager::instance().isEntitled()) {
            next = nextModeInCycle(next);
        }
        setMode(next);
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

    matchNoteDebounceTimer_ = new QTimer(this);
    matchNoteDebounceTimer_->setSingleShot(true);
    connect(matchNoteDebounceTimer_, &QTimer::timeout, this, &WorkWindow::saveMatchNoteDebounceFired);

    // Debounce playhead-driven table scans: row highlighting is O(rows).
    playheadSideEffectsDebounceTimer_ = new QTimer(this);
    playheadSideEffectsDebounceTimer_->setSingleShot(true);
    playheadSideEffectsDebounceTimer_->setInterval(200);
    connect(playheadSideEffectsDebounceTimer_, &QTimer::timeout, this, [this]() {
        onPlayheadPositionChanged(lastPlayheadPositionForSideEffectsMs_);
    });
    connect(videoPlayer_, &VideoPlayer::positionChangedMs, this, [this](qint64 positionMs) {
        lastPlayheadPositionForSideEffectsMs_ = positionMs;
        playheadSideEffectsDebounceTimer_->start();
        // Presentation playhead and clip-end stop must not be debounced.
        updatePresentationPlayhead(positionMs);
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


void WorkWindow::showTeamSetupForVideo(const QString& filePath, const QStringList& sourceVideoPaths) {
    if (!gameSetupWidget_ || !contentStack_) return;
    gameSetupWidget_->setVideoPath(filePath);
    gameSetupWidget_->setTeamDefaults(QString(), QString(), QString(), QString());
    gameSetupWidget_->setMetadataDefaults(QDate::currentDate(), QString(), QString());
    gameSetupWidget_->beginMetadataSuggestion(sourceVideoPaths);
    contentStack_->setCurrentIndex(0);
    gameSetupWidget_->setInitialFocus();
    refreshPlaybackShortcutFocusGate();
}

void WorkWindow::onGameSetupConfirmed(const QString& filePath,
                                       const QString& homeName, const QString& awayName,
                                       const QString& homeColor, const QString& awayColor,
                                       const QString& competitionName,
                                       const QDate& gameDate,
                                       const QString& homeAbbrev,
                                       const QString& awayAbbrev) {
    if (pendingConcatenator_) {
        const bool concatOk = pendingConcatenator_->waitWithProgress(this);
        const QString errorMessage = pendingConcatenator_->errorMessage();
        if (!concatOk) {
            abortVideoOpen(errorMessage);
            return;
        }
        cleanupPendingConcatenation();
    }

    if (tagSession_) {
        tagSession_->setGameTeams(homeName, awayName, homeColor, awayColor);
        tagSession_->setGameMetadata(competitionName, gameDate, homeAbbrev, awayAbbrev);
    }
    if (contentStack_) contentStack_->setCurrentIndex(1);
    loadVideoFromFile(filePath);
}

void WorkWindow::onGameStartRequested() {
    if (!LicenseManager::instance().isEntitled()) return;
    if (!videoPlayer_ || !tagSession_) return;
    const qint64 anchorMs = videoPlayer_->currentPositionMs();
    tagSession_->setGameStartAnchor(anchorMs);
    tagSession_->setCurrentQuarter(0, anchorMs);
    contextPeriod_ = QStringLiteral("Q1");

    // Insert the start-anchor instance: a 2-second window starting at the anchor moment.
    TagSession::GameTag anchorTag;
    anchorTag.mainEvent = QString::fromLatin1(EventDefaults::TimeCodes::kStartAnchor);
    anchorTag.markMs = anchorMs;
    anchorTag.startMs = anchorMs;
    anchorTag.endMs = anchorMs + 2000;
    anchorTag.period = QStringLiteral("Q1");
    anchorTag.intervalManuallyEdited = true; // anchor span is fixed; default-table changes must not move it
    tagSession_->addTag(anchorTag);
}

void WorkWindow::onNextQuarterRequested() {
    if (!LicenseManager::instance().isEntitled()) return;
    if (!videoPlayer_ || !tagSession_) return;
    if (tagSession_->currentQuarterIndex() < 0) return;

    const int closingIndex = tagSession_->currentQuarterIndex();
    const qint64 quarterStartMs = tagSession_->currentQuarterStartMs();
    const qint64 nowMs = videoPlayer_->currentPositionMs();
    const qint64 endMs = nowMs >= quarterStartMs ? nowMs : quarterStartMs;

    if (closingIndex < 0 || closingIndex > 3) return;

    TagSession::GameTag quarterTag;
    quarterTag.mainEvent = EventDefaults::quarterCode(closingIndex);
    quarterTag.markMs = quarterStartMs;
    quarterTag.startMs = quarterStartMs;
    quarterTag.endMs = endMs;
    quarterTag.period = quarterTag.mainEvent;
    quarterTag.intervalManuallyEdited = true; // quarter span is anchored to user clicks
    tagSession_->addTag(quarterTag);

    const int nextIndex = closingIndex + 1;
    if (nextIndex >= 4) {
        tagSession_->clearCurrentQuarter();
        tagSession_->setQuarterPhase(TagSession::QuarterPhase::GameEnded);
        syncContextPeriodFromSession();
    } else {
        tagSession_->setCurrentQuarter(nextIndex, nowMs);
        contextPeriod_ = EventDefaults::quarterCode(nextIndex);
    }
}

void WorkWindow::onGameSetupCancelled() {
    onCloseVideo();
}

void WorkWindow::loadVideoFromFile(const QString& filePath) {
    if (filePath.isEmpty()) return;

    QString playbackPath = filePath;
    cleanupPlaybackPrepVideo();

    if (PlaybackVideoPreparer::requiresTranscodeForPlayback(filePath)) {
        auto tempDir = std::make_unique<QTemporaryDir>();
        if (!tempDir->isValid()) {
            abortVideoOpen(AppLocale::trUi("playback_prep.error_failed"));
            return;
        }

        auto preparer = std::make_unique<PlaybackVideoPreparer>();
        preparer->startPreparation(filePath, tempDir->path());
        const bool prepOk = preparer->waitWithProgress(this);
        const QString errorMessage = preparer->errorMessage();
        const QString preparedPath = preparer->outputPath();
        preparer.reset();

        if (!prepOk) {
            abortVideoOpen(errorMessage);
            return;
        }

        playbackPrepTempDir_ = std::move(tempDir);
        playbackPath = preparedPath;
    }

    sourceVideoPath_ = filePath;
    playbackVideoPath_ = playbackPath;
    hasPreservedTaggingUiState_ = false;
    preservedTaggingVideoTagsSplitterSizes_.clear();

    discardPendingClipNote();
    if (tagSession_) tagSession_->clear();
    hasPendingTag_ = false;
    pendingMainEvent_.clear();
    pendingTimestampMs_ = 0;
    contextPeriod_.clear();
    if (tagsTable_) tagsTable_->setRowCount(0);

    if (videoPlayer_) {
        videoPlayer_->loadVideoFromFile(playbackPath);
        videoPlayer_->setControlsVisible(true);
    }

    if (gameControls_) {
        gameControls_->resetGameTimeState();
        gameControls_->show();
        if (tagSession_) {
            gameControls_->setSessionTeamNames(tagSession_->homeTeamName(), tagSession_->awayTeamName(),
                                               tagSession_->homeTeamColor(), tagSession_->awayTeamColor());
            gameControls_->setInitialTeamSide(true);
        }
        contextTeam_ = "Home";
    }
    if (modeTaggingBtn_) modeTaggingBtn_->show();
    if (modeAnalyzingBtn_) modeAnalyzingBtn_->show();
    if (modePresentingBtn_) modePresentingBtn_->show();

    if (presentationQueue_) {
        presentationQueue_->clear();
        presentationQueue_->setVideoDurationMs(videoPlayer_ ? videoPlayer_->durationMs() : 0);
    }
    presentationPlaybackStarted_ = false;
    presentationAutoPauseArmed_ = false;
    lastPresentationPlayheadMs_ = -1;
    if (presentationPanel_) {
        presentationPanel_->refreshFromSession();
        presentationPanel_->setExportEnabled(true);
    }
    updatePresentationStage();

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
        releaseTransientResources();
        setExportDefaultDirectoryFromVideoPath(filePaths.first());
        loadVideoFromFile(filePaths.first());
        return;
    }

    filePaths.sort(Qt::CaseInsensitive);
    if (!VideoConcatenator::showFileOrderDialog(filePaths, this)) return;
    setExportDefaultDirectoryFromVideoPath(filePaths.first());

    auto tempDir = std::make_unique<QTemporaryDir>();
    if (!tempDir->isValid()) {
        QMessageBox::warning(this,
                             AppLocale::trUi("app.title"),
                             AppLocale::trUi("concat.error_failed"));
        return;
    }

    VideoConcatenator concatenator;
    const VideoConcatenationResult result =
        concatenator.runWithProgress(filePaths, tempDir->path(), this);
    if (!result.succeeded) {
        tempDir.reset();
        if (!result.errorMessage.isEmpty()) {
            QMessageBox::warning(this, AppLocale::trUi("app.title"), result.errorMessage);
        }
        return;
    }

    const QString outputPath = result.outputPath;

    releaseTransientResources();
    concatenatedVideoTempDir_ = std::move(tempDir);
    loadVideoFromFile(outputPath);
}

void WorkWindow::onCloseVideo() {
    hasPreservedTaggingUiState_ = false;
    preservedTaggingVideoTagsSplitterSizes_.clear();

    if (videoPlayer_) videoPlayer_->setControlsVisible(false);
    if (gameControls_) {
        gameControls_->resetGameTimeState();
        gameControls_->hide();
    }
    if (modeTaggingBtn_) modeTaggingBtn_->hide();
    if (modeAnalyzingBtn_) modeAnalyzingBtn_->hide();
    if (modePresentingBtn_) modePresentingBtn_->hide();
    if (presentationQueue_) presentationQueue_->clear();
    if (presentationPanel_) presentationPanel_->setExportEnabled(false);
    presentationAutoPauseArmed_ = false;
    presentationPlaybackStarted_ = false;
    lastPresentationPlayheadMs_ = -1;

    discardPendingClipNote();
    if (tagSession_) tagSession_->clear();
    hasPendingTag_ = false;
    pendingMainEvent_.clear();
    pendingTimestampMs_ = 0;
    contextPeriod_.clear();
    if (tagsTable_) tagsTable_->setRowCount(0);
    if (tagsHeaderRow_) tagsHeaderRow_->hide();
    if (tagsTable_) tagsTable_->hide();
    if (statsWindow_) statsWindow_->hide();

    releaseTransientResources();
    exportDefaultDirectoryPath_.clear();
    emit videoClosed();
    refreshPlaybackShortcutFocusGate();
}

void WorkWindow::onPresentationExportRequested() {
    if (!LicenseManager::instance().isEntitled()) return;
    if (!tagSession_ || sourceVideoPath_.isEmpty() || !exportJobManager_) return;

    QVector<PresentationQueue::Clip> queuedClips;
    if (presentationQueue_) {
        queuedClips.reserve(presentationQueue_->count());
        for (int index = 0; index < presentationQueue_->count(); ++index) {
            queuedClips.append(presentationQueue_->clipAt(index));
        }
    }

    ExportSettingsDialog dialog(
        tagSession_,
        sourceVideoPath_,
        exportDefaultDirectoryPath_,
        queuedClips,
        exportJobManager_->activeOutputPaths(),
        this);
    dialog.setModal(true);

    if (dialog.exec() != QDialog::Accepted) return;

    const ExportSettingsDialog::Result settings = dialog.resultSettings();
    const ExportClipBuilder::SortOrder sortOrder = settings.sortByTeam
        ? ExportClipBuilder::SortOrder::ByTeamThenChronological
        : ExportClipBuilder::SortOrder::Chronological;
    const QVector<PresentationQueue::Clip> orderedClips =
        ExportClipBuilder::sortedClips(queuedClips, sortOrder);

    ExportClipBuilder::OverlayOptions overlayOptions;
    overlayOptions.language = settings.overlayLanguage;
    overlayOptions.includeBottomOverlay = settings.includeBottomOverlay;
    overlayOptions.includeScoreboardOverlay = settings.includeScoreboardOverlay;
    overlayOptions.includeNotesOverlay = settings.includeNotesOverlay;

    ExportJobRequest request;
    request.format = settings.format;
    request.sourceVideoPath = sourceVideoPath_;
    request.outputPath = settings.outputPath;
    request.clips = ExportClipBuilder::buildClipSegments(tagSession_, orderedClips, overlayOptions);
    request.includeAudioTrack = settings.includeAudioTrack;
    request.includeBrandingOverlay = settings.includeBrandingOverlay;
    request.tagSession = tagSession_;

    QString errorMessage;
    if (!exportJobManager_->startJob(request, &errorMessage)) {
        QMessageBox::warning(this, AppLocale::trUi("export.title"), errorMessage);
    }
}

void WorkWindow::onClipDurationSettings() {
    auto* dialog = new ClipDurationSettingsDialog(tagSession_, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setModal(true);
    connect(&LocaleNotifier::instance(), &LocaleNotifier::languageChanged, dialog,
            &ClipDurationSettingsDialog::applyUiStrings);
    dialog->show();
}

void WorkWindow::onImportXml() {
    if (!LicenseManager::instance().isEntitled()) return;
    if (!tagSession_ || !videoPlayer_ || sourceVideoPath_.isEmpty()) return;

    const QString filePath = QFileDialog::getOpenFileName(
        this,
        AppLocale::trUi("xml_import.select_file"),
        exportDefaultDirectoryPath_.isEmpty() ? QString() : exportDefaultDirectoryPath_,
        AppLocale::trUi("file.xml_filter"));
    if (filePath.isEmpty()) return;

    QVector<XmlImporter::ParsedInstance> instances;
    QString parseError;
    if (!XmlImporter::parse(filePath, &instances, &parseError)) {
        QMessageBox::warning(this, AppLocale::trUi("xml_import.title"), parseError);
        return;
    }

    TagSession::ImportMode importMode = TagSession::ImportMode::Replace;
    if (!tagSession_->tags().isEmpty()) {
        QMessageBox conflictBox(this);
        conflictBox.setWindowTitle(AppLocale::trUi("xml_import.conflict_title"));
        conflictBox.setText(AppLocale::trUi("xml_import.conflict_message"));
        QPushButton* replaceButton =
            conflictBox.addButton(AppLocale::trUi("xml_import.conflict_replace"),
                                  QMessageBox::AcceptRole);
        QPushButton* mergeButton =
            conflictBox.addButton(AppLocale::trUi("xml_import.conflict_merge"),
                                  QMessageBox::AcceptRole);
        conflictBox.addButton(AppLocale::trUi("xml_import.cancel"), QMessageBox::RejectRole);
        conflictBox.setDefaultButton(replaceButton);
        conflictBox.exec();
        if (conflictBox.clickedButton() == replaceButton) {
            importMode = TagSession::ImportMode::Replace;
        } else if (conflictBox.clickedButton() == mergeButton) {
            importMode = TagSession::ImportMode::Merge;
        } else {
            return;
        }
    }

    const XmlImporter::SyncAnchorResult anchor = XmlImporter::syncAnchorInstance(instances);
    if (!anchor.found) {
        QMessageBox::warning(this, AppLocale::trUi("xml_import.title"),
                             AppLocale::trUi("xml_import.no_sync_anchor"));
        return;
    }

    XmlSyncDialog syncDialog(videoPlayer_, anchor.instance, instances, anchor.usedFallback, this);
    syncDialog.setModal(true);
    if (videoPlayer_) {
        videoPlayer_->setPlaybackKeyboardShortcutsEnabled(false);
    }
    const int syncResult = syncDialog.exec();
    if (videoPlayer_) {
        videoPlayer_->setPlaybackKeyboardShortcutsEnabled(true);
    }
    if (syncResult != QDialog::Accepted) return;

    const qint64 offsetMs = syncDialog.offsetMs();

    XmlEventMappingDialog mappingDialog(instances, offsetMs, tagSession_, this);
    mappingDialog.setModal(true);
    if (mappingDialog.exec() != QDialog::Accepted) return;

    const XmlEventMappingDialog::ImportMappingResult importResult = mappingDialog.importResult();
    const QVector<TagSession::GameTag>& importedTags = importResult.tags;
    if (importedTags.isEmpty()) {
        QMessageBox::information(this, AppLocale::trUi("xml_import.title"),
                                 AppLocale::trUi("xml_import.mapping_none_selected"));
        return;
    }

    const qint64 videoDurationMs = videoPlayer_->durationMs();
    if (importMode == TagSession::ImportMode::Replace) {
        discardPendingClipNote();
    } else {
        flushPendingClipNote();
    }
    const TagSession::ImportResult result =
        tagSession_->importTags(importedTags, importMode, videoDurationMs);

    const int dialogSkipped = importResult.skippedInstanceCount;
    QMessageBox::information(
        this,
        AppLocale::trUi("xml_import.title"),
        AppLocale::trUi("xml_import.complete_summary")
            .arg(result.importedCount)
            .arg(dialogSkipped)
            .arg(result.clampedCount));
}

void WorkWindow::onModeToggled() {
    auto* btn = qobject_cast<QToolButton*>(sender());
    if (!btn) return;
    if (btn == modeTaggingBtn_) {
        setMode(Mode::Tagging);
    } else if (btn == modeAnalyzingBtn_) {
        setMode(Mode::Analyzing);
    } else if (btn == modePresentingBtn_) {
        setMode(Mode::Presenting);
    }
    if (modeTaggingBtn_) modeTaggingBtn_->setChecked(mode_ == Mode::Tagging);
    if (modeAnalyzingBtn_) modeAnalyzingBtn_->setChecked(mode_ == Mode::Analyzing);
    if (modePresentingBtn_) modePresentingBtn_->setChecked(mode_ == Mode::Presenting);
}

// ---------------------------------------------------------------------------
// Presentation mode
// ---------------------------------------------------------------------------

void WorkWindow::onPresentationSelectionChanged(const QVector<int>& tagSessionIndexes) {
    if (!presentationQueue_) return;
    presentationQueue_->setSelectedTagIndexes(tagSessionIndexes);
}

void WorkWindow::onPresentationClipActivated(int tagSessionIndex) {
    if (!LicenseManager::instance().isEntitled()) return;
    if (!presentationQueue_) return;
    if (mode_ != Mode::Presenting) setMode(Mode::Presenting);
    if (!presentationQueue_->setCurrentTagIndex(tagSessionIndex)) return;
    showPresentationClip(presentationQueue_->currentIndex(), /*startPlaying=*/true);
}

void WorkWindow::onPresentationQueueChanged() {
    if (presentationQueue_ && presentationQueue_->isEmpty()) {
        presentationPlaybackStarted_ = false;
        presentationAutoPauseArmed_ = false;
    }
    updatePresentationStage();
    configurePresentationClipBarForCurrentClip();
}

void WorkWindow::onPresentationCurrentClipChanged(int /*queueIndex*/) {
    updatePresentationStage();
    configurePresentationClipBarForCurrentClip();
}

void WorkWindow::onPresentationClipIntervalChanged(int queueIndex) {
    if (!presentationQueue_ || queueIndex != presentationQueue_->currentIndex()) return;
    const PresentationQueue::Clip* clip = presentationQueue_->currentClip();
    if (!clip) return;

    if (presentationPanel_) {
        presentationPanel_->setCurrentClip(clip->tagSessionIndex, clip->leadMs(), clip->lagMs());
    }
    // A drag on the clip bar already moved the handles; reconfiguring mid-drag would fight the user.
    if (!updatingPresentationClipBar_) {
        configurePresentationClipBarForCurrentClip();
    }

    const qint64 positionMs = videoPlayer_ ? videoPlayer_->currentPositionMs() : 0;
    if (positionMs < clip->endMs) {
        armPresentationAutoPause(clip->endMs);
    }
}

void WorkWindow::onPresentationLeadLagEdited(qint64 leadMs, qint64 lagMs) {
    if (!presentationQueue_) return;
    const PresentationQueue::Clip* clip = presentationQueue_->currentClip();
    if (!clip) return;
    const qint64 markMs = clip->markMs;
    presentationQueue_->setClipInterval(presentationQueue_->currentIndex(), markMs - leadMs,
                                        markMs + lagMs);
}

void WorkWindow::onPresentationApplyLeadLagToAll(qint64 leadMs, qint64 lagMs) {
    if (!presentationQueue_) return;
    presentationQueue_->applyLeadLagToAllClips(leadMs, lagMs);
    configurePresentationClipBarForCurrentClip();
}

void WorkWindow::onPresentationShowNotesToggled(bool /*enabled*/) {
    updatePresentationStage();
}

void WorkWindow::showPresentationClip(int queueIndex, bool startPlaying) {
    if (!presentationQueue_ || !videoPlayer_) return;
    presentationQueue_->setVideoDurationMs(videoPlayer_->durationMs());
    if (!presentationQueue_->setCurrentIndex(queueIndex)) return;

    const PresentationQueue::Clip* clip = presentationQueue_->currentClip();
    if (!clip) return;

    presentationPlaybackStarted_ = true;
    lastPresentationPlayheadMs_ = clip->startMs;
    videoPlayer_->seekToMs(clip->startMs);
    armPresentationAutoPause(clip->endMs);
    if (startPlaying) {
        videoPlayer_->playWithControlFlash();
    }
}

void WorkWindow::goToNextPresentationClip() {
    if (!presentationQueue_ || presentationQueue_->isEmpty()) return;
    // The first hop starts the queue where it stands instead of skipping the first clip.
    if (!presentationPlaybackStarted_) {
        showPresentationClip(qMax(0, presentationQueue_->currentIndex()), /*startPlaying=*/true);
        return;
    }
    if (!presentationQueue_->hasNextClip()) return;
    showPresentationClip(presentationQueue_->currentIndex() + 1, /*startPlaying=*/true);
}

void WorkWindow::goToPreviousPresentationClip() {
    if (!presentationQueue_ || presentationQueue_->isEmpty()) return;
    if (!presentationPlaybackStarted_) {
        showPresentationClip(qMax(0, presentationQueue_->currentIndex()), /*startPlaying=*/true);
        return;
    }
    if (!presentationQueue_->hasPreviousClip()) return;
    showPresentationClip(presentationQueue_->currentIndex() - 1, /*startPlaying=*/true);
}

void WorkWindow::updatePresentationStage() const {
    if (!presentationEventLabel_ || !presentationContextLabel_ || !presentationNoteLabel_) return;

    const PresentationQueue::Clip* clip =
        presentationQueue_ ? presentationQueue_->currentClip() : nullptr;

    if (!clip) {
        presentationEventLabel_->setText(AppLocale::trUi("presentation.empty_title"));
        presentationContextLabel_->setText(AppLocale::trUi("presentation.empty_hint"));
        presentationNoteLabel_->clear();
        presentationNoteLabel_->hide();
        if (presentationPanel_) presentationPanel_->clearCurrentClip();
        return;
    }

    TagSession::GameTag tagForDisplay;
    tagForDisplay.team = clip->team;
    const QString followUpForDisplay = tagSession_
        ? AppLocale::followUpPathWithoutTeamSegments(clip->followUpEvent,
                                                     tagSession_->homeTeamName(),
                                                     tagSession_->awayTeamName())
        : clip->followUpEvent;

    presentationEventLabel_->setText(
        AppLocale::trDisplayTagLine(clip->mainEvent, followUpForDisplay));

    const QString periodLabel =
        tagSession_ ? tagSession_->periodLabelAtTimestampMs(clip->markMs) : QString();
    QStringList contextParts;
    contextParts.append(QStringLiteral("%1 / %2")
                            .arg(presentationQueue_->currentIndex() + 1)
                            .arg(presentationQueue_->count()));
    const QString teamName = displayTeamForTag(tagForDisplay);
    if (!teamName.isEmpty() && teamName != QStringLiteral("—")) contextParts.append(teamName);
    if (!periodLabel.isEmpty()) contextParts.append(periodLabel);
    contextParts.append(formatTimestampMs(clip->markMs));
    presentationContextLabel_->setText(contextParts.join(QStringLiteral("  ·  ")));

    const bool showNote =
        presentationPanel_ && presentationPanel_->showNotesEnabled() && !clip->note.isEmpty();
    presentationNoteLabel_->setText(clip->note);
    presentationNoteLabel_->setVisible(showNote);

    if (presentationPanel_) {
        presentationPanel_->setCurrentClip(clip->tagSessionIndex, clip->leadMs(), clip->lagMs());
    }
}

void WorkWindow::configurePresentationClipBarForCurrentClip() const {
    if (!presentationClipBar_) return;

    const PresentationQueue::Clip* clip =
        presentationQueue_ ? presentationQueue_->currentClip() : nullptr;
    if (!clip) {
        presentationClipBar_->hide();
        return;
    }
    presentationClipBar_->show();

    // Same rule as the export reviewer: leave room on both sides of the clip so the presenter can
    // stretch the interval or scrub past it without the bar clipping the view.
    const qint64 halfWindowMs =
        std::max<qint64>(25000, std::max(clip->leadMs(), clip->lagMs()) + 5000);
    qint64 windowStartMs = clip->markMs - halfWindowMs;
    qint64 windowEndMs = clip->markMs + halfWindowMs;
    if (windowStartMs < 0) windowStartMs = 0;
    const qint64 videoDurationMs = videoPlayer_ ? videoPlayer_->durationMs() : 0;
    if (videoDurationMs > 0 && windowEndMs > videoDurationMs) windowEndMs = videoDurationMs;

    presentationClipBar_->configure(clip->markMs, clip->startMs, clip->endMs, windowStartMs,
                                    windowEndMs);
    if (videoPlayer_) {
        presentationClipBar_->setPlayheadMs(videoPlayer_->currentPositionMs());
    }
}

void WorkWindow::savePresentationClipIntervalFromClipBar() {
    if (!presentationQueue_ || !presentationClipBar_) return;
    const int queueIndex = presentationQueue_->currentIndex();
    if (queueIndex < 0) return;

    updatingPresentationClipBar_ = true;
    presentationQueue_->setClipInterval(queueIndex, presentationClipBar_->clipStartMs(),
                                        presentationClipBar_->clipEndMs());
    updatingPresentationClipBar_ = false;
}

void WorkWindow::armPresentationAutoPause(qint64 clipEndMs) {
    presentationAutoPauseAtMs_ = clipEndMs;
    presentationAutoPauseArmed_ = true;
}

void WorkWindow::updatePresentationPlayhead(qint64 positionMs) {
    if (mode_ != Mode::Presenting) {
        lastPresentationPlayheadMs_ = positionMs;
        return;
    }
    if (presentationClipBar_) presentationClipBar_->setPlayheadMs(positionMs);

    const qint64 previousPositionMs = lastPresentationPlayheadMs_;
    lastPresentationPlayheadMs_ = positionMs;

    if (!presentationAutoPauseArmed_ || positionMs < presentationAutoPauseAtMs_) return;

    // Stop once when playback runs into the lag boundary. Crossing it by scrubbing or seeking is a
    // deliberate move past the clip, so it only disarms the stop: exploring is never interrupted.
    constexpr qint64 kMaxPlaybackAdvancePerSampleMs = 2000;
    const bool crossedWhilePlaying =
        videoPlayer_ && videoPlayer_->isPlaying() && previousPositionMs >= 0 &&
        previousPositionMs < presentationAutoPauseAtMs_ &&
        (positionMs - previousPositionMs) <= kMaxPlaybackAdvancePerSampleMs;

    presentationAutoPauseArmed_ = false;
    if (crossedWhilePlaying) {
        videoPlayer_->pauseWithControlFlash();
    }
}

void WorkWindow::attachPresentationKeyboardShortcuts() {
    if (presentationKeyboardShortcutsInstalled_) return;
    if (QApplication* application = qApp) {
        application->installEventFilter(this);
        presentationKeyboardShortcutsInstalled_ = true;
    }
}

void WorkWindow::detachPresentationKeyboardShortcuts() {
    if (!presentationKeyboardShortcutsInstalled_) return;
    if (QApplication* application = qApp) {
        application->removeEventFilter(this);
    }
    presentationKeyboardShortcutsInstalled_ = false;
}

bool WorkWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == notesEdit_ && event && event->type() == QEvent::FocusOut) {
        flushPendingClipNote();
    }

    if (!event || !presentationKeyboardShortcutsInstalled_ || mode_ != Mode::Presenting ||
        event->type() != QEvent::KeyPress) {
        return QWidget::eventFilter(watched, event);
    }

    auto* targetWidget = qobject_cast<QWidget*>(watched);
    if (!targetWidget || targetWidget->window() != window()) {
        return QWidget::eventFilter(watched, event);
    }

    auto* keyEvent = static_cast<QKeyEvent*>(event);
    const bool isTabKey = keyEvent->key() == Qt::Key_Tab || keyEvent->key() == Qt::Key_Backtab;
    if (!isTabKey) return QWidget::eventFilter(watched, event);

    // Let Tab keep its normal meaning while the user is typing in the panel.
    if (isTextInteractionFocusWidget(QApplication::focusWidget())) {
        return QWidget::eventFilter(watched, event);
    }

    const bool goBackwards = keyEvent->key() == Qt::Key_Backtab ||
                             (keyEvent->modifiers() & Qt::ShiftModifier) != 0;
    if (goBackwards) {
        goToPreviousPresentationClip();
    } else {
        goToNextPresentationClip();
    }
    return true;
}

void WorkWindow::onTagSelectionChanged() {
    flushPendingClipNote();
    loadNoteForSelectedTag();
}

void WorkWindow::onNoteTextChanged() {
    if (!notesEdit_ || !tagSession_ || !noteDebounceTimer_) return;
    const quint64 tagId = selectedTagId();
    if (tagId == 0 || tagSession_->indexOfTagId(tagId) < 0) return;
    pendingNoteTagId_ = tagId;
    pendingNoteText_ = notesEdit_->toPlainText();
    noteDebounceTimer_->start(400);
}

void WorkWindow::saveNoteDebounceFired() {
    flushPendingClipNote();
}

void WorkWindow::onMatchNoteTextChanged() {
    if (!matchNoteDebounceTimer_) return;
    matchNoteDebounceTimer_->start(400);
}

void WorkWindow::saveMatchNoteDebounceFired() {
    if (!tagSession_ || !matchNotesEditor_) return;
    tagSession_->setMatchNote(matchNotesEditor_->serializedHtml());
}

void WorkWindow::discardPendingClipNote() {
    if (noteDebounceTimer_) noteDebounceTimer_->stop();
    pendingNoteTagId_ = 0;
    pendingNoteText_.clear();
}

void WorkWindow::flushPendingClipNote() {
    if (noteDebounceTimer_) noteDebounceTimer_->stop();
    if (pendingNoteTagId_ == 0) return;

    const quint64 tagId = pendingNoteTagId_;
    const QString noteText = pendingNoteText_;
    pendingNoteTagId_ = 0;
    pendingNoteText_.clear();

    if (!tagSession_) return;
    const int sessionIndex = tagSession_->indexOfTagId(tagId);
    if (sessionIndex < 0 || !tagSession_->isValidTagIndex(sessionIndex)) return;
    if (!tagSession_->setTagNote(sessionIndex, noteText)) return;
}

quint64 WorkWindow::selectedTagId() const {
    return tagIdFromItem(selectedTagRowTimeItem());
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
        connect(statsOverlay_, &StatsWindow::filterByEventPathRequested, this, &WorkWindow::onFilterByEventPathRequested);
    }
    if (statsOverlay_) statsOverlay_->setTagSession(tagSession_);
    statsOverlayDialog_->raise();
    statsOverlayDialog_->show();
}

void WorkWindow::loadNoteForSelectedTag() const {
    if (!notesEdit_) return;

    const QTableWidgetItem* item = selectedTagRowTimeItem();
    const quint64 selectedId = tagIdFromItem(item);
    if (pendingNoteTagId_ != 0 && pendingNoteTagId_ == selectedId) {
        notesEdit_->blockSignals(true);
        if (notesEdit_->toPlainText() != pendingNoteText_) {
            notesEdit_->setPlainText(pendingNoteText_);
        }
        notesEdit_->blockSignals(false);
        notesEdit_->setEnabled(true);
        notesEdit_->setPlaceholderText(AppLocale::trUi("tags.note_placeholder"));
        return;
    }

    notesEdit_->blockSignals(true);
    if (!item || !tagSession_) {
        if (!notesEdit_->toPlainText().isEmpty()) {
            notesEdit_->clear();
        }
        notesEdit_->setEnabled(false);
        notesEdit_->setPlaceholderText(AppLocale::trUi("notes.clip_placeholder_none"));
        notesEdit_->blockSignals(false);
        return;
    }

    int sessionIndex = tagSession_->indexOfTagId(selectedId);
    if (sessionIndex < 0) {
        sessionIndex = tagSessionIndexFromItem(item);
    }
    if (tagSession_->isValidTagIndex(sessionIndex)) {
        const QString noteText = tagSession_->tagNote(sessionIndex);
        if (notesEdit_->toPlainText() != noteText) {
            notesEdit_->setPlainText(noteText);
        }
        notesEdit_->setEnabled(true);
        notesEdit_->setPlaceholderText(AppLocale::trUi("tags.note_placeholder"));
    } else {
        if (!notesEdit_->toPlainText().isEmpty()) {
            notesEdit_->clear();
        }
        notesEdit_->setEnabled(true);
        notesEdit_->setPlaceholderText(AppLocale::trUi("notes.clip_placeholder_none"));
    }
    notesEdit_->blockSignals(false);
}

void WorkWindow::loadMatchNote() const {
    if (!matchNotesEditor_) return;
    const QString html = tagSession_ ? tagSession_->matchNote() : QString();
    if (matchNotesEditor_->isDocumentEquivalentTo(html)) return;
    matchNotesEditor_->setSerializedHtml(html);
}

void WorkWindow::refreshMatchNoteMentionCandidates() const {
    if (!matchNotesEditor_) return;

    QVector<MatchNotesEditor::MentionCandidate> candidates;
    if (tagSession_) {
        for (const TagSession::GameTag& tag : tagSession_->tags()) {
            if (tag.id == 0) continue;
            if (EventDefaults::isTimeControlEvent(tag.mainEvent)) continue;
            const QString followUp = followUpForEventColumn(tag.followUpEvent, tagSession_);
            const QString eventLine = AppLocale::trDisplayTagLine(tag.mainEvent, followUp);
            const QString teamName = displayTeamForTag(tag);
            MatchNotesEditor::MentionCandidate candidate;
            candidate.tagId = tag.id;
            candidate.label = QStringLiteral("%1  %2  ·  %3")
                                  .arg(formatTimestampMs(tag.markMs), eventLine, teamName);
            candidate.searchText = candidate.label;
            candidates.append(candidate);
        }
    }
    matchNotesEditor_->setMentionCandidates(candidates);
}

void WorkWindow::onMatchNoteTagMentionActivated(quint64 tagId) {
    if (!tagSession_ || !tagsTable_) return;
    const int sessionIndex = tagSession_->indexOfTagId(tagId);
    if (!tagSession_->isValidTagIndex(sessionIndex)) return;

    for (int row = 0; row < tagsTable_->rowCount(); ++row) {
        QTableWidgetItem* timeItem = tagsTable_->item(row, 0);
        if (!timeItem) continue;
        if (tagIdFromItem(timeItem) != tagId) continue;
        tagsTable_->selectRow(row);
        tagsTable_->scrollToItem(timeItem, QAbstractItemView::PositionAtCenter);
        onTagTableSeekToRow(row);
        return;
    }

    if (videoPlayer_) {
        videoPlayer_->seekToMs(tagSession_->tags().at(sessionIndex).markMs);
    }
}

void WorkWindow::onTagTableSeekToRow(int row) {
    if (row < 0 || !videoPlayer_ || !tagsTable_) return;
    QTableWidgetItem* timeItem = tagsTable_->item(row, 0);
    if (!timeItem) return;
    videoPlayer_->seekToMs(timeItem->data(kTagMarkMsRole).toLongLong());
}

QTableWidgetItem* WorkWindow::selectedTagRowTimeItem() const {
    if (!tagsTable_) return nullptr;
    const int row = tagsTable_->currentRow();
    return row >= 0 ? tagsTable_->item(row, 0) : nullptr;
}

void WorkWindow::setTagTableRowBackground(int row, const QBrush& brush) const {
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
        const int tagIndex = tagSessionIndexFromItem(tagsTable_->item(row, 0));
        if (tagSession_ && tagSession_->isValidTagIndex(tagIndex)) {
            paintTeamCellForTag(teamItem, tagSession_->tags().at(tagIndex), tagSession_);
            return;
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
    setTagTableRowBackground(newTagFlashRow_, QBrush(Style::ThemeColors::playheadHighlight()));
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
}

void WorkWindow::updateTagPlayheadHighlight(qint64 positionMs) const {
    if (!tagsTable_) return;
    for (int row = 0; row < tagsTable_->rowCount(); ++row) {
        QTableWidgetItem* keyItem = tagsTable_->item(row, 0);
        if (!keyItem) continue;
        const qint64 tagMs = keyItem->data(kTagMarkMsRole).toLongLong();
        const qint64 diff = (tagMs > positionMs) ? (tagMs - positionMs) : (positionMs - tagMs);
        if (diff <= kPlayheadNearToleranceMs) {
            setTagTableRowBackground(row, QBrush(Style::ThemeColors::playheadHighlight()));
        } else {
            setTagTableRowBackground(row, QBrush());
        }
    }
}

void WorkWindow::onDeleteSelectedTag() {
    if (!tagsTable_ || !tagSession_) return;

    auto* item = selectedTagRowTimeItem();
    if (!item) return;
    
    const quint64 tagId = tagIdFromItem(item);
    int tagIndex = tagSession_->indexOfTagId(tagId);
    if (tagIndex < 0) {
        tagIndex = tagSessionIndexFromItem(item);
    }
    if (tagIndex < 0 || !tagSession_->isValidTagIndex(tagIndex)) return;

    if (pendingNoteTagId_ != 0 && pendingNoteTagId_ == tagId) {
        discardPendingClipNote();
    } else {
        flushPendingClipNote();
    }
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

void WorkWindow::onFilterByEventPathRequested(const QString& mainEvent, const QString& followUpEvent) {
    activeEventPathMainEvent_ = mainEvent;
    activeEventPathFollowUp_ = followUpEvent;
    rebuildTagsList();
    updateFilterIndicator();
    updateFilterButtonsVisibility();
}

void WorkWindow::onRemoveFilters() {
    activeEventPathMainEvent_.clear();
    activeEventPathFollowUp_.clear();
    onSelectAllFilters();
    updateFilterButtonsVisibility();
}

bool WorkWindow::isMainEventAllowed(const QString& mainEvent) const {
    auto it = filterActionByMainEvent_.find(mainEvent);
    if (it == filterActionByMainEvent_.end()) return true; // no filter entry yet -> allow
    return it.value()->isChecked();
}

bool WorkWindow::isTagAllowed(const QString& mainEvent, const QString& followUpEvent) const {
    if (!activeEventPathMainEvent_.isEmpty()) {
        if (mainEvent != activeEventPathMainEvent_) return false;
        if (activeEventPathFollowUp_.isEmpty()) return true;
        return followUpEvent == activeEventPathFollowUp_
            || followUpEvent.startsWith(activeEventPathFollowUp_ + " → ");
    }
    return isMainEventAllowed(mainEvent);
}

bool WorkWindow::hasAnyFilterActive() const {
    if (!activeEventPathMainEvent_.isEmpty()) return true;
    for (auto it = filterActionByMainEvent_.cbegin(); it != filterActionByMainEvent_.cend(); ++it) {
        if (!it.value()->isChecked()) return true;
    }
    return false;
}

void WorkWindow::updateFilterButtonsVisibility() const {
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

void WorkWindow::updateFilterIndicator() const {
    if (!tagsFilterIndicator_) return;

    if (!activeEventPathMainEvent_.isEmpty()) {
        QString pathText = AppLocale::trEvent(activeEventPathMainEvent_);
        if (!activeEventPathFollowUp_.isEmpty()) {
            pathText += QStringLiteral(" → ") + AppLocale::translateCompoundPath(activeEventPathFollowUp_);
        }
        tagsFilterIndicator_->setText(AppLocale::trUi("filter.indicator") + pathText);
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
    const QString text = AppLocale::trUi("filter.indicator") + activeFilters.join(QStringLiteral(", "));
    tagsFilterIndicator_->setText(text);
    tagsFilterIndicator_->show();
}

void WorkWindow::rebuildTagsList() {
    if (!tagsTable_) return;

    const ScopedTrueFlag reloadGuard(suppressClipNoteReload_);
    const quint64 previouslySelectedTagId = selectedTagId();
    const QSignalBlocker tableBlocker(tagsTable_);

    if (!tagSession_) {
        discardPendingClipNote();
        tagsTable_->setRowCount(0);
        refreshMatchNoteMentionCandidates();
        loadNoteForSelectedTag();
        return;
    }

    flushPendingClipNote();
    tagsTable_->setRowCount(0);

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
        return a.tag.markMs < b.tag.markMs;
    });

    tagsTable_->setRowCount(entries.size());
    int row = 0;
    for (const auto& e : entries) {
        const auto& tag = e.tag;
        const QString timeText = formatTimestampMs(tag.markMs);
        const QString teamText = displayTeamForTag(tag);
        const QString eventText =
            AppLocale::trDisplayTagLine(tag.mainEvent, followUpForEventColumn(tag.followUpEvent, tagSession_));

        auto* timeItem = new QTableWidgetItem(timeText);
        timeItem->setData(kTagMarkMsRole, tag.markMs);
        timeItem->setData(kTagMainEventRole, tag.mainEvent);
        timeItem->setData(kTagFollowUpEventRole, tag.followUpEvent);
        timeItem->setData(kTagSessionIndexRole, e.tagSessionIndex);
        timeItem->setData(kTagIdRole, tag.id);
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

    const int restoredRow = rowForTagId(tagsTable_, previouslySelectedTagId);
    if (restoredRow >= 0) {
        tagsTable_->selectRow(restoredRow);
        if (QTableWidgetItem* restoredItem = tagsTable_->item(restoredRow, 0)) {
            tagsTable_->scrollToItem(restoredItem, QAbstractItemView::EnsureVisible);
        }
    } else {
        tagsTable_->scrollToBottom();
    }

    updateFilterIndicator();
    updateFilterButtonsVisibility();
    if (videoPlayer_) {
        updateTagPlayheadHighlight(videoPlayer_->currentPositionMs());
    }
    refreshMatchNoteMentionCandidates();
    loadNoteForSelectedTag();
}
