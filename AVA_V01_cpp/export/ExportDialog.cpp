#include "ExportDialog.h"
#include "ClipExporter.h"
#include "ClipTrimBar.h"
#include "TagSession.h"
#include "VideoControlsBar.h"
#include "AppLocale.h"
#include "StyleProps.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QAudioOutput>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QKeyEvent>
#include <QMediaPlayer>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QVideoWidget>

#include <algorithm>

namespace {
constexpr double kPreviewMinPlaybackRate = 0.25;
constexpr double kPreviewMaxPlaybackRate = 4.0;
constexpr double kPreviewPlaybackRateStep = 0.25;

/// Suggested export path uses ASCII "special"; UI labels use ☆ via AppLocale::trEvent.
QString eventLabelForExportSuggestedFileName(const QString& canonicalEvent) {
    if (canonicalEvent == QStringLiteral("Special")) {
        return QStringLiteral("special");
    }
    return AppLocale::trEvent(canonicalEvent);
}
} // namespace

ExportDialog::ExportDialog(TagSession* session,
                           const QString& sourceVideoPath,
                           qint64 videoDurationMs,
                           QWidget* parent)
    : QDialog(parent)
    , tagSession_(session)
    , sourceVideoPath_(sourceVideoPath)
    , videoDurationMs_(videoDurationMs)
{
    setWindowTitle(AppLocale::trUi("export.title"));
    setMinimumSize(960, 700);
    resize(1024, 768);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    pagesStack_ = new QStackedWidget(this);
    mainLayout->addWidget(pagesStack_);

    buildSettingsPage();
    buildTrimPage();

    pagesStack_->setCurrentWidget(settingsPage_);

    populateEventTypes();
    updateClipCount();
}

ExportDialog::~ExportDialog() {
    detachTrimPageKeyboardShortcuts();
    stopPreviewPlayer();
}

// ---------------------------------------------------------------------------
// Settings page
// ---------------------------------------------------------------------------

void ExportDialog::buildSettingsPage() {
    settingsPage_ = new QWidget(this);
    auto* layout = new QVBoxLayout(settingsPage_);
    layout->setSpacing(16);
    layout->setContentsMargins(24, 24, 24, 24);

    auto* titleLabel = new QLabel(AppLocale::trUi("export.title"), settingsPage_);
    Style::setRole(titleLabel, "h2");
    layout->addWidget(titleLabel);

    auto* subtitleLabel = new QLabel(AppLocale::trUi("export.subtitle"), settingsPage_);
    Style::setRole(subtitleLabel, "muted");
    subtitleLabel->setWordWrap(true);
    layout->addWidget(subtitleLabel);

    layout->addSpacing(4);

    auto* formLayout = new QFormLayout();
    formLayout->setSpacing(10);
    formLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    eventTypeCombo_ = new QComboBox(settingsPage_);
    eventTypeCombo_->setMinimumWidth(200);
    connect(eventTypeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ExportDialog::onEventTypeChanged);
    formLayout->addRow(AppLocale::trUi("export.event_type"), eventTypeCombo_);

    teamFilterCombo_ = new QComboBox(settingsPage_);
    teamFilterCombo_->setMinimumWidth(200);
    {
        const QString homeName = (tagSession_ && !tagSession_->homeTeamName().isEmpty())
            ? tagSession_->homeTeamName()
            : AppLocale::trUi("export.team_home_default");
        const QString awayName = (tagSession_ && !tagSession_->awayTeamName().isEmpty())
            ? tagSession_->awayTeamName()
            : AppLocale::trUi("export.team_away_default");

        teamFilterCombo_->addItem(AppLocale::trUi("export.team_all"), QString());
        teamFilterCombo_->addItem(homeName, QStringLiteral("Home"));
        teamFilterCombo_->addItem(awayName, QStringLiteral("Away"));
    }
    connect(teamFilterCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ExportDialog::onTeamFilterChanged);
    formLayout->addRow(AppLocale::trUi("export.team_label"), teamFilterCombo_);

    sortOrderLabel_ = new QLabel(AppLocale::trUi("export.sort_order"), settingsPage_);
    sortOrderCombo_ = new QComboBox(settingsPage_);
    sortOrderCombo_->setMinimumWidth(200);
    sortOrderCombo_->addItem(AppLocale::trUi("export.sort_chronological"),
                             QStringLiteral("chronological"));
    sortOrderCombo_->addItem(AppLocale::trUi("export.sort_by_team"),
                             QStringLiteral("by_team"));
    sortOrderLabel_->hide();
    sortOrderCombo_->hide();
    formLayout->addRow(sortOrderLabel_, sortOrderCombo_);

    exportLanguageCombo_ = new QComboBox(settingsPage_);
    exportLanguageCombo_->setMinimumWidth(200);
    exportLanguageCombo_->addItem(AppLocale::trUi("setup.lang_en"),
                                  static_cast<int>(AppLocale::Language::English));
    exportLanguageCombo_->addItem(AppLocale::trUi("setup.lang_es"),
                                  static_cast<int>(AppLocale::Language::Spanish));
    exportLanguageCombo_->setCurrentIndex(
        AppLocale::currentLanguage() == AppLocale::Language::Spanish ? 1 : 0);
    formLayout->addRow(AppLocale::trUi("export.overlay_language"), exportLanguageCombo_);

    includeBottomOverlayCheckBox_ =
        new QCheckBox(AppLocale::trUi("export.include_bottom_overlay"), settingsPage_);
    includeBottomOverlayCheckBox_->setCursor(Qt::PointingHandCursor);
    includeBottomOverlayCheckBox_->setChecked(true);
    formLayout->addRow(QString(), includeBottomOverlayCheckBox_);

    includeScoreboardOverlayCheckBox_ =
        new QCheckBox(AppLocale::trUi("export.include_scoreboard_overlay"), settingsPage_);
    includeScoreboardOverlayCheckBox_->setCursor(Qt::PointingHandCursor);
    includeScoreboardOverlayCheckBox_->setChecked(true);
    formLayout->addRow(QString(), includeScoreboardOverlayCheckBox_);

    clipCountLabel_ = new QLabel(settingsPage_);
    Style::setRole(clipCountLabel_, "muted");
    formLayout->addRow(QString(), clipCountLabel_);

    beforePaddingSpin_ = new QDoubleSpinBox(settingsPage_);
    beforePaddingSpin_->setRange(0.0, 30.0);
    beforePaddingSpin_->setValue(3.0);
    beforePaddingSpin_->setSuffix(QStringLiteral(" s"));
    beforePaddingSpin_->setSingleStep(0.5);
    beforePaddingSpin_->setDecimals(1);
    formLayout->addRow(AppLocale::trUi("export.before_tag"), beforePaddingSpin_);

    afterPaddingSpin_ = new QDoubleSpinBox(settingsPage_);
    afterPaddingSpin_->setRange(0.0, 30.0);
    afterPaddingSpin_->setValue(3.0);
    afterPaddingSpin_->setSuffix(QStringLiteral(" s"));
    afterPaddingSpin_->setSingleStep(0.5);
    afterPaddingSpin_->setDecimals(1);
    formLayout->addRow(AppLocale::trUi("export.after_tag"), afterPaddingSpin_);

    auto* pathRow = new QHBoxLayout();
    pathRow->setSpacing(8);
    outputPathEdit_ = new QLineEdit(settingsPage_);
    outputPathEdit_->setPlaceholderText(AppLocale::trUi("export.output_placeholder"));
    connect(outputPathEdit_, &QLineEdit::textChanged, this, [this](const QString& text) {
        if (text.trimmed().isEmpty()) {
            lastAutoOutputPathSuggestion_.clear();
        }
    });
    pathRow->addWidget(outputPathEdit_, 1);

    browseButton_ = new QPushButton(AppLocale::trUi("export.browse"), settingsPage_);
    browseButton_->setCursor(Qt::PointingHandCursor);
    Style::setVariant(browseButton_, "secondary");
    connect(browseButton_, &QPushButton::clicked, this, &ExportDialog::onBrowseOutputPath);
    pathRow->addWidget(browseButton_, 0);

    formLayout->addRow(AppLocale::trUi("export.save_to"), pathRow);

    layout->addLayout(formLayout);
    layout->addStretch(1);

    auto* buttonRow = new QHBoxLayout();
    buttonRow->setSpacing(8);
    buttonRow->addStretch(1);

    settingsCloseButton_ = new QPushButton(AppLocale::trUi("export.close"), settingsPage_);
    settingsCloseButton_->setCursor(Qt::PointingHandCursor);
    Style::setVariant(settingsCloseButton_, "outline");
    connect(settingsCloseButton_, &QPushButton::clicked, this, &QDialog::reject);
    buttonRow->addWidget(settingsCloseButton_);

    reviewButton_ = new QPushButton(AppLocale::trUi("export.review_clips"), settingsPage_);
    reviewButton_->setCursor(Qt::PointingHandCursor);
    reviewButton_->setDefault(true);
    Style::setVariant(reviewButton_, "primary");
    connect(reviewButton_, &QPushButton::clicked, this, &ExportDialog::onReviewClipsClicked);
    buttonRow->addWidget(reviewButton_);

    layout->addLayout(buttonRow);

    pagesStack_->addWidget(settingsPage_);
}

