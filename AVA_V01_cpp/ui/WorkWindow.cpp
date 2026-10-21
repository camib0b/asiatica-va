#include "WorkWindow.h"
#include "PlaybackTemporaryDirectoryController.h"
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
#include <QVideoWidget>
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

/// Sets a reentrancy flag for this scope and restores the value it had on entry.
class ScopedReentrancyFlag {
public:
    explicit ScopedReentrancyFlag(bool& flag) : flag_(flag), previousValue_(flag) { flag_ = true; }
    ~ScopedReentrancyFlag() { flag_ = previousValue_; }

    ScopedReentrancyFlag(const ScopedReentrancyFlag&) = delete;
    ScopedReentrancyFlag& operator=(const ScopedReentrancyFlag&) = delete;

private:
    bool& flag_;
    bool previousValue_;
};
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
#include <QSignalBlocker>
#include <QEvent>
#include <QDialog>
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
#include <cstddef>

namespace {

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

/// Same rule as the export reviewer: leave room on both sides of the clip so the presenter can
/// stretch the interval or scrub past it without the bar clipping the view.
struct PresentationClipWindow {
    qint64 startMs = 0;
    qint64 endMs = 0;
};

PresentationClipWindow presentationClipWindow(const PresentationQueue::Clip& clip, qint64 videoDurationMs) {
    const qint64 halfWindowMs =
        std::max<qint64>(25000, std::max(clip.leadMs(), clip.lagMs()) + 5000);
    qint64 windowStartMs = clip.markMs - halfWindowMs;
    qint64 windowEndMs = clip.markMs + halfWindowMs;
    if (windowStartMs < 0) windowStartMs = 0;
    if (videoDurationMs > 0 && windowEndMs > videoDurationMs) windowEndMs = videoDurationMs;
    return PresentationClipWindow{windowStartMs, windowEndMs};
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
      temporaryDirectoryController_(std::make_unique<PlaybackTemporaryDirectoryController>()) {
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
    if (exportJobManager_) {
        exportJobManager_->cancelExportingJobs();
    }
    temporaryDirectoryController_->releaseForShutdown();
    tagSession_ = nullptr;
}

bool WorkWindow::shouldDeliverPlaybackKeyboardToVideoPlayer(const QWidget* focusWidget) const {
    if (!hasOpenVideo()) return false;
    if (!videoPlayer_ || !videoPlayer_->isMediaKeyboardControlActive()) return false;
    if (!focusWidget) return true;
    if (focusWidget->window() != window()) return false;
    if (!isAncestorOf(focusWidget)) return false;
    if (mode_ == Mode::Analyzing && notesEdit_ && focusWidget == notesEdit_) return false;
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

void WorkWindow::setConcatenatedVideoTempDir(std::unique_ptr<QTemporaryDir> dir) {
    temporaryDirectoryController_->setConcatenatedVideoTempDir(std::move(dir));
}

void WorkWindow::setExportDefaultDirectoryFromVideoPath(const QString& videoPath) {
    exportDefaultDirectoryPath_.clear();
    if (videoPath.trimmed().isEmpty()) return;

    const QFileInfo videoInfo(videoPath);
    exportDefaultDirectoryPath_ = videoInfo.absolutePath();
}

void WorkWindow::setPendingConcatenation(std::unique_ptr<VideoConcatenator> concatenator) {
    temporaryDirectoryController_->setPendingConcatenation(std::move(concatenator));
}

void WorkWindow::releaseTransientResources() {
    temporaryDirectoryController_->releaseTransientResources();
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

void WorkWindow::showAdjustedImportNote(int adjustedCount) {
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
    updatePresentationStage();
    syncFilterMenu();
    refreshTagsTableRows();
}

void WorkWindow::disconnectTagSessionSignals() {
    for (const QMetaObject::Connection& connection : tagSessionConnections_) {
        QObject::disconnect(connection);
    }
    tagSessionConnections_.clear();
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
    refreshTagsTableRows();
    updatePresentationStage();
    if (presentationPanel_) presentationPanel_->refreshFromSession();
}

void WorkWindow::setTagSession(TagSession* session) {
    if (tagSession_ == session) return;

    flushPendingClipNote();
    disconnectTagSessionSignals();

    tagSession_ = session;
    bindTagSessionToSurfaces();
    if (gameControls_) {
        gameControls_->setTeamSide(tagSession_ ? TagSession::TeamSide::Home
                                               : TagSession::TeamSide::Unspecified);
    }
    syncFilterMenu();
    applySessionTeamIdentity();
    loadMatchNote();

    if (!tagSession_) return;
    connectTagSessionSignals();
}

void WorkWindow::bindTagSessionToSurfaces() {
    if (statsWindow_) statsWindow_->setTagSession(tagSession_);
    if (statsOverlay_) statsOverlay_->setTagSession(tagSession_);
    if (presentationQueue_) presentationQueue_->setTagSession(tagSession_);
    if (presentationPanel_) presentationPanel_->setTagSession(tagSession_);
}

void WorkWindow::connectTagSessionSignals() {
    tagSessionConnections_.append(connect(
        tagSession_, &TagSession::aboutToClear, this, &WorkWindow::flushPendingClipNote,
        Qt::DirectConnection));
    tagSessionConnections_.append(connect(
        tagSession_, &TagSession::aboutToReplaceTags, this, &WorkWindow::flushPendingClipNote,
        Qt::DirectConnection));
    tagSessionConnections_.append(connect(
        tagSession_, &TagSession::tagsChanged, this, &WorkWindow::onTagSessionTagsChanged));
    tagSessionConnections_.append(connect(
        tagSession_, &TagSession::tagsImported, this, &WorkWindow::onTagSessionTagsImported));
    tagSessionConnections_.append(connect(
        tagSession_, &TagSession::tagNoteChanged, this, &WorkWindow::onTagSessionTagNoteChanged));
    tagSessionConnections_.append(connect(
        tagSession_, &TagSession::matchNoteChanged, this, &WorkWindow::onTagSessionMatchNoteChanged));
    tagSessionConnections_.append(connect(
        tagSession_, &TagSession::gameMetadataChanged, this, &WorkWindow::onTagSessionGameMetadataChanged));
}

void WorkWindow::onTagSessionTagsChanged(quint64 addedTagId) {
    if (!tagSession_) return;
    syncFilterMenu();
    refreshTagsTableRows();
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

void WorkWindow::onTagSessionTagNoteChanged(int changedIndex) {
    if (!notesEdit_ || !tagSession_) return;
    if (notesEdit_->hasFocus() || pendingClipNote_.has_value()) return;
    if (!tagSession_->isValidTagIndex(changedIndex)) return;
    const quint64 changedTagId = tagSession_->tags().at(changedIndex).id;
    if (selectedTagId() != changedTagId) return;
    loadNoteForSelectedTag();
}

void WorkWindow::onTagSessionMatchNoteChanged() {
    if (!tagSession_) return;
    if (matchNotesEditor_ && matchNotesEditor_->hasFocus()) return;
    loadMatchNote();
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
    if (modeTaggingBtn_) modeTaggingBtn_->setChecked(mode_ == Mode::Tagging);
    if (modeAnalyzingBtn_) modeAnalyzingBtn_->setChecked(mode_ == Mode::Analyzing);
    if (modePresentingBtn_) modePresentingBtn_->setChecked(mode_ == Mode::Presenting);
}

void WorkWindow::setMode(Mode m) {
    if (mode_ == m) {
        syncModeToggleChecks();
        return;
    }
    flushPendingClipNote();
    if (mode_ == Mode::Presenting && m != Mode::Presenting) {
        leavePresentationPlayback();
    }
    mode_ = m;
    applyModeChrome();
    if (m == Mode::Presenting) {
        enterPresentationPlayback();
    }
    setModeToggleButtonsChecked();
    if (modeToggleTrack_) modeToggleTrack_->syncToMode(true);
    refreshPlaybackShortcutFocusGate();
}

void WorkWindow::leavePresentationPlayback() {
    detachPresentationKeyboardShortcuts();
    commitPresentationPlayback(PresentationPlayback::reduce(
        presentationPlayback_, PresentationPlayback::ModeExit{}));
}

void WorkWindow::enterPresentationPlayback() {
    if (presentationPanel_) presentationPanel_->refreshFromSession();
    updatePresentationStage();
    configurePresentationClipBarForCurrentClip();
    attachPresentationKeyboardShortcuts();
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

    if (isTagging && taggingRightCol_) {
        if (workTagsNotesSplitter_ && workTagsNotesSplitter_->indexOf(notesColumn_) >= 0) {
            notesColumn_->setParent(nullptr);
        }
        if (auto* sideLayout = qobject_cast<QVBoxLayout*>(taggingRightCol_->layout())) {
            if (sideLayout->indexOf(notesColumn_) < 0) {
                notesColumn_->setParent(taggingRightCol_);
                sideLayout->addWidget(notesColumn_, 1);
            }
        }
    } else if (isAnalyzing && workTagsNotesSplitter_) {
        if (taggingRightCol_) {
            if (auto* sideLayout = qobject_cast<QVBoxLayout*>(taggingRightCol_->layout())) {
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
    if (isTagging) page = taggingRightCol_;
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
    if (taggingRightCol_) {
        taggingRightCol_->setSizePolicy(isTagging ? QSizePolicy::Preferred : QSizePolicy::Ignored,
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

WorkWindow::TagContext WorkWindow::tagContext() const {
    TagContext context;
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

    modeTaggingBtn_ = new QToolButton(modeToggleTrack_);
    configureModeToolButton(modeTaggingBtn_, QStringLiteral("WorkModeTaggingButton"), true);

    modeAnalyzingBtn_ = new QToolButton(modeToggleTrack_);
    configureModeToolButton(modeAnalyzingBtn_, QStringLiteral("WorkModeAnalyzingButton"), false);

    modePresentingBtn_ = new QToolButton(modeToggleTrack_);
    configureModeToolButton(modePresentingBtn_, QStringLiteral("WorkModePresentingButton"), false);

    auto* modeToggleLayout = new QHBoxLayout(modeToggleTrack_);
    modeToggleLayout->setContentsMargins(3, 3, 3, 3);
    modeToggleLayout->setSpacing(4);
    modeToggleLayout->addWidget(modeTaggingBtn_);
    modeToggleLayout->addWidget(modeAnalyzingBtn_);
    modeToggleLayout->addWidget(modePresentingBtn_);
    modeToggleTrack_->setButtons(modeTaggingBtn_, modeAnalyzingBtn_, modePresentingBtn_);
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
    temporaryDirectoryController_->setExportJobManager(exportJobManager_);
    exportJobsBar_ = new ExportJobsBar(exportJobManager_, this);
    rootLayout->addWidget(exportJobsBar_, 0);
}

void WorkWindow::buildTaggingSideColumn() {
    taggingRightCol_ = new QWidget(this);
    // Object name stays TaggingRightCol so the compact-button QSS keeps matching
    // after the column moves to the left of the video.
    taggingRightCol_->setObjectName(QStringLiteral("TaggingRightCol"));
    auto* taggingRightLayout = new QVBoxLayout(taggingRightCol_);
    taggingRightLayout->setContentsMargins(0, 0, 0, 0);
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
}

void WorkWindow::buildTagsFilterMenu() {
    tagsFilterMenu_ = new QMenu(tagsFilterButton_);
    tagsFilterMenu_->setObjectName(QStringLiteral("TagsFilterMenu"));
    tagsFilterButton_->setMenu(tagsFilterMenu_);
    filterSelectAllAction_ = tagsFilterMenu_->addAction(QString());
    filterSelectNoneAction_ = tagsFilterMenu_->addAction(QString());
    tagsFilterMenu_->addSeparator();
    connect(filterSelectAllAction_, &QAction::triggered, this, &WorkWindow::onSelectAllFilters);
    connect(filterSelectNoneAction_, &QAction::triggered, this, &WorkWindow::onSelectNoFilters);
    connect(tagsFilterMenu_, &QMenu::aboutToHide, this, &WorkWindow::onTagsFilterMenuAboutToHide);
}

void WorkWindow::onTagsFilterMenuAboutToHide() {
    if (!filterMenuStructureSyncDeferred_) return;
    QTimer::singleShot(0, this, [this]() { applyDeferredFilterMenuStructureSync(); });
}

void WorkWindow::applyDeferredFilterMenuStructureSync() {
    if (!filterMenuStructureSyncDeferred_) return;
    filterMenuForceStructureSync_ = true;
    syncFilterMenu();
    filterMenuForceStructureSync_ = false;
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
}

void WorkWindow::buildGameControlsAndStatsWindow() {
    gameControls_ = new GameControls(this);
    gameControls_->setMinimumWidth(GameControls::kMinimumPanelWidthPx);
    static_cast<QVBoxLayout*>(taggingRightCol_->layout())->addWidget(gameControls_, 0);
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

    matchNotesLabel_ = new QLabel(notesColumn_);
    Style::setRole(matchNotesLabel_, "muted");
    matchNotesEditor_ = new MatchNotesEditor(notesColumn_);
    matchNotesEditor_->setMinimumWidth(0);
    matchNotesEditor_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    clipNotesLabel_ = new QLabel(notesColumn_);
    Style::setRole(clipNotesLabel_, "muted");
    notesEdit_ = new QPlainTextEdit(notesColumn_);
    notesEdit_->setMaximumHeight(88);
    notesEdit_->setMinimumHeight(64);
    notesEdit_->setMinimumWidth(0);
    notesEdit_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    Style::setRole(notesEdit_, "muted");

    notesColumnLayout->addWidget(matchNotesLabel_);
    notesColumnLayout->addWidget(matchNotesEditor_, 1);
    notesColumnLayout->addWidget(clipNotesLabel_);
    notesColumnLayout->addWidget(notesEdit_, 0);
    static_cast<QVBoxLayout*>(taggingRightCol_->layout())->addWidget(notesColumn_, 1);
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
    tagToast_ = new TagToastOverlay(videoPlayer_->videoWidget());
    tagToast_->trackVideoFrame(videoPlayer_->videoWidget());

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
    workSideStack_->addWidget(taggingRightCol_);
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

    workLeftSplitter_ = new QSplitter(Qt::Vertical, this);
    workLeftSplitter_->setObjectName(QStringLiteral("WorkLeftSplitter"));
    workLeftSplitter_->setChildrenCollapsible(false);
    workLeftSplitter_->setHandleWidth(6);
    workLeftSplitter_->addWidget(videoColumn_);
    workLeftSplitter_->addWidget(workTagsNotesSplitter_);
    workLeftSplitter_->setStretchFactor(0, 5);
    workLeftSplitter_->setStretchFactor(1, 3);

    buildWorkSideStack();

    workOuterSplitter_ = new QSplitter(Qt::Horizontal, this);
    workOuterSplitter_->setObjectName(QStringLiteral("PresentationSplitter"));
    workOuterSplitter_->setChildrenCollapsible(false);
    workOuterSplitter_->setHandleWidth(6);
    workOuterSplitter_->setAttribute(Qt::WA_StyledBackground, true);
    workOuterSplitter_->addWidget(workSideStack_);
    workOuterSplitter_->addWidget(workLeftSplitter_);
    workOuterSplitter_->setStretchFactor(0, 0);
    workOuterSplitter_->setStretchFactor(1, 4);

    for (QSplitter* splitter : {workOuterSplitter_, workLeftSplitter_, workTagsNotesSplitter_}) {
        splitter->setAttribute(Qt::WA_StyledBackground, true);
    }

    contentAreaLayout->addWidget(workOuterSplitter_, 1);
    connect(workOuterSplitter_, &QSplitter::splitterMoved, this,
            &WorkWindow::enforceTaggingSidePanelWidthCap);
    workOuterSplitter_->hide();
}

void WorkWindow::hideChromeUntilVideoOpens() {
    if (videoPlayer_) videoPlayer_->setControlsVisible(false);
    if (modeTaggingBtn_) modeTaggingBtn_->hide();
    if (modeAnalyzingBtn_) modeAnalyzingBtn_->hide();
    if (modePresentingBtn_) modePresentingBtn_->hide();
    if (modeToggleTrack_) modeToggleTrack_->syncToMode(false);
}

void WorkWindow::buildPresentationUi() {
    presentationQueue_ = new PresentationQueue(this);
    wirePresentationPanelSignals();
    wirePresentationQueueSignals();
    wirePresentationClipBarSignals();
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

void WorkWindow::wirePresentationQueueSignals() {
    connect(presentationQueue_, &PresentationQueue::queueChanged, this,
            &WorkWindow::onPresentationQueueChanged);
    connect(presentationQueue_, &PresentationQueue::currentClipChanged, this,
            &WorkWindow::onPresentationCurrentClipChanged);
    connect(presentationQueue_, &PresentationQueue::clipIntervalChanged, this,
            &WorkWindow::onPresentationClipIntervalChanged);
}

void WorkWindow::wirePresentationClipBarSignals() {
    connect(presentationClipBar_, &ClipTrimBar::seekRequested, this,
            &WorkWindow::seekVideoFromPresentationClipBar);
    connect(presentationClipBar_, &ClipTrimBar::clipStartChanged, this,
            &WorkWindow::onPresentationClipBarRangeEdited);
    connect(presentationClipBar_, &ClipTrimBar::clipEndChanged, this,
            &WorkWindow::onPresentationClipBarRangeEdited);
}

void WorkWindow::seekVideoFromPresentationClipBar(qint64 positionMs) {
    if (videoPlayer_) videoPlayer_->seekToMs(positionMs);
}

void WorkWindow::onPresentationClipBarRangeEdited(qint64 /*endpointMs*/) {
    savePresentationClipIntervalFromClipBar();
}

void WorkWindow::wireSignals() {
    wireGameSetupSignals();
    wireVideoChromeSignals();
    wireGameControlsSignals();
    wireTagsTableSignals();
    wireModeToggleSignals();
    wireNotesSignals();
    wireTagListActionSignals();
    installModeAndStatsShortcuts();
    wireLocaleAndNoteTimers();
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
                                    const QColor& teamColor, bool chainOpen, bool startsChain) {
    if (!tagToast_) return;
    const QString displayLabel = AppLocale::trEvent(stepLabel);
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

    qint64 timestampMs = videoPlayer_->currentPositionMs();
    if (capturedMainEvent_ && capturedMainEvent_->event == mainEvent) {
        timestampMs = capturedMainEvent_->positionMs;
    }
    capturedMainEvent_.reset();

    if (!tagSession_) return;
    const TagContext context = tagContext();
    TagSession::GameTag tag;
    tag.mainEvent = mainEvent;
    tag.followUpEvent = followUpEvent;
    tag.markMs = timestampMs;
    tag.period = context.period;
    tag.team = TagSession::teamSideKey(context.team);
    // tag.startMs / tag.endMs left at 0 so TagSession::addTag seeds them from EventDefaults.
    tagSession_->addTag(tag);
}

void WorkWindow::wireTagsTableSignals() {
    connect(tagsTable_, &QTableView::doubleClicked, this, &WorkWindow::seekTagsTableIndex);
    connect(tagsTable_, &QAbstractItemView::activated, this, &WorkWindow::seekTagsTableIndex);
    connect(tagsTable_->selectionModel(), &QItemSelectionModel::currentChanged, this,
            &WorkWindow::onTagsTableCurrentChanged);
}

void WorkWindow::seekTagsTableIndex(const QModelIndex& index) {
    if (index.isValid()) onTagTableSeekToRow(index.row());
}

void WorkWindow::onTagsTableCurrentChanged(const QModelIndex& current, const QModelIndex& previous) {
    if (current == previous) return;
    onTagSelectionChanged();
}

void WorkWindow::wireModeToggleSignals() {
    connect(modeTaggingBtn_, &QToolButton::clicked, this, &WorkWindow::onModeToggled);
    connect(modeAnalyzingBtn_, &QToolButton::clicked, this, &WorkWindow::onModeToggled);
    connect(modePresentingBtn_, &QToolButton::clicked, this, &WorkWindow::onModeToggled);
}

void WorkWindow::wireNotesSignals() {
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
}

void WorkWindow::wireTagListActionSignals() {
    connect(statsWindow_, &StatsWindow::filterByEventPathRequested, this, &WorkWindow::onFilterByEventPathRequested);
    connect(tagsRemoveFiltersButton_, &QToolButton::clicked, this, &WorkWindow::onRemoveFilters);
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

void WorkWindow::wireLocaleAndNoteTimers() {
    connect(&LocaleNotifier::instance(), &LocaleNotifier::languageChanged, this,
            &WorkWindow::onApplicationLanguageChanged);

    noteDebounceTimer_ = new QTimer(this);
    noteDebounceTimer_->setSingleShot(true);
    connect(noteDebounceTimer_, &QTimer::timeout, this, &WorkWindow::saveNoteDebounceFired);

    matchNoteDebounceTimer_ = new QTimer(this);
    matchNoteDebounceTimer_->setSingleShot(true);
    connect(matchNoteDebounceTimer_, &QTimer::timeout, this, &WorkWindow::saveMatchNoteDebounceFired);
}

void WorkWindow::wirePlaybackAndTemporaryDirectorySignals() {
    connect(videoPlayer_, &VideoPlayer::positionChangedMs, this, &WorkWindow::onVideoPositionChanged);
    connect(videoPlayer_, &VideoPlayer::durationChangedMs, this, &WorkWindow::onVideoDurationChanged);
    temporaryDirectoryController_->setVideoPlayer(videoPlayer_);
}

void WorkWindow::onVideoPositionChanged(qint64 positionMs) {
    onPlayheadPositionChanged(positionMs);
    updatePresentationPlayhead(positionMs);
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
    if (VideoConcatenator* pendingConcatenator = temporaryDirectoryController_->pendingConcatenator()) {
        stopNoteTimers();
        const bool concatOk = pendingConcatenator->waitWithProgress(this);
        const QString errorMessage = pendingConcatenator->errorMessage();
        if (!concatOk) {
            abortVideoOpen(errorMessage);
            return;
        }
        temporaryDirectoryController_->cleanupPendingConcatenation();
    }

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

    const std::optional<QString> playbackPath = preparePlaybackPath(filePath);
    if (!playbackPath.has_value()) return;

    sourceVideoPath_ = filePath;
    workStage_ = WorkStage::VideoOpen;
    resetSessionForOpenedVideo();
    showPlayerForOpenedVideo(*playbackPath);
    showWorkChromeForOpenedVideo();
    resetPresentationForOpenedVideo();
    refreshSurfacesForOpenedVideo();
}

std::optional<QString> WorkWindow::preparePlaybackPath(const QString& filePath) {
    stopNoteTimers();
    QString playbackPath = filePath;
    temporaryDirectoryController_->cleanupPlaybackPrepVideo();

    if (!PlaybackVideoPreparer::requiresTranscodeForPlayback(filePath)) {
        return playbackPath;
    }

    auto tempDir = std::make_unique<QTemporaryDir>();
    if (!tempDir->isValid()) {
        abortVideoOpen(AppLocale::trUi("playback_prep.error_failed"));
        return std::nullopt;
    }

    auto preparer = std::make_unique<PlaybackVideoPreparer>();
    preparer->startPreparation(filePath, tempDir->path());
    const bool prepOk = preparer->waitWithProgress(this);
    const QString errorMessage = preparer->errorMessage();
    const QString preparedPath = preparer->outputPath();
    preparer.reset();

    if (!prepOk) {
        abortVideoOpen(errorMessage);
        return std::nullopt;
    }

    temporaryDirectoryController_->setPlaybackPrepTempDir(std::move(tempDir));
    return preparedPath;
}

void WorkWindow::resetSessionForOpenedVideo() {
    if (tagToast_) tagToast_->dismissImmediately();
    capturedMainEvent_.reset();
    if (tagSession_) tagSession_->clear();
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
    if (modeTaggingBtn_) modeTaggingBtn_->show();
    if (modeAnalyzingBtn_) modeAnalyzingBtn_->show();
    if (modePresentingBtn_) modePresentingBtn_->show();
    if (modeToggleTrack_) {
        QTimer::singleShot(0, modeToggleTrack_, [track = QPointer<ModeToggleTrack>(modeToggleTrack_)]() {
            if (track) track->syncToMode(false);
        });
    }
}

void WorkWindow::resetPresentationForOpenedVideo() {
    if (presentationQueue_) presentationQueue_->clear();
    resetPresentationForVideoChange();
    if (presentationPanel_) {
        presentationPanel_->refreshFromSession();
        presentationPanel_->setExportEnabled(true);
    }
    updatePresentationStage();
}

void WorkWindow::refreshSurfacesForOpenedVideo() {
    applyModeChrome();
    if (statsWindow_) statsWindow_->setTagSession(tagSession_);
    syncFilterMenu();
    refreshTagsTableRows();
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
    flushPendingClipNote();
    saveMatchNoteDebounceFired();
    stopNoteTimers();

    const ReplaceVideoChoice choice = chooseReplaceVideoAction(tagSession_, this);
    if (choice == ReplaceVideoChoice::Cancel) {
        resumeNoteTimers();
        return;
    }
    if (choice == ReplaceVideoChoice::ExportThenContinue && !exportXmlAndNotesBeforeReplace()) {
        resumeNoteTimers();
        return;
    }

    QStringList filePaths = VideoConcatenator::selectVideoFiles(this);
    if (filePaths.isEmpty()) {
        resumeNoteTimers();
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
        resumeNoteTimers();
        return;
    }
    setExportDefaultDirectoryFromVideoPath(filePaths.first());

    auto tempDir = std::make_unique<QTemporaryDir>();
    if (!tempDir->isValid()) {
        QMessageBox::warning(this,
                             AppLocale::trUi("app.title"),
                             AppLocale::trUi("concat.error_failed"));
        resumeNoteTimers();
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
        resumeNoteTimers();
        return;
    }

    const QString outputPath = result.outputPath;

    releaseTransientResources();
    setConcatenatedVideoTempDir(std::move(tempDir));
    loadVideoFromFile(outputPath);
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
    if (modeTaggingBtn_) modeTaggingBtn_->hide();
    if (modeAnalyzingBtn_) modeAnalyzingBtn_->hide();
    if (modePresentingBtn_) modePresentingBtn_->hide();
    if (modeToggleTrack_) modeToggleTrack_->syncToMode(false);
    if (presentationQueue_) presentationQueue_->clear();
    if (presentationPanel_) presentationPanel_->setExportEnabled(false);
    resetPresentationForVideoChange();

    if (tagSession_) tagSession_->clear();
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
    temporaryDirectoryController_->appendRetainedPlaybackDirectories(request);
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
    showAdjustedImportNote(importResult.adjustedBeforeStartCount);
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

namespace {

template <typename... Functions>
struct Overloaded : Functions... {
    using Functions::operator()...;
};

template <typename... Functions>
Overloaded(Functions...) -> Overloaded<Functions...>;

PresentationPlayback::QueueSnapshot queueSnapshot(const PresentationQueue& queue) {
    PresentationPlayback::QueueSnapshot snapshot;
    snapshot.currentIndex = queue.currentIndex();
    const int clipCount = queue.count();
    snapshot.clips.reserve(static_cast<std::size_t>(clipCount));
    for (int index = 0; index < clipCount; ++index) {
        const PresentationQueue::Clip& clip = queue.clipAt(index);
        snapshot.clips.push_back(PresentationPlayback::ClipSpan{clip.startMs, clip.endMs});
    }
    return snapshot;
}

void applyPresentationEffect(VideoPlayer& player, const PresentationPlayback::Effect& effect) {
    std::visit(Overloaded{
                   [&player](const PresentationPlayback::Seek& seek) {
                       player.seekToMs(seek.positionMs);
                   },
                   [&player](const PresentationPlayback::Play&) { player.playWithControlFlash(); },
                   [&player](const PresentationPlayback::Pause&) {
                       player.pauseWithControlFlash();
                   },
               },
               effect);
}

}  // namespace

void WorkWindow::onPresentationSelectionChanged(const QVector<int>& tagSessionIndexes) {
    if (!presentationQueue_) return;
    presentationQueue_->setSelectedTagIndexes(tagSessionIndexes);
}

void WorkWindow::onPresentationClipActivated(int tagSessionIndex) {
    if (!presentationQueue_) return;
    if (mode_ != Mode::Presenting) setMode(Mode::Presenting);
    if (!presentationQueue_->setCurrentTagIndex(tagSessionIndex)) return;
    showPresentationClip(presentationQueue_->currentIndex());
}

void WorkWindow::onPresentationQueueChanged() {
    if (presentationQueue_ && presentationQueue_->isEmpty()) {
        commitPresentationPlayback(PresentationPlayback::reduce(
            presentationPlayback_, PresentationPlayback::QueueEmptied{}));
    }
    updatePresentationStage();
    // setClipInterval refresh emits queueChanged before clipIntervalChanged; reconfiguring here
    // would reset ClipTrimBar::dragTarget_ and end an in-progress handle drag.
    if (!updatingPresentationClipBar_) {
        configurePresentationClipBarForCurrentClip();
    }
}

void WorkWindow::onPresentationCurrentClipChanged(int /*queueIndex*/) {
    updatePresentationStage();
    if (!updatingPresentationClipBar_) {
        configurePresentationClipBarForCurrentClip();
    }
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
    commitPresentationPlayback(PresentationPlayback::reduce(
        presentationPlayback_,
        PresentationPlayback::ClipIntervalUpdated{positionMs, clip->endMs}));
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

void WorkWindow::onPresentationShowNotesToggled() {
    updatePresentationStage();
}

void WorkWindow::showPresentationClip(int queueIndex) {
    if (!presentationQueue_ || !videoPlayer_) return;
    if (!presentationQueue_->setCurrentIndex(queueIndex)) return;

    const PresentationQueue::Clip* clip = presentationQueue_->currentClip();
    if (!clip) return;

    commitPresentationPlayback(PresentationPlayback::reduce(
        presentationPlayback_, PresentationPlayback::OpenClip{clip->startMs, clip->endMs}));
}

void WorkWindow::goToNextPresentationClip() {
    stepPresentationQueue(true);
}

void WorkWindow::goToPreviousPresentationClip() {
    stepPresentationQueue(false);
}

void WorkWindow::stepPresentationQueue(bool forward) {
    if (!presentationQueue_ || presentationQueue_->isEmpty() || !videoPlayer_) return;

    const PresentationPlayback::QueueSnapshot snapshot = queueSnapshot(*presentationQueue_);
    const PresentationPlayback::Action action =
        forward ? PresentationPlayback::Action{PresentationPlayback::Next{snapshot}}
                : PresentationPlayback::Action{PresentationPlayback::Previous{snapshot}};
    commitPresentationPlayback(PresentationPlayback::reduce(presentationPlayback_, action));
}

void WorkWindow::commitPresentationPlayback(const PresentationPlayback::Result& result) {
    if (result.queueIndex.has_value()) {
        if (!presentationQueue_ || !presentationQueue_->setCurrentIndex(*result.queueIndex)) {
            return;
        }
    }
    presentationPlayback_ = result.state;
    if (!videoPlayer_) return;
    for (const PresentationPlayback::Effect& effect : result.effects) {
        applyPresentationEffect(*videoPlayer_, effect);
    }
}

void WorkWindow::resetPresentationForVideoChange() {
    // Closing or loading a video leaves Presenting. The next video would otherwise
    // open there with the Tab filter already removed, and setMode would not install it again.
    if (mode_ == Mode::Presenting) {
        mode_ = Mode::Tagging;
        detachPresentationKeyboardShortcuts();
        applyModeChrome();
        syncModeToggleChecks();
    }
    commitPresentationPlayback(
        PresentationPlayback::reduce(presentationPlayback_, PresentationPlayback::Reset{}));
}

void WorkWindow::updatePresentationStage() {
    if (!presentationEventLabel_ || !presentationContextLabel_ || !presentationNoteLabel_) return;

    const PresentationQueue::Clip* clip =
        presentationQueue_ ? presentationQueue_->currentClip() : nullptr;
    if (!clip) {
        showEmptyPresentationStage();
        return;
    }
    showCurrentPresentationClip(*clip);
}

void WorkWindow::showEmptyPresentationStage() {
    presentationEventLabel_->setText(AppLocale::trUi("presentation.empty_title"));
    presentationContextLabel_->setText(AppLocale::trUi("presentation.empty_hint"));
    presentationNoteLabel_->clear();
    presentationNoteLabel_->hide();
    if (presentationPanel_) presentationPanel_->clearCurrentClip();
}

void WorkWindow::showCurrentPresentationClip(const PresentationQueue::Clip& clip) {
    const QString followUpForDisplay = tagSession_
        ? AppLocale::followUpPathWithoutTeamSegments(clip.followUpEvent,
                                                     tagSession_->homeTeamName(),
                                                     tagSession_->awayTeamName())
        : clip.followUpEvent;

    presentationEventLabel_->setText(
        AppLocale::trDisplayTagLine(clip.mainEvent, followUpForDisplay));
    presentationContextLabel_->setText(presentationContextText(clip));

    const bool showNote =
        presentationPanel_ && presentationPanel_->showNotesEnabled() && !clip.note.isEmpty();
    presentationNoteLabel_->setText(clip.note);
    presentationNoteLabel_->setVisible(showNote);

    if (presentationPanel_) {
        presentationPanel_->setCurrentClip(clip.tagSessionIndex, clip.leadMs(), clip.lagMs());
    }
}

QString WorkWindow::presentationContextText(const PresentationQueue::Clip& clip) const {
    TagSession::GameTag tagForDisplay;
    tagForDisplay.team = clip.team;
    const QString periodLabel =
        tagSession_ ? tagSession_->periodLabelAtTimestampMs(clip.markMs) : QString();
    QStringList contextParts;
    contextParts.append(QStringLiteral("%1 / %2")
                            .arg(presentationQueue_->currentIndex() + 1)
                            .arg(presentationQueue_->count()));
    const QString teamName = displayTeamForTag(tagForDisplay);
    if (!teamName.isEmpty() && teamName != QStringLiteral("—")) contextParts.append(teamName);
    if (!periodLabel.isEmpty()) contextParts.append(periodLabel);
    contextParts.append(formatTimestampMs(clip.markMs));
    return contextParts.join(QStringLiteral("  ·  "));
}

void WorkWindow::configurePresentationClipBarForCurrentClip() {
    if (!presentationClipBar_) return;

    const PresentationQueue::Clip* clip =
        presentationQueue_ ? presentationQueue_->currentClip() : nullptr;
    if (!clip) {
        presentationClipBar_->hide();
        return;
    }
    presentationClipBar_->show();

    const qint64 videoDurationMs = videoPlayer_ ? videoPlayer_->durationMs() : 0;
    const PresentationClipWindow window = presentationClipWindow(*clip, videoDurationMs);
    presentationClipBar_->configure(clip->markMs, clip->startMs, clip->endMs, window.startMs,
                                    window.endMs);
    if (videoPlayer_) {
        presentationClipBar_->setPlayheadMs(videoPlayer_->currentPositionMs());
    }
}

void WorkWindow::savePresentationClipIntervalFromClipBar() {
    if (!presentationQueue_ || !presentationClipBar_) return;
    const int queueIndex = presentationQueue_->currentIndex();
    if (queueIndex < 0) return;

    const ScopedReentrancyFlag updatingClipBar(updatingPresentationClipBar_);
    presentationQueue_->setClipInterval(queueIndex, presentationClipBar_->clipStartMs(),
                                        presentationClipBar_->clipEndMs());
}

void WorkWindow::updatePresentationPlayhead(qint64 positionMs) {
    const bool presenting = mode_ == Mode::Presenting;
    if (presenting && presentationClipBar_) {
        presentationClipBar_->setPlayheadMs(positionMs);
    }

    const bool playerIsPlaying = videoPlayer_ && videoPlayer_->isPlaying();
    commitPresentationPlayback(PresentationPlayback::reduce(
        presentationPlayback_,
        PresentationPlayback::PlayheadTick{positionMs, presenting, playerIsPlaying}));
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
    if (handlePresentationTabShortcut(watched, event)) return true;
    return QWidget::eventFilter(watched, event);
}

bool WorkWindow::handlePresentationTabShortcut(QObject* watched, QEvent* event) {
    if (!event || !presentationKeyboardShortcutsInstalled_ || mode_ != Mode::Presenting ||
        event->type() != QEvent::KeyPress) {
        return false;
    }

    auto* targetWidget = qobject_cast<QWidget*>(watched);
    if (!targetWidget || targetWidget->window() != window()) {
        return false;
    }

    auto* keyEvent = static_cast<QKeyEvent*>(event);
    const bool isTabKey = keyEvent->key() == Qt::Key_Tab || keyEvent->key() == Qt::Key_Backtab;
    if (!isTabKey) return false;

    // Let Tab keep its normal meaning while the user is typing in the panel.
    if (TextInputFocus::widgetIsTextInput(QApplication::focusWidget())) {
        return false;
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
    pendingClipNote_ = PendingClipNote{tagId, tagSession_->tagIdEpoch(), notesEdit_->toPlainText()};
    noteDebounceTimer_->start(400);
}

void WorkWindow::stopNoteTimers() {
    resumeClipNoteTimer_ = noteDebounceTimer_ && noteDebounceTimer_->isActive();
    resumeMatchNoteTimer_ = matchNoteDebounceTimer_ && matchNoteDebounceTimer_->isActive();
    if (noteDebounceTimer_) noteDebounceTimer_->stop();
    if (matchNoteDebounceTimer_) matchNoteDebounceTimer_->stop();
}

void WorkWindow::resumeNoteTimers() {
    if (resumeClipNoteTimer_ && pendingClipNote_ && noteDebounceTimer_) {
        noteDebounceTimer_->start(400);
    }
    if (resumeMatchNoteTimer_ && matchNoteDebounceTimer_) {
        matchNoteDebounceTimer_->start(400);
    }
    resumeClipNoteTimer_ = false;
    resumeMatchNoteTimer_ = false;
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
    resumeClipNoteTimer_ = false;
    pendingClipNote_.reset();
}

void WorkWindow::flushPendingClipNote() {
    if (noteDebounceTimer_) noteDebounceTimer_->stop();
    resumeClipNoteTimer_ = false;
    if (!pendingClipNote_) return;

    const PendingClipNote note = *pendingClipNote_;
    pendingClipNote_.reset();
    if (!tagSession_) return;

    const int sessionIndex = tagSession_->indexOfTagId(note.tagId);
    const bool tagStillPresent = tagSession_->isValidTagIndex(sessionIndex);
    if (!pendingClipNoteApplies(note, tagSession_->tagIdEpoch(), tagStillPresent)) return;
    tagSession_->setTagNote(sessionIndex, note.text);
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

void WorkWindow::loadNoteForSelectedTag() {
    if (!notesEdit_) return;

    const QSignalBlocker notesBlocker(notesEdit_);
    const quint64 selectedId = selectedTagId();
    if (pendingClipNote_ && pendingClipNote_->tagId == selectedId) {
        showPendingClipNoteInEditor();
        return;
    }

    if (selectedId == 0 || !tagSession_) {
        showDisabledClipNoteEditor();
        return;
    }

    showStoredClipNoteInEditor(selectedId);
}

void WorkWindow::showPendingClipNoteInEditor() {
    if (notesEdit_->toPlainText() != pendingClipNote_->text) {
        notesEdit_->setPlainText(pendingClipNote_->text);
    }
    notesEdit_->setEnabled(true);
    notesEdit_->setPlaceholderText(AppLocale::trUi("tags.note_placeholder"));
}

void WorkWindow::showDisabledClipNoteEditor() {
    if (!notesEdit_->toPlainText().isEmpty()) {
        notesEdit_->clear();
    }
    notesEdit_->setEnabled(false);
    notesEdit_->setPlaceholderText(AppLocale::trUi("notes.clip_placeholder_none"));
}

void WorkWindow::showStoredClipNoteInEditor(quint64 selectedTagId) {
    const int sessionIndex = sessionIndexForTagId(selectedTagId);
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
}

int WorkWindow::sessionIndexForTagId(quint64 tagId) const {
    if (!tagSession_) return -1;
    const int sessionIndex = tagSession_->indexOfTagId(tagId);
    if (sessionIndex >= 0) return sessionIndex;
    if (!tagsModel_ || !tagsTable_ || !tagsTable_->selectionModel()) return sessionIndex;
    const QModelIndex currentIndex = tagsTable_->selectionModel()->currentIndex();
    if (!currentIndex.isValid()) return sessionIndex;
    return tagsModel_->tagSessionIndexAt(currentIndex.row());
}

void WorkWindow::loadMatchNote() {
    if (!matchNotesEditor_) return;
    const QString html = tagSession_ ? tagSession_->matchNote() : QString();
    if (matchNotesEditor_->isDocumentEquivalentTo(html)) return;
    matchNotesEditor_->setSerializedHtml(html);
}

void WorkWindow::refreshMatchNoteMentionCandidates() {
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

void WorkWindow::onPlayheadPositionChanged(qint64 positionMs) {
    if (tagsModel_) tagsModel_->setPlayheadMs(positionMs);
}

void WorkWindow::onDeleteSelectedTag() {
    if (!tagsTable_ || !tagSession_ || !tagsModel_) return;

    const quint64 tagId = selectedTagId();
    if (tagId == 0) return;

    const int tagIndex = sessionIndexForTagId(tagId);
    if (tagIndex < 0 || !tagSession_->isValidTagIndex(tagIndex)) return;

    if (pendingClipNote_ && pendingClipNote_->tagId == tagId) {
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
    tagFilter_.showAllMainEvents();
    syncFilterMenu();
    refreshTagsTableRows();
}

void WorkWindow::onSelectNoFilters() {
    QStringList mainEvents;
    if (tagSession_) mainEvents = tagSession_->mainEventCounts().keys();
    tagFilter_.hideAllMainEvents(mainEvents);
    syncFilterMenu();
    refreshTagsTableRows();
}

void WorkWindow::onFilterActionToggled(bool checked) {
    auto* action = qobject_cast<QAction*>(sender());
    if (!action) return;
    const QString mainEvent = action->data().toString();
    if (mainEvent.isEmpty()) return;
    if (checked) tagFilter_.hiddenMainEvents.remove(mainEvent);
    else tagFilter_.hiddenMainEvents.insert(mainEvent);
    refreshTagsTableRows();
}

void WorkWindow::onFilterByEventPathRequested(const QString& mainEvent, const QString& followUpEvent) {
    if (mainEvent.isEmpty()) tagFilter_.clearEventPath();
    else tagFilter_.eventPath = EventPathFilter{mainEvent, followUpEvent};
    syncFilterMenu();
    refreshTagsTableRows();
}

void WorkWindow::onRemoveFilters() {
    tagFilter_.clearEventPath();
    tagFilter_.showAllMainEvents();
    syncFilterMenu();
    refreshTagsTableRows();
}

bool WorkWindow::isTagAllowed(const QString& mainEvent, const QString& followUpEvent) const {
    return tagFilter_.allows(mainEvent, followUpEvent);
}

bool WorkWindow::hasAnyFilterActive() const {
    return tagFilter_.isActive();
}

void WorkWindow::updateFilterButtonsVisibility() const {
    if (!tagsRemoveFiltersButton_) return;
    if (hasAnyFilterActive()) {
        tagsRemoveFiltersButton_->show();
    } else {
        tagsRemoveFiltersButton_->hide();
    }
}

void WorkWindow::syncFilterMenu() {
    if (!tagsFilterMenu_ || !filterSelectAllAction_ || !filterSelectNoneAction_) return;

    const bool deferStructureChanges =
        tagsFilterMenu_->isVisible() && !filterMenuForceStructureSync_;
    if (deferStructureChanges) {
        filterMenuStructureSyncDeferred_ = true;
    }

    filterSelectAllAction_->setText(AppLocale::trUi("filter.select_all"));
    filterSelectNoneAction_->setText(AppLocale::trUi("filter.select_none"));

    const QSet<QString> liveMainEvents = refreshFilterMenuActions(deferStructureChanges);
    if (deferStructureChanges) return;

    hideRetiredFilterMenuActions(liveMainEvents);
    filterMenuStructureSyncDeferred_ = false;
}

QSet<QString> WorkWindow::refreshFilterMenuActions(bool deferStructureChanges) {
    QSet<QString> liveMainEvents;
    if (!tagSession_) return liveMainEvents;

    QStringList mainEvents = tagSession_->mainEventCounts().keys();
    mainEvents.sort(Qt::CaseInsensitive);
    for (const QString& mainEvent : mainEvents) {
        liveMainEvents.insert(mainEvent);
        QAction* action = filterActionByMainEvent_.value(mainEvent, nullptr);
        if (!action) {
            if (deferStructureChanges) continue;
            action = tagsFilterMenu_->addAction(AppLocale::trEvent(mainEvent));
            action->setCheckable(true);
            action->setData(mainEvent);
            connect(action, &QAction::toggled, this, &WorkWindow::onFilterActionToggled);
            filterActionByMainEvent_.insert(mainEvent, action);
        } else {
            action->setText(AppLocale::trEvent(mainEvent));
        }
        const bool checked = !tagFilter_.hiddenMainEvents.contains(mainEvent);
        if (action->isChecked() != checked) {
            const QSignalBlocker blocker(action);
            action->setChecked(checked);
        }
        if (!deferStructureChanges) action->setVisible(true);
    }
    return liveMainEvents;
}

void WorkWindow::hideRetiredFilterMenuActions(const QSet<QString>& liveMainEvents) {
    for (auto iterator = filterActionByMainEvent_.cbegin(); iterator != filterActionByMainEvent_.cend(); ++iterator) {
        if (!liveMainEvents.contains(iterator.key())) iterator.value()->setVisible(false);
    }
}

void WorkWindow::updateFilterIndicator() const {
    if (!tagsFilterIndicator_) return;

    if (tagFilter_.eventPath.has_value() && !tagFilter_.eventPath->mainEvent.isEmpty()) {
        QString pathText = AppLocale::trEvent(tagFilter_.eventPath->mainEvent);
        if (!tagFilter_.eventPath->followUpEvent.isEmpty()) {
            pathText += QStringLiteral(" → ")
                + AppLocale::translateCompoundPath(tagFilter_.eventPath->followUpEvent);
        }
        tagsFilterIndicator_->setText(AppLocale::trUi("filter.indicator") + pathText);
        tagsFilterIndicator_->show();
        return;
    }

    QStringList visibleMainEvents;
    if (tagSession_) {
        visibleMainEvents = tagSession_->mainEventCounts().keys();
        visibleMainEvents.sort(Qt::CaseInsensitive);
    }
    QStringList shownLabels;
    for (const QString& mainEvent : visibleMainEvents) {
        if (tagFilter_.hiddenMainEvents.contains(mainEvent)) continue;
        shownLabels.append(AppLocale::trEvent(mainEvent));
    }

    if (shownLabels.isEmpty() || shownLabels.size() == visibleMainEvents.size()) {
        tagsFilterIndicator_->hide();
        return;
    }

    shownLabels.sort(Qt::CaseInsensitive);
    tagsFilterIndicator_->setText(AppLocale::trUi("filter.indicator") + shownLabels.join(QStringLiteral(", ")));
    tagsFilterIndicator_->show();
}

void WorkWindow::refreshTagsTableRows() {
    if (!tagsTable_ || !tagsModel_) return;
    if (rebuildingTagsTable_) {
        tagsTableRebuildRequested_ = true;
        return;
    }

    const ScopedReentrancyFlag rebuilding(rebuildingTagsTable_);
    do {
        tagsTableRebuildRequested_ = false;
        rebuildTagsTableOnce();
    } while (tagsTableRebuildRequested_);
}

void WorkWindow::rebuildTagsTableOnce() {
    const quint64 previouslySelectedTagId = selectedTagId();
    const int previousVerticalScroll =
        tagsTable_->verticalScrollBar() ? tagsTable_->verticalScrollBar()->value() : 0;
    // Keep both blockers alive through the note reload. A selection change
    // during the rebuild must not re-enter through the table's currentChanged.
    const QSignalBlocker tableBlocker(tagsTable_);
    const QSignalBlocker selectionBlocker(tagsTable_->selectionModel());

    if (!tagSession_) {
        showEmptyTagsTable();
    } else {
        tagsModel_->setRows(visibleTagRowsSortedByMark());
        fitTagsTableColumns();
        restoreTagsTableSelection(previouslySelectedTagId, previousVerticalScroll);
        if (videoPlayer_) {
            tagsModel_->setPlayheadMs(videoPlayer_->currentPositionMs());
        }
        updateFilterIndicator();
        updateFilterButtonsVisibility();
        refreshMatchNoteMentionCandidates();
    }

    if (selectedTagId() != previouslySelectedTagId) {
        flushPendingClipNote();
        loadNoteForSelectedTag();
    }
}

void WorkWindow::showEmptyTagsTable() {
    tagsModel_->setRows({});
    refreshMatchNoteMentionCandidates();
    updateFilterIndicator();
    updateFilterButtonsVisibility();
}

QVector<TagsTableModel::Row> WorkWindow::visibleTagRowsSortedByMark() const {
    struct VisibleTag {
        TagSession::GameTag tag;
        int tagSessionIndex = 0;
    };
    QVector<VisibleTag> visibleTags;
    int tagSessionIndex = 0;
    for (const TagSession::GameTag& tag : tagSession_->tags()) {
        if (isTagAllowed(tag.mainEvent, tag.followUpEvent)) {
            visibleTags.append({tag, tagSessionIndex});
        }
        tagSessionIndex++;
    }

    std::sort(visibleTags.begin(), visibleTags.end(), [](const VisibleTag& left, const VisibleTag& right) {
        return left.tag.markMs < right.tag.markMs;
    });

    QVector<TagsTableModel::Row> rows;
    rows.reserve(visibleTags.size());
    for (const VisibleTag& visibleTag : visibleTags) {
        rows.append(tagsTableRowFor(visibleTag.tag, visibleTag.tagSessionIndex));
    }
    return rows;
}

TagsTableModel::Row WorkWindow::tagsTableRowFor(const TagSession::GameTag& tag, int tagSessionIndex) const {
    TagsTableModel::Row row;
    row.tagId = tag.id;
    row.tagSessionIndex = tagSessionIndex;
    row.markMs = tag.markMs;
    row.teamKey = tag.team;
    row.timeText = formatTimestampMs(tag.markMs);
    row.teamText = displayTeamForTag(tag);
    row.eventText =
        AppLocale::trDisplayTagLine(tag.mainEvent, followUpForEventColumn(tag.followUpEvent, tagSession_));
    row.teamBackground = teamBackgroundForTag(tag, tagSession_);
    return row;
}

void WorkWindow::fitTagsTableColumns() {
    tagsTable_->resizeColumnToContents(0);
    tagsTable_->resizeColumnToContents(1);
}

void WorkWindow::restoreTagsTableSelection(quint64 previouslySelectedTagId, int previousVerticalScroll) {
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
}

#include "WorkWindow.moc"
