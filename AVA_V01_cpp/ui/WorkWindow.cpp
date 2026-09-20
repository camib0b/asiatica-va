#include "WorkWindow.h"
#include "TagsTableModel.h"
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
#include <QMenu>
#include <QTimer>
#include <QPointer>
#include <QIcon>
#include <QVideoWidget>
#include <QAbstractItemView>
#include <QHeaderView>
#include <QTableView>
#include <QItemSelectionModel>
#include <QAction>
#include <QKeySequence>
#include <QBrush>

namespace {
constexpr int kVideoMuteFlashDurationMs = 150;

void setToolButtonFlashState(QToolButton* button, bool flashing) {
  if (!button) {
    return;
  }
  button->setProperty("flash", flashing);
  button->style()->unpolish(button);
  button->style()->polish(button);
  button->update();
}
}  // namespace
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
#include <QSizePolicy>
#include <QHBoxLayout>
#include <QScrollBar>
#include <QApplication>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QKeyEvent>
#include <QTextEdit>

#include <algorithm>

namespace {

bool isTextInteractionFocusWidget(const QWidget* widget) {
    if (!widget) return false;
    if (qobject_cast<const QLineEdit*>(widget)) return true;
    if (qobject_cast<const QAbstractSpinBox*>(widget)) return true;
    if (qobject_cast<const QPlainTextEdit*>(widget)) return true;
    if (qobject_cast<const QTextEdit*>(widget)) return true;
    if (qobject_cast<const QComboBox*>(widget)) return true;
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

QColor teamBackgroundForTag(const TagSession::GameTag& tag, const TagSession* session) {
    if (!session) return {};
    QString hex;
    if (tag.team == QStringLiteral("Home")) {
        hex = session->homeTeamColor();
    } else if (tag.team == QStringLiteral("Away")) {
        hex = session->awayTeamColor();
    } else {
        return {};
    }
    QString hexClean = hex.trimmed();
    if (hexClean.isEmpty()) return {};
    if (!hexClean.startsWith(QLatin1Char('#'))) {
        hexClean.prepend(QLatin1Char('#'));
    }
    const QColor backgroundColor(hexClean);
    return backgroundColor.isValid() ? backgroundColor : QColor();
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
    applyModeChrome();
    applyUiStrings();
}

WorkWindow::~WorkWindow() {
    flushPendingClipNote();
    detachPresentationKeyboardShortcuts();
    disconnectTagSessionSignals();
    tagSession_ = nullptr;
}

bool WorkWindow::shouldDeliverPlaybackKeyboardToVideoPlayer(const QWidget* focusWidget) const {
    if (!contentStack_ || contentStack_->currentIndex() != 1) return false;
    if (!videoPlayer_ || !videoPlayer_->isMediaKeyboardControlActive()) return false;
    if (!focusWidget) return true;
    if (focusWidget->window() != window()) return false;
    if (!isAncestorOf(focusWidget)) return false;
    if (mode_ == Mode::Analyzing && notesEdit_ && focusWidget == notesEdit_) return false;
    if (isTextInteractionFocusWidget(focusWidget)) return false;
    return true;
}

void WorkWindow::onApplicationFocusWidgetChanged(QWidget* /*oldFocus*/, QWidget* newFocus) const {
    if (!videoPlayer_) {
        return;
    }
    const bool allowPlaybackShortcuts = shouldDeliverPlaybackKeyboardToVideoPlayer(newFocus);
    videoPlayer_->setPlaybackShortcutFocusGate(allowPlaybackShortcuts);
}

void WorkWindow::refreshPlaybackShortcutFocusGate() const {
    onApplicationFocusWidgetChanged(nullptr, QApplication::focusWidget());
}

void WorkWindow::updateVideoMuteButton(bool muted) const {
    if (!videoMuteButton_) {
        return;
    }

    const QIcon volumeIcon = QIcon::fromTheme(QStringLiteral("audio-volume-high"));
    const QIcon mutedIcon = QIcon::fromTheme(QStringLiteral("audio-volume-muted"));
    if (muted) {
        if (!mutedIcon.isNull()) {
            videoMuteButton_->setIcon(mutedIcon);
            videoMuteButton_->setText(QString());
        } else {
            videoMuteButton_->setIcon(QIcon());
            videoMuteButton_->setText(QString::fromUtf8("\u{1F507}"));
        }
    } else {
        if (!volumeIcon.isNull()) {
            videoMuteButton_->setIcon(volumeIcon);
            videoMuteButton_->setText(QString());
        } else {
            videoMuteButton_->setIcon(QIcon());
            videoMuteButton_->setText(QString::fromUtf8("\u{1F50A}"));
        }
    }

    videoMuteButton_->setChecked(muted);
    videoMuteButton_->setToolTip(muted ? AppLocale::trUi("vc.tt.unmute") : AppLocale::trUi("vc.tt.mute"));
}

void WorkWindow::setVideoMuteButtonEnabled(bool enabled) const {
    if (videoMuteButton_) {
        videoMuteButton_->setEnabled(enabled);
    }
}

void WorkWindow::flashVideoMuteButton() {
    if (!videoMuteButton_) {
        return;
    }

    auto* timer = videoMuteButton_->findChild<QTimer*>(QStringLiteral("flashClearTimer"),
                                                       Qt::FindDirectChildrenOnly);
    if (!timer) {
        timer = new QTimer(videoMuteButton_);
        timer->setObjectName(QStringLiteral("flashClearTimer"));
        timer->setSingleShot(true);
        connect(timer, &QTimer::timeout, this, [buttonGuard = QPointer<QToolButton>(videoMuteButton_)]() {
            setToolButtonFlashState(buttonGuard, false);
        });
    }

    setToolButtonFlashState(videoMuteButton_, true);
    timer->start(kVideoMuteFlashDurationMs);
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
    }
    if (modeAnalyzingBtn_) {
        modeAnalyzingBtn_->setText(AppLocale::trUi("mode.analyzing"));
    }
    if (modePresentingBtn_) {
        modePresentingBtn_->setText(AppLocale::trUi("mode.presenting"));
    }
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
    if (tagsModel_) {
        tagsModel_->setColumnHeaders({AppLocale::trUi("tags.col_time"), AppLocale::trUi("tags.col_team"),
                                      AppLocale::trUi("tags.col_event")});
    }
    if (videoMuteButton_) {
        updateVideoMuteButton(videoPlayer_ && videoPlayer_->controlsBar()
                                  ? videoPlayer_->controlsBar()->muted()
                                  : false);
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
    refreshTagsTableRows();
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
    refreshTagsTableRows();
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
    }));

    tagSessionConnections_.append(connect(tagSession_, &TagSession::tagsChanged, this, [this]() {
        if (!tagSession_) return;
        rebuildFilterMenu();
        refreshTagsTableRows();
        if (lastAddedTagId_ != 0) {
            flashNewTagRow();
            lastAddedTagId_ = 0;
        }
        if (gameControls_) {
            gameControls_->restoreGamePhase(tagSession_->quarterPhase(),
                                            tagSession_->currentQuarterIndex());
        }
        syncContextPeriodFromSession();
    }));

    tagSessionConnections_.append(connect(tagSession_, &TagSession::tagAdded, this, [this](const TagSession::GameTag& tag) {
        lastAddedTagId_ = tag.id;
    }));
    tagSessionConnections_.append(connect(tagSession_, &TagSession::tagsImported, this, [this]() {
        if (!tagSession_) return;
        if (gameControls_) {
            gameControls_->restoreGamePhase(tagSession_->quarterPhase(),
                                            tagSession_->currentQuarterIndex());
        }
        syncContextPeriodFromSession();
    }));
    tagSessionConnections_.append(connect(tagSession_, &TagSession::tagNoteChanged, this, [this](int changedIndex) {
        if (!notesEdit_ || !tagSession_) return;
        if (notesEdit_->hasFocus() || pendingNoteTagId_ != 0) return;
        if (!tagSession_->isValidTagIndex(changedIndex)) return;
        const quint64 changedTagId = tagSession_->tags().at(changedIndex).id;
        if (selectedTagId() != changedTagId) return;
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
        refreshTagsTableRows();
        refreshMatchNoteMentionCandidates();
    }));
}