// ---------------------------------------------------------------------------
// Trim page
// ---------------------------------------------------------------------------

void ExportDialog::buildTrimPage() {
    trimPage_ = new QWidget(this);
    auto* layout = new QVBoxLayout(trimPage_);
    layout->setSpacing(8);
    layout->setContentsMargins(16, 16, 16, 16);

    // Navigation row
    auto* navRow = new QHBoxLayout();
    navRow->setSpacing(8);

    prevClipButton_ = new QPushButton(QStringLiteral("\u25C0"), trimPage_);
    prevClipButton_->setCursor(Qt::PointingHandCursor);
    Style::setVariant(prevClipButton_, "outline");
    Style::setSize(prevClipButton_, "xs");
    prevClipButton_->setFixedSize(44, 40);
    connect(prevClipButton_, &QPushButton::clicked, this, &ExportDialog::onPrevClipClicked);
    navRow->addWidget(prevClipButton_);

    clipNavigationLabel_ = new QLabel(trimPage_);
    clipNavigationLabel_->setAlignment(Qt::AlignCenter);
    navRow->addWidget(clipNavigationLabel_, 1);

    discardClipButton_ = new QPushButton(QStringLiteral("\U0001F5D1"), trimPage_);
    discardClipButton_->setCursor(Qt::PointingHandCursor);
    discardClipButton_->setToolTip(AppLocale::trUi("export.discard_clip"));
    Style::setVariant(discardClipButton_, "ghost");
    Style::setSize(discardClipButton_, "xs");
    discardClipButton_->setFixedSize(44, 40);
    connect(discardClipButton_, &QPushButton::clicked,
            this, &ExportDialog::onDiscardClipClicked);
    navRow->addWidget(discardClipButton_);

    nextClipButton_ = new QPushButton(QStringLiteral("\u25B6"), trimPage_);
    nextClipButton_->setCursor(Qt::PointingHandCursor);
    Style::setVariant(nextClipButton_, "outline");
    Style::setSize(nextClipButton_, "xs");
    nextClipButton_->setFixedSize(44, 40);
    connect(nextClipButton_, &QPushButton::clicked, this, &ExportDialog::onNextClipClicked);
    navRow->addWidget(nextClipButton_);

    layout->addLayout(navRow);

    // Video preview
    previewVideoWidget_ = new QVideoWidget(trimPage_);
    previewVideoWidget_->setAspectRatioMode(Qt::KeepAspectRatio);
    previewVideoWidget_->setMinimumHeight(400);
    previewVideoWidget_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    previewVideoWidget_->setStyleSheet(QStringLiteral("background-color: black;"));
    layout->addWidget(previewVideoWidget_, 1);

    previewControlsBar_ = new VideoControlsBar(trimPage_);
    layout->addWidget(previewControlsBar_);

    // Trim bar
    clipTrimBar_ = new ClipTrimBar(trimPage_);
    connect(clipTrimBar_, &ClipTrimBar::seekRequested,
            this, &ExportDialog::onTrimSeekRequested);
    connect(clipTrimBar_, &ClipTrimBar::inPointChanged, this, [this](qint64) {
        saveTrimForCurrentClip();
    });
    connect(clipTrimBar_, &ClipTrimBar::outPointChanged, this, [this](qint64) {
        saveTrimForCurrentClip();
    });
    layout->addWidget(clipTrimBar_);

    // Note overlay row
    auto* noteRow = new QHBoxLayout();
    noteRow->setSpacing(8);

    includeNoteCheckBox_ = new QCheckBox(
        AppLocale::trUi("export.include_note"), trimPage_);
    includeNoteCheckBox_->setCursor(Qt::PointingHandCursor);
    noteRow->addWidget(includeNoteCheckBox_);

    noteLineEdit_ = new QLineEdit(trimPage_);
    noteLineEdit_->setPlaceholderText(AppLocale::trUi("export.note_placeholder"));
    noteLineEdit_->setEnabled(false);
    noteRow->addWidget(noteLineEdit_, 1);

    connect(includeNoteCheckBox_, &QCheckBox::toggled,
            noteLineEdit_, &QLineEdit::setEnabled);

    layout->addLayout(noteRow);
    layout->addSpacing(4);

    // Progress
    progressBar_ = new QProgressBar(trimPage_);
    progressBar_->setRange(0, 100);
    progressBar_->setValue(0);
    progressBar_->hide();
    layout->addWidget(progressBar_);

    progressLabel_ = new QLabel(trimPage_);
    Style::setRole(progressLabel_, "muted");
    progressLabel_->hide();
    layout->addWidget(progressLabel_);

    // Button row
    auto* buttonRow = new QHBoxLayout();
    buttonRow->setSpacing(8);
    buttonRow->addStretch(1);

    backButton_ = new QPushButton(AppLocale::trUi("export.back"), trimPage_);
    backButton_->setCursor(Qt::PointingHandCursor);
    Style::setVariant(backButton_, "outline");
    connect(backButton_, &QPushButton::clicked, this, &ExportDialog::onBackToSettingsClicked);
    buttonRow->addWidget(backButton_);

    cancelExportButton_ = new QPushButton(AppLocale::trUi("export.cancel"), trimPage_);
    cancelExportButton_->setCursor(Qt::PointingHandCursor);
    cancelExportButton_->hide();
    Style::setVariant(cancelExportButton_, "destructive");
    connect(cancelExportButton_, &QPushButton::clicked,
            this, &ExportDialog::onCancelExportClicked);
    buttonRow->addWidget(cancelExportButton_);

    exportButton_ = new QPushButton(AppLocale::trUi("export.export"), trimPage_);
    exportButton_->setCursor(Qt::PointingHandCursor);
    exportButton_->setDefault(true);
    Style::setVariant(exportButton_, "primary");
    connect(exportButton_, &QPushButton::clicked, this, &ExportDialog::onExportClicked);
    buttonRow->addWidget(exportButton_);

    layout->addLayout(buttonRow);

    pagesStack_->addWidget(trimPage_);
}

