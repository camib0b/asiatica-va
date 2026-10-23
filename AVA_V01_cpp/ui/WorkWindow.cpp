#include "WorkWindow.h"
#include "NotesController.h"
#include "PlaybackTemporaryDirectoryController.h"
#include "PresentationPlaybackController.h"
#include "TagsTableController.h"
#include "TagsTableModel.h"
#include "ClipDurationSettingsDialog.h"
#include "PresentationPanel.h"
#include "../style/Motion.h"
#include "../style/StyleProps.h"
#include "../style/ThemeColors.h"
#include "../style/WorkDataView.h"
#include "../components/VideoPlayer.h"
#include "../components/GameControls.h"
#include "../components/TagToastOverlay.h"
#include "../components/MatchNotesEditor.h"
#include "../state/PresentationQueue.h"
#include "../state/EventLabelResolver.h"
#include "../state/TagSession.h"
#include "../state/Telestration.h"
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
#include "../export/SessionNotesWriter.h"
#include "../export/XmlImporter.h"
#include "ReplaceVideoConfirmation.h"
#include "TextInputFocus.h"
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
#include <QAbstractItemView>
#include <QHeaderView>
#include <QTableView>
#include <QItemSelectionModel>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QPropertyAnimation>
#include <QAction>
#include <QKeySequence>

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
#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <QDialog>
#include <QModelIndex>
#include <QSplitter>
#include <QSizePolicy>
#include <QHBoxLayout>
#include <QApplication>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QTextEdit>

namespace {

Motion::ButtonColors tagButtonColors() {
    Motion::ButtonColors colors;
    colors.restBackground = Style::ThemeColors::transparent();
    colors.restBorder = Style::ThemeColors::transparent();
    colors.hoverBackground = Style::ThemeColors::hoverSurface();
    colors.hoverBorder = Style::ThemeColors::transparent();
    colors.pressedBackground = Style::ThemeColors::pressedSurface();
    colors.pressedBorder = Style::ThemeColors::transparent();
    return colors;
}

void configureModeToolButton(QToolButton* button, const QString& objectName, bool checked) {
    button->setObjectName(objectName);
    button->setCheckable(true);
    button->setChecked(checked);
    Style::setVariant(button, "ghost");
    Style::setSize(button, "sm");
    button->setCursor(Qt::PointingHandCursor);
}

QString pathWithXmlSuffix(const QString& chosenPath) {
    const QFileInfo chosenInfo(chosenPath);
    if (chosenInfo.suffix().compare(QStringLiteral("xml"), Qt::CaseInsensitive) == 0) {
        return chosenPath;
    }
    return QDir(chosenInfo.absolutePath())
        .filePath(chosenInfo.completeBaseName() + QStringLiteral(".xml"));
}

} // namespace

class ModeLabelTint final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QColor textColor READ textColor WRITE setTextColor)

public:
    explicit ModeLabelTint(QToolButton* button) : QObject(button), button_(button) {
        setObjectName(QStringLiteral("motion.modeLabel"));
    }

    QColor textColor() const { return textColor_; }

    void setTextColor(const QColor& color) {
        if (textColor_ == color) return;
        textColor_ = color;
        if (!button_) return;
        const QString selector = button_->objectName().isEmpty()
            ? QStringLiteral("QToolButton")
            : QStringLiteral("QToolButton#%1").arg(button_->objectName());
        button_->setStyleSheet(QStringLiteral("%1 { color: %2; }").arg(selector, color.name(QColor::HexRgb)));
    }

private:
    QPointer<QToolButton> button_;
    QColor textColor_;
};

class ModeToggleTrack final : public QWidget {
    Q_OBJECT
    Q_PROPERTY(QRectF pillRect READ pillRect WRITE setPillRect)

public:
    explicit ModeToggleTrack(QWidget* parent = nullptr) : QWidget(parent) {
        setObjectName(QStringLiteral("WorkModeToggleGroup"));
        setAttribute(Qt::WA_StyledBackground, true);
    }

    void setButtons(QToolButton* tagging, QToolButton* analyzing, QToolButton* presenting) {
        taggingButton_ = tagging;
        analyzingButton_ = analyzing;
        presentingButton_ = presenting;
        tintFor(tagging);
        tintFor(analyzing);
        tintFor(presenting);
    }

    void syncToMode(bool animate) {
        syncLabels(animate);
        syncPillGeometry(animate);
    }

    QRectF pillRect() const { return pillRect_; }

    void setPillRect(const QRectF& rect) {
        if (pillRect_ == rect) return;
        pillRect_ = rect;
        update();
    }

protected:
    void paintEvent(QPaintEvent* event) override {
        QWidget::paintEvent(event);
        if (pillRect_.width() < 1.0) return;
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(QPen(Style::ThemeColors::border(), 1.0));
        painter.setBrush(Style::ThemeColors::surface());
        painter.drawRoundedRect(pillRect_.adjusted(0.5, 0.5, -0.5, -0.5), 6.0, 6.0);
    }

    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        auto* animation = findChild<QPropertyAnimation*>(QStringLiteral("motion.property.pillRect"),
                                                        Qt::FindDirectChildrenOnly);
        const bool sliding = animation && animation->state() == QAbstractAnimation::Running;
        syncPillGeometry(sliding);
    }

private:
    ModeLabelTint* tintFor(QToolButton* button) {
        if (!button) return nullptr;
        auto* tint = button->findChild<ModeLabelTint*>(QStringLiteral("motion.modeLabel"),
                                                       Qt::FindDirectChildrenOnly);
        if (!tint) tint = new ModeLabelTint(button);
        return tint;
    }

    QToolButton* activeButton() const {
        if (taggingButton_ && taggingButton_->isChecked()) return taggingButton_;
        if (analyzingButton_ && analyzingButton_->isChecked()) return analyzingButton_;
        if (presentingButton_ && presentingButton_->isChecked()) return presentingButton_;
        return taggingButton_;
    }

    QRectF rectForButton(QToolButton* button) const {
        if (!button || !button->isVisible()) return {};
        return QRectF(button->geometry()).adjusted(1.0, 1.0, -1.0, -1.0);
    }

    void syncLabels(bool animate) {
        const int duration = animate ? Motion::kHoverIn : 0;
        const QList<QPointer<QToolButton>> buttons = {taggingButton_, analyzingButton_, presentingButton_};
        for (const QPointer<QToolButton>& button : buttons) {
            if (!button) continue;
            ModeLabelTint* tint = tintFor(button);
            if (!tint) continue;
            const QColor color = button->isChecked() ? Style::ThemeColors::foreground()
                                                     : Style::ThemeColors::quietText();
            Motion::animateColor(tint, "textColor", color, duration);
        }
    }

    void syncPillGeometry(bool animate) {
        const QRectF target = rectForButton(activeButton());
        const QEasingCurve curve = Motion::strongOut();
        if (target.width() < 1.0) {
            if (pillRect_.width() >= 1.0) {
                Motion::animateProperty(this, "pillRect", QVariant::fromValue(QRectF()), 0, curve);
            }
            return;
        }
        const bool instant = !animate || !pillPlaced_ || Motion::reduced();
        Motion::animateProperty(this,
                                "pillRect",
                                QVariant::fromValue(target),
                                instant ? 0 : Motion::kEnter,
                                curve);
        pillPlaced_ = true;
    }

    QPointer<QToolButton> taggingButton_;
    QPointer<QToolButton> analyzingButton_;
    QPointer<QToolButton> presentingButton_;
    QRectF pillRect_;
    bool pillPlaced_ = false;
};

WorkWindow::WorkWindow(QWidget* parent)
    : QWidget(parent),
      playbackTemporaryDirectoryController_(std::make_unique<PlaybackTemporaryDirectoryController>()),
      notesController_(std::make_unique<NotesController>()),
      tagsTableController_(std::make_unique<TagsTableController>()),
      presentationPlaybackController_(std::make_unique<PresentationPlaybackController>()) {
    tagsTableController_->setNotesController(notesController_.get());
    presentationPlaybackController_->setHostWindow(this);
    presentationPlaybackController_->setPresentingQuery([this] { return mode_ == Mode::Presenting; });
    connect(presentationPlaybackController_.get(),
            &PresentationPlaybackController::leavePresentationRequested, this,
            &WorkWindow::leavePresentingForVideoChange, Qt::DirectConnection);
    setAttribute(Qt::WA_StyledBackground, true);
    buildUi();
    telestrationSession_ = new Telestration::Session(this);
    videoPlayer_->setTelestrationSession(telestrationSession_);
    videoPlayer_->setDrawingTagResolver([this](qint64 positionMs) {
        if (tagSession_ == nullptr) return std::optional<quint64>{};
        return Telestration::tagIdCoveringPosition(*tagSession_, positionMs);
    });
    presentationPlaybackController_->setTelestrationSession(telestrationSession_);
    wireSignals();
    applyModeChrome();
    applyUiStrings();
}

WorkWindow::~WorkWindow() {
    notesController_->flushPendingClipNote();
    presentationPlaybackController_->detachPresentationKeyboardShortcuts();
    disconnectTagSessionSignals();
    if (exportJobManager_) {
        exportJobManager_->cancelExportingJobs();
    }
    playbackTemporaryDirectoryController_->releaseForShutdown();
    tagSession_ = nullptr;
    notesController_->setTagSession(nullptr);
    tagsTableController_->setTagSession(nullptr);
}

bool WorkWindow::shouldDeliverPlaybackKeyboardToVideoPlayer(const QWidget* focusWidget) const {
    if (!hasOpenVideo()) return false;
    if (!videoPlayer_ || !videoPlayer_->isMediaKeyboardControlActive()) return false;
    if (!focusWidget) return true;
    if (focusWidget->window() != window()) return false;
    if (!isAncestorOf(focusWidget)) return false;
    if (mode_ == Mode::Analyzing && notesController_->isClipNotesEditor(focusWidget)) return false;
    if (TextInputFocus::widgetIsTextInput(focusWidget)) return false;
    return true;
}