void WorkWindow::setMode(Mode m) {
    if (m == Mode::Presenting && !LicenseManager::instance().isEntitled()) {
        return;
    }
    if (mode_ == m) return;
    flushPendingClipNote();
    if (mode_ == Mode::Presenting && m != Mode::Presenting) {
        detachPresentationKeyboardShortcuts();
        presentationAutoPauseArmed_ = false;
    }
    mode_ = m;
    applyModeChrome();
    if (m == Mode::Presenting) {
        if (presentationQueue_ && videoPlayer_) {
            presentationQueue_->setVideoDurationMs(videoPlayer_->durationMs());
        }
        if (presentationPanel_) presentationPanel_->refreshFromSession();
        updatePresentationStage();
        configurePresentationClipBarForCurrentClip();
        attachPresentationKeyboardShortcuts();
    }
    if (modeTaggingBtn_) modeTaggingBtn_->setChecked(m == Mode::Tagging);
    if (modeAnalyzingBtn_) modeAnalyzingBtn_->setChecked(m == Mode::Analyzing);
    if (modePresentingBtn_) modePresentingBtn_->setChecked(m == Mode::Presenting);
    refreshPlaybackShortcutFocusGate();
}

void WorkWindow::applyModeChrome() {
    const bool isTagging = mode_ == Mode::Tagging;
    const bool isAnalyzing = mode_ == Mode::Analyzing;
    const bool isPresenting = mode_ == Mode::Presenting;

    if (presentationBanner_) presentationBanner_->setVisible(isPresenting);
    if (presentationClipBar_) presentationClipBar_->setVisible(isPresenting);
    if (workTagsNotesSplitter_) workTagsNotesSplitter_->setVisible(!isPresenting);
    if (notesColumn_) notesColumn_->setVisible(isAnalyzing);
    if (tagsHeaderRow_) tagsHeaderRow_->setVisible(isAnalyzing);

    if (workOuterSplitter_) {
        workOuterSplitter_->setChildrenCollapsible(isPresenting);
    }

    if (workSideStack_) {
        if (isTagging && taggingRightCol_) {
            workSideStack_->setCurrentWidget(taggingRightCol_);
        } else if (isAnalyzing && statsWindow_) {
            workSideStack_->setCurrentWidget(statsWindow_);
        } else if (isPresenting && presentationPanel_) {
            workSideStack_->setCurrentWidget(presentationPanel_);
        }
    }

    if (gameControls_) {
        gameControls_->setMinimumWidth(GameControls::kMinimumPanelWidthPx);
        gameControls_->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
    }
    if (taggingRightCol_) {
        taggingRightCol_->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
    }
    if (workSideStack_) {
        workSideStack_->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
    }
}