// ---------------------------------------------------------------------------
// Settings page logic
// ---------------------------------------------------------------------------

void ExportDialog::populateEventTypes() {
    eventTypeCombo_->clear();
    if (!tagSession_) return;

    const auto& counts = tagSession_->mainEventCounts();
    QStringList eventTypes = counts.keys();
    eventTypes.sort(Qt::CaseInsensitive);

    for (const QString& eventType : eventTypes) {
        const int count = counts.value(eventType, 0);
        if (count <= 0) continue;
        const QString displayText = QStringLiteral("%1  (%2)")
            .arg(AppLocale::trEvent(eventType))
            .arg(count);
        eventTypeCombo_->addItem(displayText, eventType);
    }
}

void ExportDialog::onEventTypeChanged(int /*index*/) {
    updateClipCount();
}

void ExportDialog::onTeamFilterChanged(int /*index*/) {
    updateClipCount();
    updateSortOrderVisibility();
}

void ExportDialog::updateSortOrderVisibility() {
    const bool allTeams = teamFilterCombo_
        && teamFilterCombo_->currentData().toString().isEmpty();
    if (sortOrderLabel_) sortOrderLabel_->setVisible(allTeams);
    if (sortOrderCombo_) sortOrderCombo_->setVisible(allTeams);
}