void WorkWindow::onApplicationFocusWidgetChanged(QWidget* /*oldFocus*/, QWidget* newFocus) {
    if (!videoPlayer_) {
        return;
    }
    const bool allowPlaybackShortcuts = shouldDeliverPlaybackKeyboardToVideoPlayer(newFocus);
    videoPlayer_->setPlaybackShortcutFocusGate(allowPlaybackShortcuts);
}

void WorkWindow::refreshPlaybackShortcutFocusGate() {
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

void WorkWindow::setVideoMuteButtonEnabled(bool enabled) {
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

void WorkWindow::setConcatenatedVideoTemporaryDirectory(std::unique_ptr<QTemporaryDir> directory) {
    playbackTemporaryDirectoryController_->setConcatenatedVideoTemporaryDirectory(std::move(directory));
}

void WorkWindow::setExportDefaultDirectoryFromVideoPath(const QString& videoPath) {
    exportDefaultDirectoryPath_.clear();
    if (videoPath.trimmed().isEmpty()) return;

    const QFileInfo videoInfo(videoPath);
    exportDefaultDirectoryPath_ = videoInfo.absolutePath();
}

void WorkWindow::setPendingConcatenation(std::unique_ptr<VideoConcatenator> concatenator) {
    playbackTemporaryDirectoryController_->setPendingConcatenation(std::move(concatenator));
}

void WorkWindow::releaseTransientResources() {
    playbackTemporaryDirectoryController_->releaseTransientResources();
}

bool WorkWindow::confirmExportAllowsVideoRelease() {
    if (!exportJobManager_ || !exportJobManager_->hasExportingJob()) return true;

    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(AppLocale::trUi("export.busy_title"));
    box.setText(AppLocale::trUi("export.busy_message"));
    auto* waitButton = box.addButton(AppLocale::trUi("export.busy_wait"), QMessageBox::AcceptRole);
    auto* cancelExportButton =
        box.addButton(AppLocale::trUi("export.busy_cancel"), QMessageBox::DestructiveRole);
    box.setDefaultButton(waitButton);
    box.setEscapeButton(waitButton);
    const QString message = box.text();
    const auto labels = box.findChildren<QLabel*>();
    for (QLabel* label : labels) {
        if (label->text() != message) continue;
        label->setWordWrap(true);
        label->setMaximumWidth(460);
    }
    box.exec();
    if (box.clickedButton() != cancelExportButton) return false;
    exportJobManager_->cancelExportingJobs();
    return true;
}

void WorkWindow::showXmlImportAdjustmentNotice(int adjustedCount) {
    if (adjustedCount <= 0 || !tagsSection_) return;

    auto* label = tagsSection_->findChild<QLabel*>(QStringLiteral("ImportAdjustmentNotice"),
                                                   Qt::FindDirectChildrenOnly);
    if (!label) {
        label = new QLabel(tagsSection_);
        label->setObjectName(QStringLiteral("ImportAdjustmentNotice"));
        label->setWordWrap(true);
        label->setMinimumWidth(0);
        label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
        Style::setRole(label, "muted");
        if (auto* layout = qobject_cast<QVBoxLayout*>(tagsSection_->layout())) {
            layout->insertWidget(1, label);
        }
    }

    const char* messageKey = adjustedCount == 1
        ? "xml_import.adjusted_before_start_one"
        : "xml_import.adjusted_before_start_many";
    label->setText(AppLocale::trUi(messageKey).arg(adjustedCount));
    label->show();

    auto* timer = label->findChild<QTimer*>(QStringLiteral("hideTimer"), Qt::FindDirectChildrenOnly);
    if (!timer) {
        timer = new QTimer(label);
        timer->setObjectName(QStringLiteral("hideTimer"));
        timer->setSingleShot(true);
        connect(timer, &QTimer::timeout, label, [guard = QPointer<QLabel>(label)]() {
            if (guard) guard->hide();
        });
    }
    timer->start(8000);
}

void WorkWindow::abortVideoOpen(const QString& errorMessage) {
    if (!errorMessage.isEmpty()) {
        QMessageBox::warning(this, AppLocale::trUi("app.title"), errorMessage);
    }
    onCloseVideo();
}

void WorkWindow::applyUiStrings() const {
    if (modeTaggingButton_) {
        modeTaggingButton_->setText(AppLocale::trUi("mode.tagging"));
    }
    if (modeAnalyzingButton_) {
        modeAnalyzingButton_->setText(AppLocale::trUi("mode.analyzing"));
    }
    if (modePresentingButton_) {
        modePresentingButton_->setText(AppLocale::trUi("mode.presenting"));
    }
    if (replaceVideoAction_) replaceVideoAction_->setText(AppLocale::trUi("menu.replace_video"));
    if (closeVideoAction_) closeVideoAction_->setText(AppLocale::trUi("menu.close_video"));
    if (importXmlAction_) importXmlAction_->setText(AppLocale::trUi("menu.import_xml"));
    if (exportXmlAction_) exportXmlAction_->setText(AppLocale::trUi("menu.export_xml"));
    if (clipDurationSettingsAction_) {
        clipDurationSettingsAction_->setText(AppLocale::trUi("menu.clip_durations"));
    }
    if (reduceMotionAction_) {
        reduceMotionAction_->setText(AppLocale::trUi("menu.reduce_motion"));
        reduceMotionAction_->setToolTip(AppLocale::trUi("menu.reduce_motion_tooltip"));
    }
    if (tagsHeaderLabel_) tagsHeaderLabel_->setText(AppLocale::trUi("tags.header"));
    if (tagsFilterButton_) tagsFilterButton_->setText(AppLocale::trUi("tags.filter"));
    if (tagsRemoveFiltersButton_) tagsRemoveFiltersButton_->setText(AppLocale::trUi("tags.remove_filters"));
    if (undoLastTagButton_) {
        undoLastTagButton_->setText(AppLocale::trUi("tags.undo"));
        undoLastTagButton_->setToolTip(AppLocale::trUi("tags.undo_tooltip"));
    }
    notesController_->applyUiStrings();
    if (tagsModel_) {
        tagsModel_->setColumnHeaders({AppLocale::trUi("tags.col_time"), AppLocale::trUi("tags.col_team"),
                                      AppLocale::trUi("tags.col_event")});
    }
    if (videoMuteButton_) {
        updateVideoMuteButton(videoPlayer_ && videoPlayer_->controlsBar()
                                  ? videoPlayer_->controlsBar()->muted()
                                  : false);
    }
    if (filterSelectAllAction_) filterSelectAllAction_->setText(AppLocale::trUi("filter.select_all"));
    if (filterSelectNoneAction_) filterSelectNoneAction_->setText(AppLocale::trUi("filter.select_none"));
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
    presentationPlaybackController_->updatePresentationStage();
    tagsTableController_->syncFilterMenu();
    tagsTableController_->refreshTagsTableRows();
}

void WorkWindow::disconnectTagSessionSignals() {
    for (const QMetaObject::Connection& connection : tagSessionConnections_) {
        QObject::disconnect(connection);
    }
    tagSessionConnections_.clear();
    notesController_->disconnectTagSessionSignals();
}

void WorkWindow::applySessionTeamIdentity() {
    if (gameControls_) {
        if (tagSession_) {
            gameControls_->setSessionTeamNames(tagSession_->homeTeamName(), tagSession_->awayTeamName(),
                                               tagSession_->homeTeamColor(), tagSession_->awayTeamColor());
        } else {
            gameControls_->setSessionTeamNames(QString(), QString(), QString(), QString());
        }
    }
    tagsTableController_->refreshTagsTableRows();
    presentationPlaybackController_->updatePresentationStage();
    if (presentationPanel_) presentationPanel_->refreshFromSession();
}

void WorkWindow::setTagSession(TagSession* session) {
    if (tagSession_ == session) return;

    notesController_->flushPendingClipNote();
    disconnectTagSessionSignals();

    if (telestrationSession_) telestrationSession_->clear();
    tagSession_ = session;
    notesController_->setTagSession(session);
    tagsTableController_->setTagSession(session);
    bindTagSessionToSurfaces();
    if (gameControls_) {
        gameControls_->setTeamSide(tagSession_ ? TagSession::TeamSide::Home
                                               : TagSession::TeamSide::Unspecified);
    }
    tagsTableController_->syncFilterMenu();
    applySessionTeamIdentity();
    notesController_->loadMatchNote();

    if (tagSession_) connectTagSessionSignals();
    pushEventLabelOverridesToGameControls();
}

void WorkWindow::pushEventLabelOverridesToGameControls() {
    if (!gameControls_) return;
    if (tagSession_) {
        gameControls_->setEventLabelOverrides(tagSession_->eventLabelOverrides());
    } else {
        gameControls_->setEventLabelOverrides(EventLabelOverrides::Table{});
    }
}

void WorkWindow::onEventLabelOverridesChanged() {
    pushEventLabelOverridesToGameControls();
    tagsTableController_->syncFilterMenu();
    tagsTableController_->refreshTagsTableRows();
}

void WorkWindow::bindTagSessionToSurfaces() {
    if (statsWindow_) statsWindow_->setTagSession(tagSession_);
    if (statsOverlay_) statsOverlay_->setTagSession(tagSession_);
    // Stage text reads the session while the queue emits during setTagSession.
    presentationPlaybackController_->setTagSession(tagSession_);
    if (presentationQueue_) presentationQueue_->setTagSession(tagSession_);
    if (presentationPanel_) presentationPanel_->setTagSession(tagSession_);
}

void WorkWindow::connectTagSessionSignals() {
    notesController_->connectTagSessionSignals();
    tagSessionConnections_.append(connect(
        tagSession_, &TagSession::tagsChanged, this, &WorkWindow::onTagSessionTagsChanged));
    tagSessionConnections_.append(connect(
        tagSession_, &TagSession::tagsImported, this, &WorkWindow::onTagSessionTagsImported));
    tagSessionConnections_.append(connect(
        tagSession_, &TagSession::gameMetadataChanged, this, &WorkWindow::onTagSessionGameMetadataChanged));
    tagSessionConnections_.append(connect(
        tagSession_, &TagSession::eventLabelOverridesChanged, this,
        &WorkWindow::onEventLabelOverridesChanged));
}

void WorkWindow::onTagSessionTagsChanged(quint64 addedTagId) {
    if (!tagSession_) return;
    tagsTableController_->syncFilterMenu();
    tagsTableController_->refreshTagsTableRows();
    if (addedTagId != 0) flashNewTagRow(addedTagId);
    if (gameControls_) {
        gameControls_->restoreGamePhase(tagSession_->quarterPhase(),
                                        tagSession_->currentQuarterIndex());
    }
}

void WorkWindow::onTagSessionTagsImported() {
    if (!tagSession_) return;
    if (gameControls_) {
        gameControls_->restoreGamePhase(tagSession_->quarterPhase(),
                                        tagSession_->currentQuarterIndex());
    }
}

void WorkWindow::onTagSessionGameMetadataChanged() {
    if (!tagSession_) return;
    applySessionTeamIdentity();
}

void WorkWindow::syncModeToggleChecks() {
    setModeToggleButtonsChecked();
    if (modeToggleTrack_) modeToggleTrack_->syncToMode(false);
}

void WorkWindow::setModeToggleButtonsChecked() {
    if (modeTaggingButton_) modeTaggingButton_->setChecked(mode_ == Mode::Tagging);
    if (modeAnalyzingButton_) modeAnalyzingButton_->setChecked(mode_ == Mode::Analyzing);
    if (modePresentingButton_) modePresentingButton_->setChecked(mode_ == Mode::Presenting);
}

void WorkWindow::setMode(Mode mode) {
    if (mode_ == mode) {
        syncModeToggleChecks();
        return;
    }
    notesController_->flushPendingClipNote();
    if (mode_ == Mode::Presenting && mode != Mode::Presenting) {
        presentationPlaybackController_->leavePresentationPlayback();
    }
    mode_ = mode;
    applyModeChrome();
    if (mode == Mode::Presenting) {
        if (presentationPanel_) presentationPanel_->refreshFromSession();
        presentationPlaybackController_->enterPresentationPlayback();
    }
    setModeToggleButtonsChecked();
    if (modeToggleTrack_) modeToggleTrack_->syncToMode(true);
    refreshPlaybackShortcutFocusGate();
}

void WorkWindow::leavePresentingForVideoChange() {
    // Closing or loading a video leaves Presenting. The next video would otherwise
    // open there with the Tab filter already removed, and setMode would not install it again.
    mode_ = Mode::Tagging;
    presentationPlaybackController_->detachPresentationKeyboardShortcuts();
    applyModeChrome();
    syncModeToggleChecks();
}

void WorkWindow::applyModeChrome() {
    const bool isTagging = mode_ == Mode::Tagging;
    const bool isAnalyzing = mode_ == Mode::Analyzing;
    const bool isPresenting = mode_ == Mode::Presenting;

    updatePresentationChromeVisibility(isPresenting);
    placeNotesColumnForMode(isTagging, isAnalyzing);
    if (tagsHeaderRow_) tagsHeaderRow_->setVisible(isAnalyzing);
    if (workOuterSplitter_) workOuterSplitter_->setChildrenCollapsible(isPresenting);
    showModeSidePage(isTagging, isAnalyzing, isPresenting);
    applyModeSizePolicies(isTagging, isAnalyzing, isPresenting);
    placeSidePanelForMode(isTagging);
    applySidePanelCompressedWidth();
    updateTagToastForMode(isTagging);
    if (videoPlayer_) videoPlayer_->setPresentationDrawingsActive(isPresenting);
}

void WorkWindow::updatePresentationChromeVisibility(bool isPresenting) {
    if (presentationBanner_) presentationBanner_->setVisible(isPresenting);
    if (presentationClipBar_) presentationClipBar_->setVisible(isPresenting);
    if (workTagsNotesSplitter_) workTagsNotesSplitter_->setVisible(!isPresenting);
}

void WorkWindow::placeNotesColumnForMode(bool isTagging, bool isAnalyzing) {
    if (!notesColumn_) return;
    const bool showNotes = isTagging || isAnalyzing;
    notesColumn_->setVisible(showNotes);

    if (isTagging && taggingSideColumn_) {
        if (workTagsNotesSplitter_ && workTagsNotesSplitter_->indexOf(notesColumn_) >= 0) {
            notesColumn_->setParent(nullptr);
        }
        if (auto* sideLayout = qobject_cast<QVBoxLayout*>(taggingSideColumn_->layout())) {
            if (sideLayout->indexOf(notesColumn_) < 0) {
                notesColumn_->setParent(taggingSideColumn_);
                sideLayout->addWidget(notesColumn_, 1);
            }
        }
    } else if (isAnalyzing && workTagsNotesSplitter_) {
        if (taggingSideColumn_) {
            if (auto* sideLayout = qobject_cast<QVBoxLayout*>(taggingSideColumn_->layout())) {
                if (sideLayout->indexOf(notesColumn_) >= 0) {
                    sideLayout->removeWidget(notesColumn_);
                }
            }
        }
        if (workTagsNotesSplitter_->indexOf(notesColumn_) < 0) {
            workTagsNotesSplitter_->addWidget(notesColumn_);
            workTagsNotesSplitter_->setStretchFactor(0, 1);
            workTagsNotesSplitter_->setStretchFactor(1, 1);
        }
    }
}

void WorkWindow::showModeSidePage(bool isTagging, bool isAnalyzing, bool isPresenting) {
    if (!workSideStack_) return;
    // Every mode owns one page. Leaving this unset keeps the previous
    // page, so returning to tagging still shows the analyzing panel.
    QWidget* page = nullptr;
    if (isTagging) page = taggingSideColumn_;
    else if (isAnalyzing) page = statsWindow_;
    else if (isPresenting) page = presentationPanel_;
    if (page) workSideStack_->setCurrentWidget(page);
}

void WorkWindow::applyModeSizePolicies(bool isTagging, bool isAnalyzing, bool isPresenting) {
    // Preferred can shrink to the longest button word. Minimum treated each
    // button's full line as a hard floor, which locked this column near 472px.
    // Hidden stack pages are Ignored so stats and presentation cannot hold
    // the tagging column open.
    if (gameControls_) {
        gameControls_->setMinimumWidth(GameControls::kMinimumPanelWidthPx);
        gameControls_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    }
    if (notesColumn_) {
        notesColumn_->setMinimumWidth(0);
        notesColumn_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    }
    if (taggingSideColumn_) {
        taggingSideColumn_->setSizePolicy(isTagging ? QSizePolicy::Preferred : QSizePolicy::Ignored,
                                        QSizePolicy::Preferred);
    }
    if (statsWindow_) {
        statsWindow_->setSizePolicy(isAnalyzing ? QSizePolicy::Preferred : QSizePolicy::Ignored,
                                    QSizePolicy::Preferred);
    }
    if (presentationPanel_) {
        presentationPanel_->setSizePolicy(isPresenting ? QSizePolicy::Preferred : QSizePolicy::Ignored,
                                          QSizePolicy::Preferred);
    }
    if (workSideStack_) {
        workSideStack_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        if (isTagging) {
            workSideStack_->setMaximumWidth(kMaximumTaggingSidePanelWidthPx);
        } else {
            workSideStack_->setMaximumWidth(QWIDGETSIZE_MAX);
        }
    }
}

void WorkWindow::placeSidePanelForMode(bool isTagging) {
    // Tagging keeps the event column on the left, next to the hand on the keys.
    // Analyzing and presenting put their side panel back on the right.
    if (!workOuterSplitter_ || !workSideStack_ || workOuterSplitter_->indexOf(workSideStack_) < 0) return;
    const bool onLeft = isTagging;
    const int desiredIndex = onLeft ? 0 : 1;
    if (workOuterSplitter_->indexOf(workSideStack_) != desiredIndex) {
        workOuterSplitter_->insertWidget(desiredIndex, workSideStack_);
    }
    if (onLeft) {
        workOuterSplitter_->setStretchFactor(0, 0);
        workOuterSplitter_->setStretchFactor(1, 4);
    } else {
        workOuterSplitter_->setStretchFactor(0, 4);
        workOuterSplitter_->setStretchFactor(1, 1);
    }
}

void WorkWindow::updateTagToastForMode(bool isTagging) {
    if (!tagToast_) return;
    if (isTagging) tagToast_->resumeOpenChain();
    else tagToast_->suspend();
}

void WorkWindow::applySidePanelCompressedWidth() {
    if (!workOuterSplitter_ || !workOuterSplitter_->isVisible()) return;

    const int sideMinimumWidth = GameControls::kMinimumPanelWidthPx;
    const int totalWidth = workOuterSplitter_->width();
    if (totalWidth <= sideMinimumWidth) return;

    const bool sideOnLeft = workSideStack_ && workOuterSplitter_->indexOf(workSideStack_) == 0;
    const int sideWidth = sideMinimumWidth;
    const int videoWidth = qMax(0, totalWidth - sideWidth);
    if (sideOnLeft) {
        workOuterSplitter_->setSizes({sideWidth, videoWidth});
    } else {
        workOuterSplitter_->setSizes({videoWidth, sideWidth});
    }
    enforceTaggingSidePanelWidthCap();
}

void WorkWindow::enforceTaggingSidePanelWidthCap() {
    if (!workOuterSplitter_ || mode_ != Mode::Tagging || !workSideStack_) return;

    const int sideIndex = workOuterSplitter_->indexOf(workSideStack_);
    if (sideIndex < 0) return;

    QList<int> sizes = workOuterSplitter_->sizes();
    if (sideIndex >= sizes.size()) return;
    if (sizes.at(sideIndex) <= kMaximumTaggingSidePanelWidthPx) return;

    const int excess = sizes.at(sideIndex) - kMaximumTaggingSidePanelWidthPx;
    sizes[sideIndex] = kMaximumTaggingSidePanelWidthPx;
    const int videoIndex = sideIndex == 0 ? 1 : 0;
    if (videoIndex < sizes.size()) {
        sizes[videoIndex] += excess;
    }
    workOuterSplitter_->setSizes(sizes);
}

WorkWindow::PendingTagPeriodAndTeam WorkWindow::pendingTagPeriodAndTeam() const {
    PendingTagPeriodAndTeam context;
    if (tagSession_) context.period = tagSession_->currentPeriodLabel();
    if (gameControls_) context.team = gameControls_->selectedTeamSide();
    return context;
}

void WorkWindow::buildUi() {
    setObjectName("AppRoot");

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    contentStack_ = new QStackedWidget(this);
    mainContentContainer_ = new QWidget(this);
    auto* mainContentLayout = new QVBoxLayout(mainContentContainer_);
    mainContentLayout->setContentsMargins(12, 12, 12, 12);
    mainContentLayout->setSpacing(4);
    mainContentLayout->addWidget(buildTopToolbar(mainContentContainer_));

    auto* contentArea = new QWidget(mainContentContainer_);
    auto* contentAreaLayout = new QVBoxLayout(contentArea);
    contentAreaLayout->setContentsMargins(0, 0, 0, 0);
    contentAreaLayout->setSpacing(0);
    mainContentLayout->addWidget(contentArea, 1);

    gameSetupWidget_ = new GameSetupWindow(this);
    contentStack_->addWidget(gameSetupWidget_);
    contentStack_->addWidget(mainContentContainer_);
    contentStack_->setCurrentIndex(0);

    rootLayout->addWidget(contentStack_);
    buildExportJobsBar(rootLayout);
    buildTaggingSideColumn();
    buildTagsSection();
    buildGameControlsAndStatsWindow();
    buildNotesColumn();
    buildVideoStageColumn();
    assembleWorkSplitters(contentAreaLayout);
    buildPresentationUi();
    hideChromeUntilVideoOpens();
}

QWidget* WorkWindow::buildTopToolbar(QWidget* parent) {
    auto* topRow = new QWidget(parent);
    auto* topLayout = new QHBoxLayout(topRow);
    topLayout->setContentsMargins(0, 0, 0, 0);
    topLayout->setSpacing(12);
    buildModeToggle(topRow, topLayout);
    buildVideoToolbar(topLayout);
    return topRow;
}

void WorkWindow::buildModeToggle(QWidget* topRow, QHBoxLayout* topLayout) {
    modeToggleTrack_ = new ModeToggleTrack(topRow);

    modeTaggingButton_ = new QToolButton(modeToggleTrack_);
    configureModeToolButton(modeTaggingButton_, QStringLiteral("WorkModeTaggingButton"), true);

    modeAnalyzingButton_ = new QToolButton(modeToggleTrack_);
    configureModeToolButton(modeAnalyzingButton_, QStringLiteral("WorkModeAnalyzingButton"), false);

    modePresentingButton_ = new QToolButton(modeToggleTrack_);
    configureModeToolButton(modePresentingButton_, QStringLiteral("WorkModePresentingButton"), false);

    auto* modeToggleLayout = new QHBoxLayout(modeToggleTrack_);
    modeToggleLayout->setContentsMargins(3, 3, 3, 3);
    modeToggleLayout->setSpacing(4);
    modeToggleLayout->addWidget(modeTaggingButton_);
    modeToggleLayout->addWidget(modeAnalyzingButton_);
    modeToggleLayout->addWidget(modePresentingButton_);
    modeToggleTrack_->setButtons(modeTaggingButton_, modeAnalyzingButton_, modePresentingButton_);
    modeToggleTrack_->syncToMode(false);
    topLayout->addWidget(modeToggleTrack_);
    topLayout->addSpacing(16);
}

void WorkWindow::buildVideoToolbar(QHBoxLayout* topLayout) {
    auto* videoControlsRow = new QWidget(this);
    auto* videoControlsLayout = new QHBoxLayout(videoControlsRow);
    videoControlsLayout->setContentsMargins(0, 0, 0, 0);
    videoControlsLayout->setSpacing(8);
    videoControlsLayout->addStretch(1);
    buildVideoMuteButton(videoControlsLayout);
    buildVideoSettingsMenu(videoControlsLayout);
    topLayout->addWidget(videoControlsRow, 1);
}

void WorkWindow::buildVideoMuteButton(QHBoxLayout* videoControlsLayout) {
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
}

void WorkWindow::buildVideoSettingsMenu(QHBoxLayout* videoControlsLayout) {
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
    auto* videoMenu = new QMenu(videoMenuButton_);
    replaceVideoAction_ = videoMenu->addAction(QString());
    closeVideoAction_ = videoMenu->addAction(QString());
    videoMenu->addSeparator();
    importXmlAction_ = videoMenu->addAction(QString());
    exportXmlAction_ = videoMenu->addAction(QString());
    clipDurationSettingsAction_ = videoMenu->addAction(QString());
    videoMenu->addSeparator();
    reduceMotionAction_ = videoMenu->addAction(QString());
    reduceMotionAction_->setCheckable(true);
    reduceMotionAction_->setChecked(Motion::reducedMotionPreferred());
    videoMenuButton_->setMenu(videoMenu);
    videoControlsLayout->addWidget(videoMenuButton_, 0, Qt::AlignRight | Qt::AlignVCenter);
}

void WorkWindow::buildExportJobsBar(QVBoxLayout* rootLayout) {
    exportJobManager_ = new ExportJobManager(this);
    playbackTemporaryDirectoryController_->setExportJobManager(exportJobManager_);
    exportJobsBar_ = new ExportJobsBar(exportJobManager_, this);
    rootLayout->addWidget(exportJobsBar_, 0);
}

void WorkWindow::buildTaggingSideColumn() {
    taggingSideColumn_ = new QWidget(this);
    // Object name stays TaggingRightCol so the compact-button QSS keeps matching
    // after the column moves to the left of the video.
    taggingSideColumn_->setObjectName(QStringLiteral("TaggingRightCol"));
    auto* taggingSideColumnLayout = new QVBoxLayout(taggingSideColumn_);
    taggingSideColumnLayout->setContentsMargins(0, 0, 0, 0);
}

void WorkWindow::buildTagsSection() {
    tagsSection_ = new QWidget(this);
    auto* tagsSectionLayout = new QVBoxLayout(tagsSection_);
    tagsSectionLayout->setContentsMargins(0, 0, 0, 0);
    tagsSectionLayout->setSpacing(6);
    buildTagsHeaderRow(tagsSectionLayout);
    buildTagsTable(tagsSectionLayout);
}

void WorkWindow::buildTagsHeaderRow(QVBoxLayout* tagsSectionLayout) {
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
    buildTagsFilterMenu();

    tagsFilterIndicator_ = new QLabel(tagsHeaderRow_);
    tagsFilterIndicator_->setWordWrap(false);
    Style::setRole(tagsFilterIndicator_, "muted");
    tagsFilterIndicator_->hide();

    undoLastTagButton_ = new QToolButton(tagsHeaderRow_);
    Style::setVariant(undoLastTagButton_, "ghost");
    Style::setSize(undoLastTagButton_, "sm");
    undoLastTagButton_->setCursor(Qt::PointingHandCursor);
    Motion::installButtonHover(tagsFilterButton_, tagButtonColors());
    Motion::installButtonHover(tagsRemoveFiltersButton_, tagButtonColors());
    Motion::installButtonHover(undoLastTagButton_, tagButtonColors());

    tagsHeaderLayout->addWidget(tagsHeaderLabel_, 0);
    tagsHeaderLayout->addStretch(1);
    tagsHeaderLayout->addWidget(tagsFilterIndicator_, 0);
    tagsHeaderLayout->addWidget(undoLastTagButton_, 0);
    tagsHeaderLayout->addWidget(tagsRemoveFiltersButton_, 0);
    tagsHeaderLayout->addWidget(tagsFilterButton_, 0);
    tagsSectionLayout->addWidget(tagsHeaderRow_);
    tagsTableController_->setFilterWidgets(tagsFilterMenu_,
                                           filterSelectAllAction_,
                                           filterSelectNoneAction_,
                                           tagsFilterIndicator_,
                                           tagsRemoveFiltersButton_);
}

void WorkWindow::buildTagsFilterMenu() {
    tagsFilterMenu_ = new QMenu(tagsFilterButton_);
    tagsFilterMenu_->setObjectName(QStringLiteral("TagsFilterMenu"));
    tagsFilterButton_->setMenu(tagsFilterMenu_);
    filterSelectAllAction_ = tagsFilterMenu_->addAction(QString());
    filterSelectNoneAction_ = tagsFilterMenu_->addAction(QString());
    tagsFilterMenu_->addSeparator();
}

void WorkWindow::buildTagsTable(QVBoxLayout* tagsSectionLayout) {
    tagsModel_ = new TagsTableModel(this);
    tagsModel_->setColumnHeaders({AppLocale::trUi("tags.col_time"), AppLocale::trUi("tags.col_team"),
                                  AppLocale::trUi("tags.col_event")});
    tagsTable_ = new QTableView(tagsSection_);
    tagsTable_->setObjectName("TagsTable");
    tagsTable_->setModel(tagsModel_);
    Style::configureWorkDataView(tagsTable_);
    tagsTable_->horizontalHeader()->setStretchLastSection(true);
    tagsTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    tagsTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    tagsTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    const int rowHeight = Style::workDataViewRowHeight(tagsTable_);
    const int headerHeight = tagsTable_->horizontalHeader()->sizeHint().height();
    tagsTable_->setMinimumHeight(rowHeight * 2 + headerHeight);
    tagsTable_->setMaximumHeight(QWIDGETSIZE_MAX);
    tagsSectionLayout->addWidget(tagsTable_, 1);
    tagsTableController_->setTagsTable(tagsTable_, tagsModel_);
    notesController_->setTagsTable(tagsTable_, tagsModel_);
}

void WorkWindow::buildGameControlsAndStatsWindow() {
    gameControls_ = new GameControls(this);
    gameControls_->setMinimumWidth(GameControls::kMinimumPanelWidthPx);
    static_cast<QVBoxLayout*>(taggingSideColumn_->layout())->addWidget(gameControls_, 0);
    statsWindow_ = new StatsWindow(this);
    statsWindow_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    statsWindow_->setMinimumHeight(180);
}

void WorkWindow::buildNotesColumn() {
    notesColumn_ = new QWidget(this);
    notesColumn_->setObjectName(QStringLiteral("MatchNotesColumn"));
    auto* notesColumnLayout = new QVBoxLayout(notesColumn_);
    notesColumnLayout->setContentsMargins(0, 0, 0, 0);
    notesColumnLayout->setSpacing(6);

    auto* matchNotesLabel = new QLabel(notesColumn_);
    Style::setRole(matchNotesLabel, "muted");
    auto* matchNotesEditor = new MatchNotesEditor(notesColumn_);
    matchNotesEditor->setMinimumWidth(0);
    matchNotesEditor->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto* clipNotesLabel = new QLabel(notesColumn_);
    Style::setRole(clipNotesLabel, "muted");
    auto* clipNotesEditor = new QPlainTextEdit(notesColumn_);
    clipNotesEditor->setMaximumHeight(88);
    clipNotesEditor->setMinimumHeight(64);
    clipNotesEditor->setMinimumWidth(0);
    clipNotesEditor->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    Style::setRole(clipNotesEditor, "muted");

    notesColumnLayout->addWidget(matchNotesLabel);
    notesColumnLayout->addWidget(matchNotesEditor, 1);
    notesColumnLayout->addWidget(clipNotesLabel);
    notesColumnLayout->addWidget(clipNotesEditor, 0);
    notesController_->setNotesColumnWidgets(matchNotesLabel, matchNotesEditor, clipNotesLabel, clipNotesEditor);
    static_cast<QVBoxLayout*>(taggingSideColumn_->layout())->addWidget(notesColumn_, 1);
}

void WorkWindow::buildVideoStageColumn() {
    videoColumn_ = new QWidget(this);
    videoColumn_->setObjectName(QStringLiteral("PresentationStageColumn"));
    videoColumn_->setAttribute(Qt::WA_StyledBackground, true);
    videoColumn_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* videoColumnLayout = new QVBoxLayout(videoColumn_);
    videoColumnLayout->setContentsMargins(0, 0, 0, 0);
    videoColumnLayout->setSpacing(8);

    buildPresentationBanner(videoColumnLayout);

    videoPlayer_ = new VideoPlayer(videoColumn_);
    videoColumnLayout->addWidget(videoPlayer_, 1);
    tagToast_ = new TagToastOverlay(videoPlayer_->videoFrame());
    tagToast_->trackVideoFrame(videoPlayer_->videoFrame());

    presentationClipBar_ = new ClipTrimBar(videoColumn_);
    videoColumnLayout->addWidget(presentationClipBar_, 0);

    presentationBanner_->hide();
    presentationClipBar_->hide();
}

void WorkWindow::buildPresentationBanner(QVBoxLayout* videoColumnLayout) {
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
}

void WorkWindow::buildWorkSideStack() {
    workSideStack_ = new QStackedWidget(this);
    workSideStack_->addWidget(taggingSideColumn_);
    workSideStack_->addWidget(statsWindow_);
    presentationPanel_ = new PresentationPanel(this);
    presentationPanel_->setMinimumWidth(260);
    presentationPanel_->setExportEnabled(false);
    workSideStack_->addWidget(presentationPanel_);
}

void WorkWindow::assembleWorkSplitters(QVBoxLayout* contentAreaLayout) {
    workTagsNotesSplitter_ = new QSplitter(Qt::Horizontal, this);
    workTagsNotesSplitter_->setObjectName(QStringLiteral("WorkTagsNotesSplitter"));
    workTagsNotesSplitter_->setChildrenCollapsible(false);
    workTagsNotesSplitter_->setHandleWidth(6);
    workTagsNotesSplitter_->addWidget(tagsSection_);
    workTagsNotesSplitter_->setStretchFactor(0, 1);
    tagsSection_->setMinimumWidth(160);

    videoAndTagsSplitter_ = new QSplitter(Qt::Vertical, this);
    // Object name stays WorkLeftSplitter so the splitter QSS keeps matching.
    videoAndTagsSplitter_->setObjectName(QStringLiteral("WorkLeftSplitter"));
    videoAndTagsSplitter_->setChildrenCollapsible(false);
    videoAndTagsSplitter_->setHandleWidth(6);
    videoAndTagsSplitter_->addWidget(videoColumn_);
    videoAndTagsSplitter_->addWidget(workTagsNotesSplitter_);
    videoAndTagsSplitter_->setStretchFactor(0, 5);
    videoAndTagsSplitter_->setStretchFactor(1, 3);

    buildWorkSideStack();

    workOuterSplitter_ = new QSplitter(Qt::Horizontal, this);
    workOuterSplitter_->setObjectName(QStringLiteral("PresentationSplitter"));
    workOuterSplitter_->setChildrenCollapsible(false);
    workOuterSplitter_->setHandleWidth(6);
    workOuterSplitter_->setAttribute(Qt::WA_StyledBackground, true);
    workOuterSplitter_->addWidget(workSideStack_);
    workOuterSplitter_->addWidget(videoAndTagsSplitter_);
    workOuterSplitter_->setStretchFactor(0, 0);
    workOuterSplitter_->setStretchFactor(1, 4);

    for (QSplitter* splitter : {workOuterSplitter_, videoAndTagsSplitter_, workTagsNotesSplitter_}) {
        splitter->setAttribute(Qt::WA_StyledBackground, true);
    }

    contentAreaLayout->addWidget(workOuterSplitter_, 1);
    connect(workOuterSplitter_, &QSplitter::splitterMoved, this,
            &WorkWindow::enforceTaggingSidePanelWidthCap);
    workOuterSplitter_->hide();
}

void WorkWindow::hideChromeUntilVideoOpens() {
    if (videoPlayer_) videoPlayer_->setControlsVisible(false);
    if (modeTaggingButton_) modeTaggingButton_->hide();
    if (modeAnalyzingButton_) modeAnalyzingButton_->hide();
    if (modePresentingButton_) modePresentingButton_->hide();
    if (modeToggleTrack_) modeToggleTrack_->syncToMode(false);
}

void WorkWindow::buildPresentationUi() {
    presentationQueue_ = new PresentationQueue(this);
    presentationPlaybackController_->setPresentationQueue(presentationQueue_);
    presentationPlaybackController_->setPresentationClipBar(presentationClipBar_);
    presentationPlaybackController_->setPresentationPanel(presentationPanel_);
    presentationPlaybackController_->setStageLabels(presentationEventLabel_,
                                                    presentationContextLabel_,
                                                    presentationNoteLabel_);
    wirePresentationPanelSignals();
    presentationPlaybackController_->wirePresentationQueueSignals();
    presentationPlaybackController_->wirePresentationClipBarSignals();
}

void WorkWindow::wirePresentationPanelSignals() {
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
}

void WorkWindow::wireSignals() {
    wireGameSetupSignals();
    wireVideoChromeSignals();
    wireGameControlsSignals();
    wireTagsTableSignals();
    wireModeToggleSignals();
    wireNotesSignals();
    wireTagsTableActionSignals();
    installModeAndStatsShortcuts();
    wireApplicationLanguageSignals();
    wirePlaybackAndTemporaryDirectorySignals();
    installTagEditShortcuts();
    wireApplicationFocusSignals();
}

void WorkWindow::wireGameSetupSignals() {
    if (gameSetupWidget_) {
        connect(gameSetupWidget_, &GameSetupWindow::gameSetupConfirmed, this, &WorkWindow::onGameSetupConfirmed);
        connect(gameSetupWidget_, &GameSetupWindow::cancelled, this, &WorkWindow::onGameSetupCancelled);
    }
}

void WorkWindow::wireVideoChromeSignals() {
    // Video file management
    connect(replaceVideoAction_, &QAction::triggered, this, &WorkWindow::onReplaceVideo);
    connect(closeVideoAction_, &QAction::triggered, this, &WorkWindow::onCloseVideo);

    // Connect VideoPlayer's videoClosed signal to WorkWindow's signal
    connect(videoPlayer_, &VideoPlayer::videoClosed, this, &WorkWindow::videoClosed);
    connect(videoMuteButton_, &QToolButton::clicked, this, &WorkWindow::toggleVideoMuteFromToolbar);
    connect(videoPlayer_, &VideoPlayer::muteStateChanged, this, &WorkWindow::updateVideoMuteButton);
    connect(videoPlayer_, &VideoPlayer::muteToolbarFlashRequested, this, &WorkWindow::flashVideoMuteButton);

    connect(importXmlAction_, &QAction::triggered, this, &WorkWindow::onImportXml);
    connect(exportXmlAction_, &QAction::triggered, this, &WorkWindow::onExportXml);
    connect(clipDurationSettingsAction_, &QAction::triggered, this,
            &WorkWindow::onClipDurationSettings);
    connect(reduceMotionAction_, &QAction::toggled, this, [](bool checked) {
        Motion::setReducedMotionPreferred(checked);
    });
}

void WorkWindow::toggleVideoMuteFromToolbar() {
    if (videoPlayer_) videoPlayer_->toggleMuteWithControlFlash();
}

void WorkWindow::wireGameControlsSignals() {
    // GameControls -> capture timestamp and store tags
    connect(gameControls_, &GameControls::mainEventTimestampCaptured, this,
            &WorkWindow::rememberCapturedMainEvent);

    if (gameControls_) {
        connect(gameControls_, &GameControls::gameStartRequested, this,
                &WorkWindow::onGameStartRequested);
        connect(gameControls_, &GameControls::nextQuarterRequested, this,
                &WorkWindow::onNextQuarterRequested);
    }

    connect(gameControls_, &GameControls::tagSequenceAdvanced, this, &WorkWindow::showTagSequenceStep);
    connect(gameControls_, &GameControls::tagSequenceDismissed, this, &WorkWindow::dismissTagSequenceToast);
    connect(gameControls_, &GameControls::tagCommitted, this, &WorkWindow::commitTagFromGameControls);
}

void WorkWindow::rememberCapturedMainEvent(const QString& mainEvent) {
    if (!videoPlayer_) return;
    capturedMainEvent_ = CapturedMainEvent{mainEvent, videoPlayer_->currentPositionMs()};
}

void WorkWindow::showTagSequenceStep(const QString& shortcutKey, const QString& stepLabel,
                                    const QColor& teamColor, bool chainOpen, bool startsChain,
                                    EventCatalog::EventId stepEventId) {
    if (!tagToast_) return;
    const EventLabelOverrides::Table overrides =
        tagSession_ ? tagSession_->eventLabelOverrides() : EventLabelOverrides::Table{};
    QString displayLabel = EventLabelResolver::labelForId(stepEventId, overrides);
    if (displayLabel.isEmpty()) displayLabel = AppLocale::trEvent(stepLabel);
    if (startsChain) {
        tagToast_->beginEvent(shortcutKey, displayLabel, teamColor, chainOpen);
    } else {
        tagToast_->appendFollowUp(displayLabel, chainOpen);
    }
}

void WorkWindow::dismissTagSequenceToast() {
    if (tagToast_) tagToast_->dismissImmediately();
}

void WorkWindow::commitTagFromGameControls(const QString& mainEvent, const QString& followUpEvent) {
    if (!videoPlayer_) return;

    qint64 markMs = videoPlayer_->currentPositionMs();
    if (capturedMainEvent_ && capturedMainEvent_->event == mainEvent) {
        markMs = capturedMainEvent_->markMs;
    }
    capturedMainEvent_.reset();

    if (!tagSession_) return;
    const PendingTagPeriodAndTeam context = pendingTagPeriodAndTeam();
    TagSession::GameTag tag;
    const TagEvent::MainEventKey key = TagEvent::keyFromCanonicalName(mainEvent);
    tag.setMainEventKey(key);
    const std::optional<TagEvent::FollowUpChain> parsedFollowUps =
        TagEvent::parseLegacyFollowUpPayload(key.id, followUpEvent, tagSession_->homeTeamName(),
                                             tagSession_->awayTeamName());
    if (parsedFollowUps.has_value()) {
        tag.setFollowUpChain(*parsedFollowUps);
    } else {
        Q_ASSERT_X(false, "WorkWindow::commitTagFromGameControls",
                   "follow-up payload is not in the catalog");
    }
    tag.markMs = markMs;
    tag.period = context.period;
    tag.team = TagSession::teamSideKey(context.team);
    // tag.startMs / tag.endMs left at 0 so TagSession::addTag seeds them from EventDefaults.
    tagSession_->addTag(tag);
}

void WorkWindow::wireTagsTableSignals() {
    connect(tagsTable_, &QTableView::doubleClicked, this, &WorkWindow::seekVideoFromTagsTableIndex);
    connect(tagsTable_, &QAbstractItemView::activated, this, &WorkWindow::seekVideoFromTagsTableIndex);
    connect(tagsTable_->selectionModel(), &QItemSelectionModel::currentChanged, this,
            &WorkWindow::onTagsTableCurrentChanged);
}

void WorkWindow::seekVideoFromTagsTableIndex(const QModelIndex& index) {
    if (index.isValid()) seekVideoToTagsTableRow(index.row());
}

void WorkWindow::onTagsTableCurrentChanged(const QModelIndex& current, const QModelIndex& previous) {
    if (current == previous) return;
    onTagSelectionChanged();
}

void WorkWindow::wireModeToggleSignals() {
    connect(modeTaggingButton_, &QToolButton::clicked, this, &WorkWindow::onModeToggled);
    connect(modeAnalyzingButton_, &QToolButton::clicked, this, &WorkWindow::onModeToggled);
    connect(modePresentingButton_, &QToolButton::clicked, this, &WorkWindow::onModeToggled);
}

void WorkWindow::wireNotesSignals() {
    notesController_->wireEditorSignals();
}

void WorkWindow::wireTagsTableActionSignals() {
    tagsTableController_->wireFilterSignals();
    connect(statsWindow_, &StatsWindow::filterByEventPathRequested, tagsTableController_.get(),
            &TagsTableController::onFilterByEventPathRequested);
    connect(undoLastTagButton_, &QToolButton::clicked, this, &WorkWindow::onUndoLastTag);
}

void WorkWindow::installModeAndStatsShortcuts() {
    auto* modeAction = new QAction(this);
    modeAction->setShortcut(QKeySequence(Qt::Key_M));
    modeAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(modeAction, &QAction::triggered, this, &WorkWindow::cycleWorkMode);
    addAction(modeAction);

    statsOverlayAction_ = new QAction(this);
    statsOverlayAction_->setShortcut(QKeySequence(Qt::Key_Comma));
    statsOverlayAction_->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(statsOverlayAction_, &QAction::triggered, this, &WorkWindow::showStatsOverlay);
    addAction(statsOverlayAction_);
}

void WorkWindow::cycleWorkMode() {
    setMode(nextModeInCycle(mode_));
}

void WorkWindow::wireApplicationLanguageSignals() {
    connect(&LocaleNotifier::instance(), &LocaleNotifier::languageChanged, this,
            &WorkWindow::onApplicationLanguageChanged);
}

void WorkWindow::wirePlaybackAndTemporaryDirectorySignals() {
    connect(videoPlayer_, &VideoPlayer::positionChangedMs, this, &WorkWindow::onVideoPositionChanged);
    connect(videoPlayer_, &VideoPlayer::durationChangedMs, this, &WorkWindow::onVideoDurationChanged);
    playbackTemporaryDirectoryController_->setVideoPlayer(videoPlayer_);
    notesController_->setVideoPlayer(videoPlayer_);
    tagsTableController_->setVideoPlayer(videoPlayer_);
    presentationPlaybackController_->setVideoPlayer(videoPlayer_);
}

void WorkWindow::onVideoPositionChanged(qint64 positionMs) {
    tagsTableController_->updatePlayheadHighlight(positionMs);
    presentationPlaybackController_->updatePresentationPlayhead(positionMs);
}

void WorkWindow::onVideoDurationChanged(qint64 durationMs) {
    if (presentationQueue_) presentationQueue_->setVideoDurationMs(durationMs);
}

void WorkWindow::installTagEditShortcuts() {
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

void WorkWindow::wireApplicationFocusSignals() {
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
                                       const QDate& gameDate,
                                       const QString& homeAbbrev,
                                       const QString& awayAbbrev) {
    if (VideoConcatenator* pendingConcatenator = playbackTemporaryDirectoryController_->pendingConcatenator()) {
        notesController_->suspendNoteDebounceTimers();
        pendingConcatenator->waitWithProgress(
            this,
            [this, filePath, homeName, awayName, homeColor, awayColor, gameDate, homeAbbrev, awayAbbrev](
                bool concatOk) {
                VideoConcatenator* concatenator =
                    playbackTemporaryDirectoryController_->pendingConcatenator();
                const QString errorMessage = concatenator ? concatenator->errorMessage() : QString();
                if (!concatOk) {
                    abortVideoOpen(errorMessage);
                    return;
                }
                playbackTemporaryDirectoryController_->cleanupPendingConcatenation();
                applyConfirmedGameSetup(filePath, homeName, awayName, homeColor, awayColor,
                                        gameDate, homeAbbrev, awayAbbrev);
            });
        return;
    }

    applyConfirmedGameSetup(filePath, homeName, awayName, homeColor, awayColor,
                            gameDate, homeAbbrev, awayAbbrev);
}

void WorkWindow::applyConfirmedGameSetup(const QString& filePath,
                                         const QString& homeName, const QString& awayName,
                                         const QString& homeColor, const QString& awayColor,
                                         const QDate& gameDate,
                                         const QString& homeAbbrev,
                                         const QString& awayAbbrev) {
    if (tagSession_) {
        tagSession_->setGameTeams(homeName, awayName, homeColor, awayColor);
        tagSession_->setGameMetadata(gameDate, homeAbbrev, awayAbbrev);
    }
    if (contentStack_) contentStack_->setCurrentIndex(1);
    loadVideoFromFile(filePath);
}

void WorkWindow::onGameStartRequested() {
    if (!videoPlayer_ || !tagSession_) return;
    if (tagSession_->startGame(videoPlayer_->currentPositionMs())) return;
    if (gameControls_) {
        gameControls_->restoreGamePhase(tagSession_->quarterPhase(),
                                        tagSession_->currentQuarterIndex());
    }
}

void WorkWindow::onNextQuarterRequested() {
    if (!videoPlayer_ || !tagSession_) return;
    if (tagSession_->advanceQuarter(videoPlayer_->currentPositionMs())) return;
    if (gameControls_) {
        gameControls_->restoreGamePhase(tagSession_->quarterPhase(),
                                        tagSession_->currentQuarterIndex());
    }
}

void WorkWindow::onGameSetupCancelled() {
    onCloseVideo();
}

void WorkWindow::loadVideoFromFile(const QString& filePath) {
    if (filePath.isEmpty()) return;

    ++playbackPreparationGeneration_;
    activePlaybackPreparer_.reset();
    activePlaybackPrepDirectory_.reset();
    notesController_->suspendNoteDebounceTimers();
    playbackTemporaryDirectoryController_->releasePlaybackPrepTemporaryDirectory();

    if (!PlaybackVideoPreparer::requiresTranscodeForPlayback(filePath)) {
        finishOpeningVideo(filePath, filePath);
        return;
    }

    auto tempDir = std::make_unique<QTemporaryDir>();
    if (!tempDir->isValid()) {
        abortVideoOpen(AppLocale::trUi("playback_prep.error_failed"));
        return;
    }

    const int generation = playbackPreparationGeneration_;
    activePlaybackPrepDirectory_ = std::move(tempDir);
    activePlaybackPreparer_ = std::make_unique<PlaybackVideoPreparer>();
    PlaybackVideoPreparer* preparer = activePlaybackPreparer_.get();
    preparer->startPreparation(filePath, activePlaybackPrepDirectory_->path());
    preparer->waitWithProgress(this, [this, filePath, generation](bool prepOk) {
        if (generation != playbackPreparationGeneration_) return;
        const QString errorMessage = activePlaybackPreparer_
            ? activePlaybackPreparer_->errorMessage()
            : QString();
        const QString preparedPath = activePlaybackPreparer_
            ? activePlaybackPreparer_->outputPath()
            : QString();
        std::unique_ptr<QTemporaryDir> preparedDirectory = std::move(activePlaybackPrepDirectory_);
        activePlaybackPreparer_.reset();
        if (!prepOk) {
            abortVideoOpen(errorMessage);
            return;
        }
        playbackTemporaryDirectoryController_->setPlaybackPrepTemporaryDirectory(
            std::move(preparedDirectory));
        finishOpeningVideo(filePath, preparedPath);
    });
}

void WorkWindow::finishOpeningVideo(const QString& filePath, const QString& playbackPath) {
    sourceVideoPath_ = filePath;
    workStage_ = WorkStage::VideoOpen;
    resetSessionForOpenedVideo();
    showPlayerForOpenedVideo(playbackPath);
    showWorkChromeForOpenedVideo();
    resetPresentationForOpenedVideo();
    refreshSurfacesForOpenedVideo();
}

void WorkWindow::resetSessionForOpenedVideo() {
    if (tagToast_) tagToast_->dismissImmediately();
    capturedMainEvent_.reset();
    if (tagSession_) tagSession_->clear();
    if (telestrationSession_) telestrationSession_->clear();
    if (tagsModel_) tagsModel_->setRows({});
}

void WorkWindow::showPlayerForOpenedVideo(const QString& playbackPath) {
    if (!videoPlayer_) return;
    videoPlayer_->loadVideoFromFile(playbackPath);
    videoPlayer_->setControlsVisible(true);
    setVideoMuteButtonEnabled(true);
}

void WorkWindow::showWorkChromeForOpenedVideo() {
    if (gameControls_) {
        gameControls_->resetGameTimeState();
        gameControls_->setTeamSide(TagSession::TeamSide::Home);
    }
    if (workOuterSplitter_) {
        workOuterSplitter_->show();
        QTimer::singleShot(0, this, &WorkWindow::applySidePanelCompressedWidth);
    }
    if (modeTaggingButton_) modeTaggingButton_->show();
    if (modeAnalyzingButton_) modeAnalyzingButton_->show();
    if (modePresentingButton_) modePresentingButton_->show();
    if (modeToggleTrack_) {
        QTimer::singleShot(0, modeToggleTrack_, [track = QPointer<ModeToggleTrack>(modeToggleTrack_)]() {
            if (track) track->syncToMode(false);
        });
    }
}

void WorkWindow::resetPresentationForOpenedVideo() {
    if (presentationQueue_) presentationQueue_->clear();
    presentationPlaybackController_->resetPresentationForVideoChange();
    if (presentationPanel_) {
        presentationPanel_->refreshFromSession();
        presentationPanel_->setExportEnabled(true);
    }
    presentationPlaybackController_->updatePresentationStage();
}

void WorkWindow::refreshSurfacesForOpenedVideo() {
    applyModeChrome();
    if (statsWindow_) statsWindow_->setTagSession(tagSession_);
    tagsTableController_->syncFilterMenu();
    tagsTableController_->refreshTagsTableRows();
    refreshPlaybackShortcutFocusGate();
}

bool WorkWindow::exportXmlAndNotesBeforeReplace() {
    if (!tagSession_ || !exportJobManager_) {
        return false;
    }

    const QString chosenPath = QFileDialog::getSaveFileName(
        this,
        AppLocale::trUi("replace_video.export_dialog_title"),
        suggestedXmlReportPath(),
        AppLocale::trUi("file.xml_filter"));
    if (chosenPath.isEmpty()) {
        return false;
    }

    const QString xmlPath = pathWithXmlSuffix(chosenPath);
    if (!startXmlReportJob(xmlPath,
                           AppLocale::trUi("export.xml_title"),
                           AppLocale::trUi("export.xml_failed"))) {
        return false;
    }
    return writeSessionNotesBesideXml(xmlPath);
}

QString WorkWindow::suggestedXmlReportPath() const {
    const QString baseName = ExportClipBuilder::xmlReportBaseName(tagSession_);
    QString directoryPath = exportDefaultDirectoryPath_.trimmed();
    if (directoryPath.isEmpty()) {
        directoryPath = QFileInfo(sourceVideoPath_).absolutePath();
    }
    return QDir(directoryPath).filePath(baseName + QStringLiteral(".xml"));
}

bool WorkWindow::startXmlReportJob(const QString& outputPath,
                                   const QString& failureTitle,
                                   const QString& emptyErrorFallback) {
    ExportJobRequest request;
    request.format = ExportOutputFormat::Xml;
    request.outputPath = outputPath;
    request.session = captureTagSessionExportData(*tagSession_);

    QString errorMessage;
    if (exportJobManager_->startJob(request, &errorMessage)) return true;

    const QString shownMessage = errorMessage.isEmpty() ? emptyErrorFallback : errorMessage;
    QMessageBox::warning(this, failureTitle, shownMessage);
    return false;
}

bool WorkWindow::writeSessionNotesBesideXml(const QString& xmlPath) {
    const QString notesPath = SessionNotesWriter::notesPathForXmlReport(xmlPath);
    QString errorMessage;
    if (SessionNotesWriter::writeFile(*tagSession_, notesPath, &errorMessage)) return true;
    QMessageBox::warning(this,
                         AppLocale::trUi("replace_video.title"),
                         errorMessage.isEmpty() ? AppLocale::trUi("replace_video.notes_failed")
                                                : errorMessage);
    return false;
}

void WorkWindow::onReplaceVideo() {
    if (!confirmExportAllowsVideoRelease()) return;
    notesController_->flushPendingClipNote();
    notesController_->saveMatchNoteDebounceFired();
    notesController_->suspendNoteDebounceTimers();

    const ReplaceVideoChoice choice = chooseReplaceVideoAction(tagSession_, this);
    if (choice == ReplaceVideoChoice::Cancel) {
        notesController_->resumeNoteDebounceTimers();
        return;
    }
    if (choice == ReplaceVideoChoice::ExportThenContinue && !exportXmlAndNotesBeforeReplace()) {
        notesController_->resumeNoteDebounceTimers();
        return;
    }

    QStringList filePaths = VideoConcatenator::selectVideoFiles(this);
    if (filePaths.isEmpty()) {
        notesController_->resumeNoteDebounceTimers();
        return;
    }

    if (filePaths.size() == 1) {
        replaceOpenVideoWithFile(filePaths.first());
        return;
    }
    replaceOpenVideoWithConcatenation(filePaths);
}

void WorkWindow::replaceOpenVideoWithFile(const QString& filePath) {
    releaseTransientResources();
    setExportDefaultDirectoryFromVideoPath(filePath);
    loadVideoFromFile(filePath);
}

void WorkWindow::replaceOpenVideoWithConcatenation(QStringList filePaths) {
    filePaths.sort(Qt::CaseInsensitive);
    if (!VideoConcatenator::showFileOrderDialog(filePaths, this)) {
        notesController_->resumeNoteDebounceTimers();
        return;
    }
    setExportDefaultDirectoryFromVideoPath(filePaths.first());

    ++videoConcatenationGeneration_;
    activeConcatenator_.reset();
    activeConcatTemporaryDirectory_.reset();

    auto tempDir = std::make_unique<QTemporaryDir>();
    if (!tempDir->isValid()) {
        QMessageBox::warning(this,
                             AppLocale::trUi("app.title"),
                             AppLocale::trUi("concat.error_failed"));
        notesController_->resumeNoteDebounceTimers();
        return;
    }

    const int generation = videoConcatenationGeneration_;
    activeConcatTemporaryDirectory_ = std::move(tempDir);
    activeConcatenator_ = std::make_unique<VideoConcatenator>();
    connect(activeConcatenator_.get(), &VideoConcatenator::runFinished, this,
            [this, generation](const VideoConcatenationResult& result) {
                if (generation != videoConcatenationGeneration_) return;
                std::unique_ptr<QTemporaryDir> finishedDirectory =
                    std::move(activeConcatTemporaryDirectory_);
                activeConcatenator_.reset();
                if (!result.succeeded) {
                    if (!result.errorMessage.isEmpty()) {
                        QMessageBox::warning(this, AppLocale::trUi("app.title"), result.errorMessage);
                    }
                    notesController_->resumeNoteDebounceTimers();
                    return;
                }
                const QString outputPath = result.outputPath;
                releaseTransientResources();
                setConcatenatedVideoTemporaryDirectory(std::move(finishedDirectory));
                loadVideoFromFile(outputPath);
            },
            Qt::QueuedConnection);
    activeConcatenator_->runWithProgress(
        filePaths, activeConcatTemporaryDirectory_->path(), this);
}

void WorkWindow::onCloseVideo() {
    if (!confirmExportAllowsVideoRelease()) return;
    workStage_ = WorkStage::NoVideo;
    sourceVideoPath_.clear();
    if (tagToast_) tagToast_->dismissImmediately();
    capturedMainEvent_.reset();
    if (gameControls_) gameControls_->setTeamSide(TagSession::TeamSide::Unspecified);
    if (videoPlayer_) videoPlayer_->setControlsVisible(false);
    setVideoMuteButtonEnabled(false);
    updateVideoMuteButton(false);
    if (gameControls_) gameControls_->resetGameTimeState();
    if (workOuterSplitter_) workOuterSplitter_->hide();
    if (modeTaggingButton_) modeTaggingButton_->hide();
    if (modeAnalyzingButton_) modeAnalyzingButton_->hide();
    if (modePresentingButton_) modePresentingButton_->hide();
    if (modeToggleTrack_) modeToggleTrack_->syncToMode(false);
    if (presentationQueue_) presentationQueue_->clear();
    if (presentationPanel_) presentationPanel_->setExportEnabled(false);
    presentationPlaybackController_->resetPresentationForVideoChange();

    if (tagSession_) tagSession_->clear();
    if (telestrationSession_) telestrationSession_->clear();
    if (tagsModel_) tagsModel_->setRows({});

    releaseTransientResources();
    exportDefaultDirectoryPath_.clear();
    emit videoClosed();
    refreshPlaybackShortcutFocusGate();
}

void WorkWindow::onPresentationExportRequested() {
    if (!tagSession_ || !hasOpenVideo() || !exportJobManager_) return;

    const QVector<PresentationQueue::Clip> queuedClips = copyPresentationQueueClips();

    ExportSettingsDialog dialog(
        tagSession_,
        sourceVideoPath_,
        exportDefaultDirectoryPath_,
        queuedClips,
        exportJobManager_->activeOutputPaths(),
        this);
    dialog.setModal(true);

    if (dialog.exec() != QDialog::Accepted) return;

    const ExportJobRequest request = buildPresentationExportRequest(dialog.resultSettings(), queuedClips);
    QString errorMessage;
    if (!exportJobManager_->startJob(request, &errorMessage)) {
        QMessageBox::warning(this, AppLocale::trUi("export.title"), errorMessage);
    }
}

QVector<PresentationQueue::Clip> WorkWindow::copyPresentationQueueClips() const {
    QVector<PresentationQueue::Clip> queuedClips;
    if (!presentationQueue_) return queuedClips;
    queuedClips.reserve(presentationQueue_->count());
    for (int index = 0; index < presentationQueue_->count(); ++index) {
        queuedClips.append(presentationQueue_->clipAt(index));
    }
    return queuedClips;
}

void WorkWindow::appendRetainedPlaybackDirectories(ExportJobRequest& request) const {
    playbackTemporaryDirectoryController_->appendRetainedPlaybackDirectories(request);
}

ExportJobRequest WorkWindow::buildPresentationExportRequest(
    const ExportSettingsDialog::Result& settings,
    const QVector<PresentationQueue::Clip>& queuedClips) const {
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
    overlayOptions.includeDrawings = settings.includeDrawingsOverlay;
    if (settings.includeDrawingsOverlay && telestrationSession_ != nullptr) {
        overlayOptions.drawings = telestrationSession_->drawings();
    }

    ExportJobRequest request;
    request.format = settings.format;
    request.sourceVideoPath = sourceVideoPath_;
    request.outputPath = settings.outputPath;
    request.clips = ExportClipBuilder::buildClipSegments(tagSession_, orderedClips, overlayOptions);
    request.includeAudioTrack = settings.includeAudioTrack;
    request.includeBrandingOverlay = settings.includeBrandingOverlay;
    request.session = captureTagSessionExportData(*tagSession_);
    appendRetainedPlaybackDirectories(request);
    return request;
}

void WorkWindow::onExportXml() {
    if (!tagSession_ || !hasOpenVideo() || !exportJobManager_) return;

    if (tagSession_->tags().isEmpty()) {
        QMessageBox::warning(this,
                             AppLocale::trUi("export.xml_title"),
                             AppLocale::trUi("export.no_tags_for_xml"));
        return;
    }

    const QString outputPath = QFileDialog::getSaveFileName(
        this,
        AppLocale::trUi("export.xml_save_dialog_title"),
        suggestedXmlReportPath(),
        AppLocale::trUi("file.xml_filter"));
    if (outputPath.isEmpty()) return;

    startXmlReportJob(outputPath, AppLocale::trUi("export.xml_title"), QString());
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
    if (!tagSession_ || !videoPlayer_ || !hasOpenVideo()) return;

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

    const std::optional<TagSession::ImportMode> importMode = promptXmlImportMode();
    if (!importMode.has_value()) return;

    const XmlImporter::SyncAnchorResult anchor = XmlImporter::syncAnchorInstance(instances);
    if (!anchor.found) {
        QMessageBox::warning(this, AppLocale::trUi("xml_import.title"),
                             AppLocale::trUi("xml_import.no_sync_anchor"));
        return;
    }

    const std::optional<qint64> offsetMs = promptXmlSyncOffset(anchor, instances);
    if (!offsetMs.has_value()) return;

    const std::optional<XmlEventMappingDialog::ImportMappingResult> importResult =
        promptXmlEventMapping(instances, *offsetMs);
    if (!importResult.has_value()) return;

    importMappedXmlTags(*importMode, *importResult);
}

std::optional<TagSession::ImportMode> WorkWindow::promptXmlImportMode() {
    if (tagSession_->tags().isEmpty()) return TagSession::ImportMode::Replace;

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
    if (conflictBox.clickedButton() == replaceButton) return TagSession::ImportMode::Replace;
    if (conflictBox.clickedButton() == mergeButton) return TagSession::ImportMode::Merge;
    return std::nullopt;
}

std::optional<qint64> WorkWindow::promptXmlSyncOffset(
    const XmlImporter::SyncAnchorResult& anchor,
    const QVector<XmlImporter::ParsedInstance>& instances) {
    XmlSyncDialog syncDialog(videoPlayer_, anchor.instance, instances, anchor.usedFallback, this);
    syncDialog.setModal(true);
    if (videoPlayer_) {
        videoPlayer_->setPlaybackKeyboardShortcutsEnabled(false);
    }
    const int syncResult = syncDialog.exec();
    if (videoPlayer_) {
        videoPlayer_->setPlaybackKeyboardShortcutsEnabled(true);
    }
    if (syncResult != QDialog::Accepted) return std::nullopt;
    return syncDialog.offsetMs();
}

std::optional<XmlEventMappingDialog::ImportMappingResult> WorkWindow::promptXmlEventMapping(
    const QVector<XmlImporter::ParsedInstance>& instances,
    qint64 offsetMs) {
    XmlEventMappingDialog mappingDialog(instances, offsetMs, tagSession_, this);
    mappingDialog.setModal(true);
    if (mappingDialog.exec() != QDialog::Accepted) return std::nullopt;

    const XmlEventMappingDialog::ImportMappingResult importResult = mappingDialog.importResult();
    if (importResult.tags.isEmpty()) {
        QMessageBox::information(this, AppLocale::trUi("xml_import.title"),
                                 AppLocale::trUi("xml_import.mapping_none_selected"));
        return std::nullopt;
    }
    return importResult;
}

void WorkWindow::importMappedXmlTags(TagSession::ImportMode importMode,
                                    const XmlEventMappingDialog::ImportMappingResult& importResult) {
    const qint64 videoDurationMs = videoPlayer_->durationMs();
    const TagSession::ImportResult result =
        tagSession_->importTags(importResult.tags, importMode, videoDurationMs);

    const int dialogSkipped = importResult.skippedInstanceCount;
    QMessageBox::information(
        this,
        AppLocale::trUi("xml_import.title"),
        AppLocale::trUi("xml_import.complete_summary")
            .arg(result.importedCount)
            .arg(dialogSkipped)
            .arg(result.clampedCount));
    showXmlImportAdjustmentNotice(importResult.adjustedBeforeStartCount);
}

void WorkWindow::onModeToggled() {
    auto* modeButton = qobject_cast<QToolButton*>(sender());
    if (!modeButton) return;
    if (modeButton == modeTaggingButton_) {
        setMode(Mode::Tagging);
    } else if (modeButton == modeAnalyzingButton_) {
        setMode(Mode::Analyzing);
    } else if (modeButton == modePresentingButton_) {
        setMode(Mode::Presenting);
    }
}

void WorkWindow::onPresentationSelectionChanged(const QVector<int>& tagSessionIndexes) {
    if (!presentationQueue_) return;
    presentationQueue_->setSelectedTagIndexes(tagSessionIndexes);
}

void WorkWindow::onPresentationClipActivated(int tagSessionIndex) {
    if (!presentationQueue_) return;
    if (mode_ != Mode::Presenting) setMode(Mode::Presenting);
    if (!presentationQueue_->setCurrentTagIndex(tagSessionIndex)) return;
    presentationPlaybackController_->showPresentationClip(presentationQueue_->currentIndex());
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
    presentationPlaybackController_->configurePresentationClipBarForCurrentClip();
}

void WorkWindow::onPresentationShowNotesToggled() {
    presentationPlaybackController_->updatePresentationStage();
}

void WorkWindow::onTagSelectionChanged() {
    notesController_->flushPendingClipNote();
    notesController_->loadClipNoteForSelectedTag();
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
        connect(statsOverlay_, &StatsWindow::filterByEventPathRequested, tagsTableController_.get(),
                &TagsTableController::onFilterByEventPathRequested);
    }
    if (statsOverlay_) statsOverlay_->setTagSession(tagSession_);
    statsOverlayDialog_->raise();
    statsOverlayDialog_->show();
}

void WorkWindow::seekVideoToTagsTableRow(int row) {
    if (row < 0 || !videoPlayer_ || !tagsModel_) return;
    videoPlayer_->seekToMs(tagsModel_->markMsAt(row));
}

void WorkWindow::flashNewTagRow(quint64 tagId) {
    if (!tagsModel_ || tagId == 0) return;
    if (tagsModel_->rowForTagId(tagId) < 0) return;
    if (newTagFlashTimer_) {
        newTagFlashTimer_->stop();
    } else {
        newTagFlashTimer_ = new QTimer(this);
        newTagFlashTimer_->setSingleShot(true);
        connect(newTagFlashTimer_, &QTimer::timeout, this, &WorkWindow::fadeNewTagFlash);
    }
    tagsModel_->setFlashTagId(tagId);
    Motion::animateColor(tagsModel_, "flashTint", Style::ThemeColors::playheadHighlight(), 0);
    newTagFlashTimer_->start(500);
}

void WorkWindow::fadeNewTagFlash() {
    if (!tagsModel_ || tagsModel_->flashTint().alpha() == 0) return;
    QColor clearTint = Style::ThemeColors::playheadHighlight();
    clearTint.setAlpha(0);
    Motion::animateColor(tagsModel_, "flashTint", clearTint, Motion::kHoverIn);
}

void WorkWindow::onDeleteSelectedTag() {
    if (!tagsTable_ || !tagSession_ || !tagsModel_) return;

    const quint64 tagId = notesController_->selectedTagId();
    if (tagId == 0) return;

    const int tagIndex = notesController_->sessionIndexForTagId(tagId);
    if (tagIndex < 0 || !tagSession_->isValidTagIndex(tagIndex)) return;

    notesController_->discardPendingClipNoteForRemovedTag(tagId);
    tagSession_->removeTag(tagIndex);
}

void WorkWindow::onUndoLastTag() {
    if (!tagSession_) return;
    const int tagCount = tagSession_->tags().size();
    if (tagCount == 0) return;
    tagSession_->removeTag(tagCount - 1);
}

#include "WorkWindow.moc"