void WorkWindow::applySidePanelCompressedWidth() {
    if (!workOuterSplitter_ || !workOuterSplitter_->isVisible()) return;

    const int sideMinimumWidth = GameControls::kMinimumPanelWidthPx;
    const int totalWidth = workOuterSplitter_->width();
    if (totalWidth <= sideMinimumWidth) return;

    workOuterSplitter_->setSizes({totalWidth - sideMinimumWidth, sideMinimumWidth});
}

TagSession::GameTag WorkWindow::pendingTagPeriodAndTeam() const {
    TagSession::GameTag pendingTag;
    pendingTag.period = contextPeriod_;
    pendingTag.team = contextTeam_;
    return pendingTag;
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
    mainContentLayout->setSpacing(4);

    // Top row: mode toggle | settings icon
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

    videoControlsRow_ = new QWidget(this);
    auto* videoControlsLayout = new QHBoxLayout(videoControlsRow_);
    videoControlsLayout->setContentsMargins(0, 0, 0, 0);
    videoControlsLayout->setSpacing(8);
    videoControlsLayout->addStretch(1);
    videoMuteButton_ = new QToolButton(this);
    videoMuteButton_->setObjectName(QStringLiteral("VideoMuteButton"));
    videoMuteButton_->setCheckable(true);
    videoMuteButton_->setMinimumWidth(36);
    Style::setVariant(videoMuteButton_, "ghost");
    Style::setSize(videoMuteButton_, "sm");
    videoMuteButton_->setCursor(Qt::PointingHandCursor);
    videoMuteButton_->setFocusPolicy(Qt::NoFocus);
    videoMuteButton_->setEnabled(false);
    updateVideoMuteButton(false);
    videoControlsLayout->addWidget(videoMuteButton_, 0, Qt::AlignRight | Qt::AlignVCenter);

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
    videoMenuButton_->setFocusPolicy(Qt::NoFocus);
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

    contentArea_ = new QWidget(mainContentContainer_);
    auto* contentAreaLayout = new QVBoxLayout(contentArea_);
    contentAreaLayout->setContentsMargins(0, 0, 0, 0);
    contentAreaLayout->setSpacing(0);
    mainContentLayout->addWidget(contentArea_, 1);

    gameSetupWidget_ = new GameSetupWindow(this);
    contentStack_->addWidget(gameSetupWidget_);
    contentStack_->addWidget(mainContentContainer_);
    contentStack_->setCurrentIndex(0);

    layout->addWidget(contentStack_);

    exportJobManager_ = new ExportJobManager(this);
    exportJobsBar_ = new ExportJobsBar(exportJobManager_, this);
    layout->addWidget(exportJobsBar_, 0);

    taggingRightCol_ = new QWidget(this);
    taggingRightCol_->setObjectName(QStringLiteral("TaggingRightCol"));
    auto* taggingRightLayout = new QVBoxLayout(taggingRightCol_);
    taggingRightLayout->setContentsMargins(0, 0, 0, 0);

    // Tags section
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

    tagsModel_ = new TagsTableModel(this);
    tagsModel_->setColumnHeaders({AppLocale::trUi("tags.col_time"), AppLocale::trUi("tags.col_team"),
                                  AppLocale::trUi("tags.col_event")});
    tagsTable_ = new QTableView(tagsSection_);
    tagsTable_->setObjectName("TagsTable");
    tagsTable_->setModel(tagsModel_);
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

    gameControls_ = new GameControls(this);
    gameControls_->setMinimumWidth(GameControls::kMinimumPanelWidthPx);
    taggingRightLayout->addWidget(gameControls_, 1);
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

    videoColumn_ = new QWidget(this);
    videoColumn_->setObjectName(QStringLiteral("PresentationStageColumn"));
    videoColumn_->setAttribute(Qt::WA_StyledBackground, true);
    videoColumn_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* videoColumnLayout = new QVBoxLayout(videoColumn_);
    videoColumnLayout->setContentsMargins(0, 0, 0, 0);
    videoColumnLayout->setSpacing(8);

    presentationBanner_ = new QWidget(videoColumn_);
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

    videoColumnLayout->addWidget(presentationBanner_, 0);

    videoPlayer_ = new VideoPlayer(videoColumn_);
    videoColumnLayout->addWidget(videoPlayer_, 1);

    presentationClipBar_ = new ClipTrimBar(videoColumn_);
    videoColumnLayout->addWidget(presentationClipBar_, 0);

    presentationBanner_->hide();
    presentationClipBar_->hide();

    workTagsNotesSplitter_ = new QSplitter(Qt::Horizontal, this);
    workTagsNotesSplitter_->setObjectName(QStringLiteral("WorkTagsNotesSplitter"));
    workTagsNotesSplitter_->setChildrenCollapsible(false);
    workTagsNotesSplitter_->setHandleWidth(6);
    workTagsNotesSplitter_->addWidget(tagsSection_);
    workTagsNotesSplitter_->addWidget(notesColumn_);
    workTagsNotesSplitter_->setStretchFactor(0, 1);
    workTagsNotesSplitter_->setStretchFactor(1, 1);
    tagsSection_->setMinimumWidth(160);

    workLeftSplitter_ = new QSplitter(Qt::Vertical, this);
    workLeftSplitter_->setObjectName(QStringLiteral("WorkLeftSplitter"));
    workLeftSplitter_->setChildrenCollapsible(false);
    workLeftSplitter_->setHandleWidth(6);
    workLeftSplitter_->addWidget(videoColumn_);
    workLeftSplitter_->addWidget(workTagsNotesSplitter_);
    workLeftSplitter_->setStretchFactor(0, 5);
    workLeftSplitter_->setStretchFactor(1, 3);

    workSideStack_ = new QStackedWidget(this);
    workSideStack_->addWidget(taggingRightCol_);
    workSideStack_->addWidget(statsWindow_);
    presentationPanel_ = new PresentationPanel(this);
    presentationPanel_->setMinimumWidth(260);
    presentationPanel_->setExportEnabled(false);
    workSideStack_->addWidget(presentationPanel_);

    workOuterSplitter_ = new QSplitter(Qt::Horizontal, this);
    workOuterSplitter_->setObjectName(QStringLiteral("PresentationSplitter"));
    workOuterSplitter_->setChildrenCollapsible(false);
    workOuterSplitter_->setHandleWidth(6);
    workOuterSplitter_->setAttribute(Qt::WA_StyledBackground, true);
    workOuterSplitter_->addWidget(workLeftSplitter_);
    workOuterSplitter_->addWidget(workSideStack_);
    workOuterSplitter_->setStretchFactor(0, 4);
    workOuterSplitter_->setStretchFactor(1, 1);

    for (QSplitter* splitter : {workOuterSplitter_, workLeftSplitter_, workTagsNotesSplitter_}) {
        splitter->setStyleSheet(QStringLiteral("background-color: #FFFFFF;"));
    }

    contentAreaLayout->addWidget(workOuterSplitter_, 1);
    workOuterSplitter_->hide();

    buildPresentationUi();

    if (videoPlayer_) videoPlayer_->setControlsVisible(false);
    if (modeTaggingBtn_) modeTaggingBtn_->hide();
    if (modeAnalyzingBtn_) modeAnalyzingBtn_->hide();
    if (modePresentingBtn_) modePresentingBtn_->hide();
}