void ExportDialog::updateClipCount() {
    if (!clipCountLabel_ || !tagSession_ || !eventTypeCombo_) return;

    const QString canonicalEvent = eventTypeCombo_->currentData().toString();
    if (canonicalEvent.isEmpty()) {
        clipCountLabel_->setText(QString());
        if (reviewButton_) reviewButton_->setEnabled(false);
        refreshOutputPathIfFollowingForm();
        return;
    }

    const QString teamFilter = teamFilterCombo_
        ? teamFilterCombo_->currentData().toString()
        : QString();

    int count = 0;
    for (const auto& tag : tagSession_->tags()) {
        if (tag.mainEvent != canonicalEvent) continue;
        if (!teamFilter.isEmpty() && tag.team != teamFilter) continue;
        ++count;
    }

    clipCountLabel_->setText(
        QStringLiteral("%1 %2").arg(count).arg(AppLocale::trUi("export.clips_label")));
    if (reviewButton_) reviewButton_->setEnabled(count > 0);

    refreshOutputPathIfFollowingForm();
}

void ExportDialog::onBrowseOutputPath() {
    const QString defaultPath = defaultExportSuggestedFilePath();

    const QString path = QFileDialog::getSaveFileName(
        this,
        AppLocale::trUi("export.save_dialog_title"),
        defaultPath,
        QStringLiteral("MP4 (*.mp4);;All files (*.*)"));

    if (!path.isEmpty()) {
        QSignalBlocker blocker(outputPathEdit_);
        outputPathEdit_->setText(path);
        lastAutoOutputPathSuggestion_.clear();
    }
}

// ---------------------------------------------------------------------------
// Settings → Trim transition
// ---------------------------------------------------------------------------

void ExportDialog::onReviewClipsClicked() {
    const QString canonicalEvent = eventTypeCombo_->currentData().toString();
    if (canonicalEvent.isEmpty()) return;

    buildTrimDataFromSettings();
    if (trimData_.isEmpty()) return;

    ensurePreviewPlayer();

    currentTrimIndex_ = 0;
    showClipAtIndex(0);
    pagesStack_->setCurrentWidget(trimPage_);
    updateTrimPageKeyboardShortcutsForCurrentPage();
    if (previewVideoWidget_) {
        previewVideoWidget_->setFocus(Qt::OtherFocusReason);
    }
}

void ExportDialog::buildTrimDataFromSettings() {
    trimData_.clear();

    const QString canonicalEvent = eventTypeCombo_->currentData().toString();
    const QString teamFilter = teamFilterCombo_
        ? teamFilterCombo_->currentData().toString()
        : QString();
    const bool sortByTeamFirst = teamFilter.isEmpty()
        && sortOrderCombo_
        && sortOrderCombo_->currentData().toString() == QStringLiteral("by_team");

    exportLanguage_ = exportLanguageCombo_
        ? static_cast<AppLocale::Language>(exportLanguageCombo_->currentData().toInt())
        : AppLocale::currentLanguage();

    struct TagWithIndex {
        TagSession::GameTag tag;
        int originalIndex;
    };
    QVector<TagWithIndex> matchingTags;
    const auto& allTags = tagSession_->tags();
    for (int i = 0; i < allTags.size(); ++i) {
        if (allTags[i].mainEvent != canonicalEvent) continue;
        if (!teamFilter.isEmpty() && allTags[i].team != teamFilter) continue;
        matchingTags.append({allTags[i], i});
    }

    if (sortByTeamFirst) {
        std::sort(matchingTags.begin(), matchingTags.end(),
                  [](const TagWithIndex& a, const TagWithIndex& b) {
            if (a.tag.team != b.tag.team) {
                if (a.tag.team == QStringLiteral("Home")) return true;
                if (b.tag.team == QStringLiteral("Home")) return false;
                return a.tag.team < b.tag.team;
            }
            return a.tag.positionMs < b.tag.positionMs;
        });
    } else {
        std::sort(matchingTags.begin(), matchingTags.end(),
                  [](const TagWithIndex& a, const TagWithIndex& b) {
            return a.tag.positionMs < b.tag.positionMs;
        });
    }

    const double beforePaddingMs = beforePaddingSpin_->value() * 1000.0;
    const double afterPaddingMs = afterPaddingSpin_->value() * 1000.0;
    const int totalClips = matchingTags.size();
    translatedEvent_ =
        AppLocale::trEventForLanguage(canonicalEvent, exportLanguage_);

    trimData_.reserve(totalClips);
    for (int i = 0; i < totalClips; ++i) {
        const auto& entry = matchingTags[i];
        qint64 clipStart = static_cast<qint64>(entry.tag.positionMs - beforePaddingMs);
        qint64 clipEnd = static_cast<qint64>(entry.tag.positionMs + afterPaddingMs);

        if (clipStart < 0) clipStart = 0;
        if (videoDurationMs_ > 0 && clipEnd > videoDurationMs_) clipEnd = videoDurationMs_;
        if (clipEnd <= clipStart) clipEnd = clipStart + 1000;

        const QString teamName = teamDisplayName(entry.tag.team);
        const QString overlayText = QStringLiteral("%1 - %2  %3 / %4")
            .arg(teamName, translatedEvent_)
            .arg(i + 1)
            .arg(totalClips);

        const bool hasNote = !entry.tag.note.trimmed().isEmpty();
        trimData_.append({entry.tag, clipStart, clipEnd, overlayText,
                          hasNote, entry.tag.note.trimmed()});
    }
}

void ExportDialog::onBackToSettingsClicked() {
    stopPreviewPlayer();
    pagesStack_->setCurrentWidget(settingsPage_);
    updateTrimPageKeyboardShortcutsForCurrentPage();
}

// ---------------------------------------------------------------------------
// Trim page: preview player
// ---------------------------------------------------------------------------

void ExportDialog::ensurePreviewPlayer() {
    if (previewPlayer_) return;

    previewAudioOutput_ = new QAudioOutput(this);
    previewAudioOutput_->setMuted(true);
    previewPlayer_ = new QMediaPlayer(this);
    previewPlayer_->setAudioOutput(previewAudioOutput_);
    previewPlayer_->setVideoOutput(previewVideoWidget_);
    previewPlayer_->setSource(QUrl::fromLocalFile(sourceVideoPath_));

    connect(previewPlayer_, &QMediaPlayer::positionChanged,
            this, &ExportDialog::onPreviewPositionChanged);

    connect(previewPlayer_, &QMediaPlayer::playbackStateChanged,
            this, [this](QMediaPlayer::PlaybackState state) {
        const bool playing = (state == QMediaPlayer::PlayingState);
        if (previewControlsBar_) {
            previewControlsBar_->setPlaying(playing);
        }
    });

    if (previewControlsBar_) {
        previewControlsBar_->setEnabledForMedia(true);
        previewControlsBar_->setPlaying(false);
        previewControlsBar_->setMuted(true);
        previewControlsBar_->setPlaybackRate(previewPlaybackRate_);
        previewPlayer_->setPlaybackRate(previewPlaybackRate_);

        connect(previewControlsBar_, &VideoControlsBar::playRequested,
                previewPlayer_, &QMediaPlayer::play);
        connect(previewControlsBar_, &VideoControlsBar::pauseRequested,
                previewPlayer_, &QMediaPlayer::pause);
        connect(previewControlsBar_, &VideoControlsBar::seekRequestedMs, this,
                [this](qint64 deltaMs) {
            if (!previewPlayer_) return;
            const qint64 durationMs = previewPlayer_->duration();
            const qint64 positionMs = previewPlayer_->position();
            const qint64 nextMs = positionMs + deltaMs;
            const qint64 targetMs = (durationMs > 0)
                ? std::clamp(nextMs, qint64{0}, durationMs)
                : std::max<qint64>(0, nextMs);
            previewPlayer_->setPosition(targetMs);
        });
        connect(previewControlsBar_, &VideoControlsBar::slowerRequested,
                this, &ExportDialog::onPreviewSlowerClicked);
        connect(previewControlsBar_, &VideoControlsBar::fasterRequested,
                this, &ExportDialog::onPreviewFasterClicked);
        connect(previewControlsBar_, &VideoControlsBar::resetSpeedRequested,
                this, &ExportDialog::onPreviewResetSpeedClicked);
        connect(previewControlsBar_, &VideoControlsBar::muteToggled, this,
                [this](bool muted) {
            if (previewAudioOutput_) previewAudioOutput_->setMuted(muted);
        });
    }
}

void ExportDialog::stopPreviewPlayer() {
    if (previewPlayer_) {
        previewPlayer_->stop();
    }
    if (previewControlsBar_) {
        previewControlsBar_->setPlaying(false);
    }
}

void ExportDialog::applyPreviewPlaybackRate() {
    if (previewPlayer_) {
        previewPlayer_->setPlaybackRate(previewPlaybackRate_);
    }
    if (previewControlsBar_) {
        previewControlsBar_->setPlaybackRate(previewPlaybackRate_);
    }
}

void ExportDialog::onPreviewSlowerClicked() {
    previewPlaybackRate_ = std::max(kPreviewMinPlaybackRate,
                                    previewPlaybackRate_ - kPreviewPlaybackRateStep);
    applyPreviewPlaybackRate();
}

void ExportDialog::onPreviewFasterClicked() {
    previewPlaybackRate_ = std::min(kPreviewMaxPlaybackRate,
                                    previewPlaybackRate_ + kPreviewPlaybackRateStep);
    applyPreviewPlaybackRate();
}

void ExportDialog::onPreviewResetSpeedClicked() {
    previewPlaybackRate_ = 1.0;
    applyPreviewPlaybackRate();
}

void ExportDialog::onTogglePreviewPlayPause() {
    if (!previewPlayer_) return;
    if (previewPlayer_->playbackState() == QMediaPlayer::PlayingState) {
        previewPlayer_->pause();
    } else {
        previewPlayer_->play();
    }
}

void ExportDialog::onTrimSeekRequested(qint64 posMs) {
    if (!previewPlayer_) return;
    previewPlayer_->pause();
    previewPlayer_->setPosition(posMs);
}

void ExportDialog::onPreviewPositionChanged(qint64 posMs) {
    if (clipTrimBar_) {
        clipTrimBar_->setPlayheadMs(posMs);
    }
}

// ---------------------------------------------------------------------------
// Trim page: clip navigation
// ---------------------------------------------------------------------------

void ExportDialog::showClipAtIndex(int index) {
    if (index < 0 || index >= trimData_.size()) return;
    currentTrimIndex_ = index;

    const auto& clip = trimData_.at(index);

    const qint64 beforePaddingMs =
        static_cast<qint64>(beforePaddingSpin_->value() * 1000.0);
    const qint64 afterPaddingMs =
        static_cast<qint64>(afterPaddingSpin_->value() * 1000.0);
    const qint64 halfWindow = std::max(qint64{25000},
        std::max(beforePaddingMs, afterPaddingMs) + 5000);

    qint64 windowStart = clip.tag.positionMs - halfWindow;
    qint64 windowEnd = clip.tag.positionMs + halfWindow;
    if (windowStart < 0) windowStart = 0;
    if (videoDurationMs_ > 0 && windowEnd > videoDurationMs_)
        windowEnd = videoDurationMs_;

    if (clipTrimBar_) {
        clipTrimBar_->configure(clip.tag.positionMs, clip.startMs, clip.endMs,
                                windowStart, windowEnd);
    }

    if (previewPlayer_) {
        previewPlayer_->pause();
        previewPlayer_->setPosition(clip.startMs);
        previewPlayer_->setPlaybackRate(previewPlaybackRate_);
    }

    if (includeNoteCheckBox_) {
        includeNoteCheckBox_->setChecked(clip.includeSecondaryOverlay);
    }
    if (noteLineEdit_) {
        noteLineEdit_->setText(clip.secondaryOverlayText);
        noteLineEdit_->setEnabled(clip.includeSecondaryOverlay);
    }

    updateClipNavigation();
}