void WorkWindow::buildPresentationUi() {
    presentationQueue_ = new PresentationQueue(this);

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
    connect(videoMuteButton_, &QToolButton::clicked, this, [this]() {
        if (videoPlayer_) videoPlayer_->toggleMuteWithControlFlash();
    });
    connect(videoPlayer_, &VideoPlayer::muteStateChanged, this, &WorkWindow::updateVideoMuteButton);
    connect(videoPlayer_, &VideoPlayer::muteToolbarFlashRequested, this, &WorkWindow::flashVideoMuteButton);

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

    connect(tagsTable_, &QTableView::doubleClicked, this, [this](const QModelIndex& index) {
        if (index.isValid()) onTagTableSeekToRow(index.row());
    });
    connect(tagsTable_, &QAbstractItemView::activated, this, [this](const QModelIndex& index) {
        if (index.isValid()) onTagTableSeekToRow(index.row());
    });
    connect(tagsTable_->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex& current, const QModelIndex& previous) {
                if (current == previous) return;
                onTagSelectionChanged();
            });

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

    connect(videoPlayer_, &VideoPlayer::positionChangedMs, this, [this](qint64 positionMs) {
        onPlayheadPositionChanged(positionMs);
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

    discardPendingClipNote();
    if (tagSession_) tagSession_->clear();
    hasPendingTag_ = false;
    pendingMainEvent_.clear();
    pendingTimestampMs_ = 0;
    contextPeriod_.clear();
    if (tagsModel_) tagsModel_->setRows({});

    if (videoPlayer_) {
        videoPlayer_->loadVideoFromFile(playbackPath);
        videoPlayer_->setControlsVisible(true);
        setVideoMuteButtonEnabled(true);
    }

    if (gameControls_) {
        gameControls_->resetGameTimeState();
        if (tagSession_) {
            gameControls_->setSessionTeamNames(tagSession_->homeTeamName(), tagSession_->awayTeamName(),
                                               tagSession_->homeTeamColor(), tagSession_->awayTeamColor());
            gameControls_->setInitialTeamSide(true);
        }
        contextTeam_ = "Home";
    }
    if (workOuterSplitter_) {
        workOuterSplitter_->show();
        QTimer::singleShot(0, this, &WorkWindow::applySidePanelCompressedWidth);
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

    applyModeChrome();
    updateFilterIndicator();
    if (statsWindow_) statsWindow_->setTagSession(tagSession_);

    rebuildFilterMenu();
    refreshTagsTableRows();
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
    if (mode_ == Mode::Presenting) {
        detachPresentationKeyboardShortcuts();
        presentationAutoPauseArmed_ = false;
    }
    if (videoPlayer_) videoPlayer_->setControlsVisible(false);
    setVideoMuteButtonEnabled(false);
    updateVideoMuteButton(false);
    if (gameControls_) gameControls_->resetGameTimeState();
    if (workOuterSplitter_) workOuterSplitter_->hide();
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
    if (tagsModel_) tagsModel_->setRows({});

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
    if (!tagsTable_ || !tagsModel_ || !tagsTable_->selectionModel()) return 0;
    const QModelIndex currentIndex = tagsTable_->selectionModel()->currentIndex();
    if (!currentIndex.isValid()) return 0;
    return tagsModel_->tagIdAt(currentIndex.row());
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

    const quint64 selectedId = selectedTagId();
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
    if (selectedId == 0 || !tagSession_) {
        if (!notesEdit_->toPlainText().isEmpty()) {
            notesEdit_->clear();
        }
        notesEdit_->setEnabled(false);
        notesEdit_->setPlaceholderText(AppLocale::trUi("notes.clip_placeholder_none"));
        notesEdit_->blockSignals(false);
        return;
    }

    int sessionIndex = tagSession_->indexOfTagId(selectedId);
    if (sessionIndex < 0 && tagsModel_ && tagsTable_ && tagsTable_->selectionModel()) {
        const QModelIndex currentIndex = tagsTable_->selectionModel()->currentIndex();
        if (currentIndex.isValid()) {
            sessionIndex = tagsModel_->tagSessionIndexAt(currentIndex.row());
        }
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
    if (!tagSession_ || !tagsTable_ || !tagsModel_) return;
    const int sessionIndex = tagSession_->indexOfTagId(tagId);
    if (!tagSession_->isValidTagIndex(sessionIndex)) return;

    const int row = tagsModel_->rowForTagId(tagId);
    if (row >= 0) {
        tagsTable_->selectRow(row);
        tagsTable_->scrollTo(tagsModel_->index(row, 0), QAbstractItemView::PositionAtCenter);
        onTagTableSeekToRow(row);
        return;
    }

    if (videoPlayer_) {
        videoPlayer_->seekToMs(tagSession_->tags().at(sessionIndex).markMs);
    }
}

void WorkWindow::onTagTableSeekToRow(int row) {
    if (row < 0 || !videoPlayer_ || !tagsModel_) return;
    videoPlayer_->seekToMs(tagsModel_->markMsAt(row));
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

void WorkWindow::flashNewTagRow() {
    if (!tagsModel_ || lastAddedTagId_ == 0) return;
    if (tagsModel_->rowForTagId(lastAddedTagId_) < 0) return;
    if (newTagFlashTimer_) {
        newTagFlashTimer_->stop();
    } else {
        newTagFlashTimer_ = new QTimer(this);
        newTagFlashTimer_->setSingleShot(true);
        connect(newTagFlashTimer_, &QTimer::timeout, this, &WorkWindow::clearNewTagFlash);
    }
    tagsModel_->setFlashTagId(lastAddedTagId_);
    newTagFlashTimer_->start(500);
}

void WorkWindow::clearNewTagFlash() {
    if (tagsModel_) {
        tagsModel_->setFlashTagId(0);
        if (videoPlayer_) tagsModel_->setPlayheadMs(videoPlayer_->currentPositionMs());
    }
}

void WorkWindow::onPlayheadPositionChanged(qint64 positionMs) {
    if (tagsModel_) tagsModel_->setPlayheadMs(positionMs);
}

void WorkWindow::onDeleteSelectedTag() {
    if (!tagsTable_ || !tagSession_ || !tagsModel_) return;

    const quint64 tagId = selectedTagId();
    if (tagId == 0) return;

    int tagIndex = tagSession_->indexOfTagId(tagId);
    if (tagIndex < 0 && tagsTable_->selectionModel()) {
        const QModelIndex currentIndex = tagsTable_->selectionModel()->currentIndex();
        if (currentIndex.isValid()) {
            tagIndex = tagsModel_->tagSessionIndexAt(currentIndex.row());
        }
    }
    if (tagIndex < 0 || !tagSession_->isValidTagIndex(tagIndex)) return;

    if (pendingNoteTagId_ != 0 && pendingNoteTagId_ == tagId) {
        discardPendingClipNote();
    } else {
        flushPendingClipNote();
    }
    tagSession_->removeTag(tagIndex);
}

void WorkWindow::onUndoLastTag() {
    if (!tagSession_) return;
    const int n = tagSession_->tags().size();
    if (n == 0) return;
    tagSession_->removeTag(n - 1);
}

void WorkWindow::onSelectAllFilters() {
    for (auto it = filterActionByMainEvent_.begin(); it != filterActionByMainEvent_.end(); ++it) {
        it.value()->setChecked(true);
    }
    refreshTagsTableRows();
    updateFilterIndicator();
    updateFilterButtonsVisibility();
}

void WorkWindow::onSelectNoFilters() {
    for (auto it = filterActionByMainEvent_.begin(); it != filterActionByMainEvent_.end(); ++it) {
        it.value()->setChecked(false);
    }
    refreshTagsTableRows();
}

void WorkWindow::onFilterActionToggled(bool /*checked*/) {
    refreshTagsTableRows();
    updateFilterIndicator();
    updateFilterButtonsVisibility();
}

void WorkWindow::onFilterByEventPathRequested(const QString& mainEvent, const QString& followUpEvent) {
    activeEventPathMainEvent_ = mainEvent;
    activeEventPathFollowUp_ = followUpEvent;
    refreshTagsTableRows();
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

void WorkWindow::refreshTagsTableRows() {
    if (!tagsTable_ || !tagsModel_) return;

    const quint64 previouslySelectedTagId = selectedTagId();
    const int previousVerticalScroll =
        tagsTable_->verticalScrollBar() ? tagsTable_->verticalScrollBar()->value() : 0;
    const QSignalBlocker tableBlocker(tagsTable_);

    if (!tagSession_) {
        tagsModel_->setRows({});
        refreshMatchNoteMentionCandidates();
        if (selectedTagId() != previouslySelectedTagId) {
            loadNoteForSelectedTag();
        }
        return;
    }

    struct TagEntry {
        TagSession::GameTag tag;
        int tagSessionIndex;
    };
    QVector<TagEntry> entries;
    int tagSessionIndex = 0;
    for (const TagSession::GameTag& tag : tagSession_->tags()) {
        if (isTagAllowed(tag.mainEvent, tag.followUpEvent)) {
            entries.append({tag, tagSessionIndex});
        }
        tagSessionIndex++;
    }

    std::sort(entries.begin(), entries.end(), [](const TagEntry& left, const TagEntry& right) {
        return left.tag.markMs < right.tag.markMs;
    });

    QVector<TagsTableModel::Row> rows;
    rows.reserve(entries.size());
    for (const TagEntry& entry : entries) {
        const TagSession::GameTag& tag = entry.tag;
        TagsTableModel::Row row;
        row.tagId = tag.id;
        row.tagSessionIndex = entry.tagSessionIndex;
        row.markMs = tag.markMs;
        row.teamKey = tag.team;
        row.timeText = formatTimestampMs(tag.markMs);
        row.teamText = displayTeamForTag(tag);
        row.eventText =
            AppLocale::trDisplayTagLine(tag.mainEvent, followUpForEventColumn(tag.followUpEvent, tagSession_));
        row.teamBackground = teamBackgroundForTag(tag, tagSession_);
        rows.append(row);
    }

    tagsModel_->setRows(rows);
    tagsTable_->resizeColumnToContents(0);
    tagsTable_->resizeColumnToContents(1);

    const int restoredRow = tagsModel_->rowForTagId(previouslySelectedTagId);
    if (restoredRow >= 0) {
        tagsTable_->selectRow(restoredRow);
        tagsTable_->scrollTo(tagsModel_->index(restoredRow, 0), QAbstractItemView::EnsureVisible);
        if (tagsTable_->verticalScrollBar()) {
            tagsTable_->verticalScrollBar()->setValue(previousVerticalScroll);
        }
    } else {
        tagsTable_->scrollToBottom();
    }

    if (videoPlayer_) {
        tagsModel_->setPlayheadMs(videoPlayer_->currentPositionMs());
    }

    updateFilterIndicator();
    updateFilterButtonsVisibility();
    refreshMatchNoteMentionCandidates();

    if (selectedTagId() != previouslySelectedTagId) {
        loadNoteForSelectedTag();
    }
}