void ExportDialog::saveTrimForCurrentClip() {
    if (currentTrimIndex_ < 0 || currentTrimIndex_ >= trimData_.size()) return;
    if (clipTrimBar_) {
        trimData_[currentTrimIndex_].startMs = clipTrimBar_->inPointMs();
        trimData_[currentTrimIndex_].endMs = clipTrimBar_->outPointMs();
    }
    if (includeNoteCheckBox_) {
        trimData_[currentTrimIndex_].includeSecondaryOverlay =
            includeNoteCheckBox_->isChecked();
    }
    if (noteLineEdit_) {
        trimData_[currentTrimIndex_].secondaryOverlayText =
            noteLineEdit_->text().trimmed();
    }
}

void ExportDialog::onPrevClipClicked() {
    if (currentTrimIndex_ <= 0) return;
    saveTrimForCurrentClip();
    showClipAtIndex(currentTrimIndex_ - 1);
}

void ExportDialog::onNextClipClicked() {
    if (currentTrimIndex_ >= trimData_.size() - 1) return;
    saveTrimForCurrentClip();
    showClipAtIndex(currentTrimIndex_ + 1);
}

void ExportDialog::updateClipNavigation() {
    const int total = trimData_.size();
    const int current = currentTrimIndex_ + 1;

    if (clipNavigationLabel_) {
        clipNavigationLabel_->setText(
            QStringLiteral("%1 %2 / %3")
                .arg(AppLocale::trUi("export.clip_label"))
                .arg(current)
                .arg(total));
    }
    if (prevClipButton_) prevClipButton_->setEnabled(currentTrimIndex_ > 0);
    if (nextClipButton_) nextClipButton_->setEnabled(currentTrimIndex_ < total - 1);
}

void ExportDialog::onDiscardClipClicked() {
    if (trimData_.isEmpty()) return;

    saveTrimForCurrentClip();
    trimData_.remove(currentTrimIndex_);

    if (trimData_.isEmpty()) {
        stopPreviewPlayer();
        pagesStack_->setCurrentWidget(settingsPage_);
        updateTrimPageKeyboardShortcutsForCurrentPage();
        QMessageBox::information(this,
            AppLocale::trUi("export.title"),
            AppLocale::trUi("export.all_clips_discarded"));
        return;
    }

    regenerateOverlayTexts();

    if (currentTrimIndex_ >= trimData_.size()) {
        currentTrimIndex_ = trimData_.size() - 1;
    }
    showClipAtIndex(currentTrimIndex_);
}

void ExportDialog::regenerateOverlayTexts() {
    const int totalClips = trimData_.size();
    for (int i = 0; i < totalClips; ++i) {
        const QString teamName = teamDisplayName(trimData_[i].tag.team);
        trimData_[i].overlayText = QStringLiteral("%1 - %2  %3 / %4")
            .arg(teamName, translatedEvent_)
            .arg(i + 1)
            .arg(totalClips);
    }
}

QString ExportDialog::teamDisplayName(const QString& teamKey) const {
    if (teamKey == QStringLiteral("Home")) {
        return (tagSession_ && !tagSession_->homeTeamName().isEmpty())
            ? tagSession_->homeTeamName()
            : AppLocale::trUi("export.team_home_default");
    }
    if (teamKey == QStringLiteral("Away")) {
        return (tagSession_ && !tagSession_->awayTeamName().isEmpty())
            ? tagSession_->awayTeamName()
            : AppLocale::trUi("export.team_away_default");
    }
    return teamKey;
}

QString ExportDialog::sanitizedExportFileNamePart(const QString& raw) const {
    QString segment = raw.trimmed();
    const QString forbidden = QStringLiteral("\\/:*?\"<>|\r\n\t");
    for (QChar character : forbidden) {
        segment.replace(character, QLatin1Char('_'));
    }
    while (segment.contains(QStringLiteral("  "))) {
        segment.replace(QStringLiteral("  "), QStringLiteral(" "));
    }
    if (segment.isEmpty()) {
        return QStringLiteral("clip");
    }
    return segment;
}

QString ExportDialog::suggestedExportBaseName() const {
    const QString homeSegment =
        sanitizedExportFileNamePart(teamDisplayName(QStringLiteral("Home")));
    const QString awaySegment =
        sanitizedExportFileNamePart(teamDisplayName(QStringLiteral("Away")));
    const QString canonicalEvent =
        eventTypeCombo_ ? eventTypeCombo_->currentData().toString() : QString();
    const QString eventSegment = canonicalEvent.isEmpty()
        ? sanitizedExportFileNamePart(QStringLiteral("clips"))
        : sanitizedExportFileNamePart(eventLabelForExportSuggestedFileName(canonicalEvent));
    const QString teamChoiceSegment = teamFilterCombo_
        ? sanitizedExportFileNamePart(teamFilterCombo_->currentText())
        : sanitizedExportFileNamePart(AppLocale::trUi("export.team_all"));
    return QStringLiteral("%1 vs %2 - %3 %4")
        .arg(homeSegment, awaySegment, eventSegment, teamChoiceSegment);
}

QString ExportDialog::defaultExportSuggestedFilePath() const {
    const QFileInfo sourceInfo(sourceVideoPath_);
    const QString directoryPath = sourceInfo.absolutePath();
    return QDir(directoryPath).filePath(suggestedExportBaseName()
        + QStringLiteral(".mp4"));
}

void ExportDialog::applySuggestedOutputPathFromForm() {
    if (!outputPathEdit_) return;
    const QString suggestedPath = defaultExportSuggestedFilePath();
    {
        QSignalBlocker blocker(outputPathEdit_);
        outputPathEdit_->setText(suggestedPath);
    }
    lastAutoOutputPathSuggestion_ = suggestedPath;
}

void ExportDialog::refreshOutputPathIfFollowingForm() {
    if (!outputPathEdit_ || sourceVideoPath_.isEmpty()) return;

    const QString canonicalEvent =
        eventTypeCombo_ ? eventTypeCombo_->currentData().toString() : QString();
    const QString currentPath = outputPathEdit_->text();

    if (canonicalEvent.isEmpty()) {
        if (currentPath.trimmed().isEmpty() || currentPath == lastAutoOutputPathSuggestion_) {
            QSignalBlocker blocker(outputPathEdit_);
            outputPathEdit_->clear();
            lastAutoOutputPathSuggestion_.clear();
        }
        return;
    }

    if (!currentPath.trimmed().isEmpty() && currentPath != lastAutoOutputPathSuggestion_) {
        return;
    }

    applySuggestedOutputPathFromForm();
}

// ---------------------------------------------------------------------------
// Export
// ---------------------------------------------------------------------------

void ExportDialog::onExportClicked() {
    saveTrimForCurrentClip();

    const QString outputPath = outputPathEdit_->text().trimmed();
    if (outputPath.isEmpty()) {
        onBrowseOutputPath();
        if (outputPathEdit_->text().trimmed().isEmpty()) return;
    }

    const QString ffmpegPath = ClipExporter::findFfmpeg();
    if (ffmpegPath.isEmpty()) {
        QMessageBox::critical(this,
            AppLocale::trUi("export.title"),
            AppLocale::trUi("export.ffmpeg_not_found"));
        return;
    }

    if (trimData_.isEmpty()) return;

    const QString homeName = teamDisplayName(QStringLiteral("Home"));
    const QString awayName = teamDisplayName(QStringLiteral("Away"));
    const QString homeColorHex = tagSession_ ? tagSession_->homeTeamColor() : QString();
    const QString awayColorHex = tagSession_ ? tagSession_->awayTeamColor() : QString();

    const QVector<TagSession::GameTag> emptyTags;
    const auto& allTags = tagSession_ ? tagSession_->tags() : emptyTags;

    QVector<ClipSegment> clips;
    clips.reserve(trimData_.size());
    const bool includeBottomOverlay =
        !includeBottomOverlayCheckBox_ || includeBottomOverlayCheckBox_->isChecked();
    const bool includeScoreboardOverlay =
        !includeScoreboardOverlayCheckBox_ || includeScoreboardOverlayCheckBox_->isChecked();
    for (const auto& td : trimData_) {
        const QString secondary =
            (td.includeSecondaryOverlay && !td.secondaryOverlayText.isEmpty())
                ? td.secondaryOverlayText : QString();
        const QString primary = includeBottomOverlay ? td.overlayText : QString();
        const QString secondaryText = includeBottomOverlay ? secondary : QString();

        QVector<TimedScoreboard> scoreboardPhases;
        if (includeScoreboardOverlay) {
            int initialHomeGoals = 0;
            int initialAwayGoals = 0;
            struct InClipGoal {
                qint64 positionMs;
                QString team;
            };
            QVector<InClipGoal> inClipGoals;

            for (const auto& tag : allTags) {
                if (tag.mainEvent != QStringLiteral("Goal")) continue;
                if (tag.positionMs <= td.startMs) {
                    if (tag.team == QStringLiteral("Home")) ++initialHomeGoals;
                    else if (tag.team == QStringLiteral("Away")) ++initialAwayGoals;
                } else if (tag.positionMs <= td.endMs) {
                    inClipGoals.append({tag.positionMs, tag.team});
                }
            }

            std::sort(inClipGoals.begin(), inClipGoals.end(),
                      [](const InClipGoal& a, const InClipGoal& b) {
                return a.positionMs < b.positionMs;
            });

            scoreboardPhases.append({0.0, {homeName, awayName,
                                           initialHomeGoals, initialAwayGoals,
                                           homeColorHex, awayColorHex}});

            int runningHome = initialHomeGoals;
            int runningAway = initialAwayGoals;
            for (const auto& goal : inClipGoals) {
                if (goal.team == QStringLiteral("Home")) ++runningHome;
                else if (goal.team == QStringLiteral("Away")) ++runningAway;

                const double offsetSeconds = (goal.positionMs - td.startMs) / 1000.0;
                if (scoreboardPhases.last().activationOffsetSeconds == offsetSeconds) {
                    scoreboardPhases.last().scoreboard.homeGoals = runningHome;
                    scoreboardPhases.last().scoreboard.awayGoals = runningAway;
                } else {
                    scoreboardPhases.append({offsetSeconds, {homeName, awayName,
                                                             runningHome, runningAway,
                                                             homeColorHex, awayColorHex}});
                }
            }
        }

        clips.append({td.startMs, td.endMs - td.startMs, primary,
                      secondaryText, scoreboardPhases});
    }

    if (exporter_) {
        exporter_->deleteLater();
    }
    exporter_ = new ClipExporter(this);
    exporter_->setSourceVideo(sourceVideoPath_);
    exporter_->setOutputPath(outputPathEdit_->text().trimmed());
    exporter_->setClips(clips);

    connect(exporter_, &ClipExporter::progressChanged,
            this, &ExportDialog::onExportProgress);
    connect(exporter_, &ClipExporter::exportFinished,
            this, &ExportDialog::onExportFinished);

    stopPreviewPlayer();
    setExporting(true);
    exporter_->startExport();
}

void ExportDialog::onCancelExportClicked() {
    if (exporter_) {
        exporter_->cancelExport();
    }
    setExporting(false);
}

void ExportDialog::onExportProgress(int currentClip, int totalClips) {
    if (progressBar_) {
        progressBar_->setMaximum(totalClips);
        progressBar_->setValue(currentClip);
    }
    if (progressLabel_) {
        progressLabel_->setText(
            QStringLiteral("%1 %2 / %3...")
                .arg(AppLocale::trUi("export.progress_prefix"))
                .arg(currentClip)
                .arg(totalClips));
    }
}

void ExportDialog::onExportFinished(bool success, const QString& message) {
    setExporting(false);

    if (success) {
        if (progressBar_) {
            progressBar_->setValue(progressBar_->maximum());
        }
        if (progressLabel_) {
            progressLabel_->setText(AppLocale::trUi("export.done"));
            progressLabel_->show();
        }
        QMessageBox::information(this,
            AppLocale::trUi("export.title"),
            AppLocale::trUi("export.success"));
    } else {
        if (progressLabel_) progressLabel_->hide();
        if (progressBar_) {
            progressBar_->setValue(0);
            progressBar_->hide();
        }
        if (!message.contains(QStringLiteral("cancelled"), Qt::CaseInsensitive)) {
            QMessageBox::critical(this,
                AppLocale::trUi("export.title"),
                message);
        }
    }
}

void ExportDialog::setExporting(bool exporting) {
    exportInProgress_ = exporting;

    if (prevClipButton_) prevClipButton_->setEnabled(!exporting);
    if (nextClipButton_) nextClipButton_->setEnabled(!exporting);
    if (discardClipButton_) discardClipButton_->setEnabled(!exporting);
    if (previewControlsBar_) previewControlsBar_->setEnabled(!exporting);
    if (clipTrimBar_) clipTrimBar_->setEnabled(!exporting);
    if (includeNoteCheckBox_) includeNoteCheckBox_->setEnabled(!exporting);
    if (noteLineEdit_) noteLineEdit_->setEnabled(!exporting && includeNoteCheckBox_->isChecked());
    if (exportButton_) exportButton_->setVisible(!exporting);
    if (backButton_) backButton_->setVisible(!exporting);
    if (cancelExportButton_) cancelExportButton_->setVisible(exporting);

    if (exporting) {
        if (progressBar_) {
            progressBar_->setValue(0);
            progressBar_->show();
        }
        if (progressLabel_) {
            progressLabel_->setText(AppLocale::trUi("export.starting"));
            progressLabel_->show();
        }
    }

    updateTrimPageKeyboardShortcutsForCurrentPage();
}

void ExportDialog::updateTrimPageKeyboardShortcutsForCurrentPage() {
    const bool wantShortcuts = pagesStack_
        && pagesStack_->currentWidget() == trimPage_
        && !exportInProgress_;
    if (wantShortcuts) {
        attachTrimPageKeyboardShortcuts();
    } else {
        detachTrimPageKeyboardShortcuts();
    }
}

void ExportDialog::attachTrimPageKeyboardShortcuts() {
    if (trimKeyboardShortcutsInstalled_) return;
    if (QApplication* application = qApp) {
        application->installEventFilter(this);
    }
    trimKeyboardShortcutsInstalled_ = true;
}

void ExportDialog::detachTrimPageKeyboardShortcuts() {
    if (!trimKeyboardShortcutsInstalled_) return;
    if (QApplication* application = qApp) {
        application->removeEventFilter(this);
    }
    trimKeyboardShortcutsInstalled_ = false;
}

bool ExportDialog::eventFilter(QObject* watched, QEvent* event) {
    if (!trimKeyboardShortcutsInstalled_
        || event->type() != QEvent::KeyPress
        || !trimPage_
        || !pagesStack_
        || pagesStack_->currentWidget() != trimPage_) {
        return false;
    }

    QWidget* targetWidget = qobject_cast<QWidget*>(watched);
    if (!targetWidget) return false;
    if (targetWidget != trimPage_ && !trimPage_->isAncestorOf(targetWidget)) return false;

    auto* keyEvent = static_cast<QKeyEvent*>(event);

    if (keyEvent->key() == Qt::Key_Tab || keyEvent->key() == Qt::Key_Backtab) {
        QWidget* focusWidget = QApplication::focusWidget();
        if (focusWidget == noteLineEdit_ && noteLineEdit_->isEnabled()) {
            return false;
        }
        const bool shift = (keyEvent->key() == Qt::Key_Backtab)
            || (keyEvent->modifiers() & Qt::ShiftModifier);
        if (shift) {
            if (prevClipButton_ && prevClipButton_->isEnabled()) {
                onPrevClipClicked();
                return true;
            }
        } else {
            if (nextClipButton_ && nextClipButton_->isEnabled()) {
                onNextClipClicked();
                return true;
            }
        }
        return false;
    }

    if (keyEvent->key() == Qt::Key_Space && keyEvent->modifiers() == Qt::NoModifier) {
        QWidget* focusWidget = QApplication::focusWidget();
        if (focusWidget == noteLineEdit_ && noteLineEdit_->isEnabled()) {
            return false;
        }
        if (qobject_cast<QAbstractSpinBox*>(focusWidget)) {
            return false;
        }
        // Space always toggles preview playback on the trim page; never activate the focused QPushButton
        // (e.g. discard) as if it were the default button.
        if (previewPlayer_ && previewControlsBar_ && previewControlsBar_->isEnabled()) {
            const auto state = previewPlayer_->playbackState();
            if (state == QMediaPlayer::PlayingState) {
                previewControlsBar_->flashPauseButton();
            } else {
                previewControlsBar_->flashPlayButton();
            }
            onTogglePreviewPlayPause();
            return true;
        }
        return false;
    }

    return false;
}
