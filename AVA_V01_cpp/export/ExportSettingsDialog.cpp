#include "ExportSettingsDialog.h"

#include "ClipExporter.h"
#include "ExportClipBuilder.h"
#include "TagSession.h"
#include "AppLocale.h"
#include "StyleProps.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace {

QString extensionForOutputFormat(ExportOutputFormat format) {
    return format == ExportOutputFormat::Xml ? QStringLiteral(".xml") : QStringLiteral(".mp4");
}

}  // namespace

ExportSettingsDialog::ExportSettingsDialog(TagSession* session,
                                           const QString& sourceVideoPath,
                                           const QString& defaultOutputDirectoryPath,
                                           const QVector<PresentationQueue::Clip>& queuedClips,
                                           const QStringList& occupiedOutputPaths,
                                           QWidget* parent)
    : QDialog(parent)
    , tagSession_(session)
    , sourceVideoPath_(sourceVideoPath)
    , defaultOutputDirectoryPath_(defaultOutputDirectoryPath)
    , queuedClips_(queuedClips)
    , occupiedOutputPaths_(occupiedOutputPaths)
{
    setWindowTitle(AppLocale::trUi("export.title"));
    setMinimumSize(640, 560);
    resize(720, 620);

    buildUi();
    updateUiForFormat();
}

void ExportSettingsDialog::buildUi() {
    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(16);
    layout->setContentsMargins(24, 24, 24, 24);

    auto* titleLabel = new QLabel(AppLocale::trUi("export.title"), this);
    Style::setRole(titleLabel, "h2");
    layout->addWidget(titleLabel);

    auto* subtitleLabel = new QLabel(AppLocale::trUi("export.subtitle"), this);
    Style::setRole(subtitleLabel, "muted");
    subtitleLabel->setWordWrap(true);
    layout->addWidget(subtitleLabel);

    layout->addSpacing(4);

    auto* formLayout = new QFormLayout();
    formLayout->setSpacing(10);
    formLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    outputFormatCombo_ = new QComboBox(this);
    outputFormatCombo_->setMinimumWidth(200);
    outputFormatCombo_->addItem(AppLocale::trUi("export.format_mp4"),
                                static_cast<int>(ExportOutputFormat::Mp4));
    outputFormatCombo_->addItem(AppLocale::trUi("export.format_xml"),
                                static_cast<int>(ExportOutputFormat::Xml));
    outputFormatCombo_->addItem(AppLocale::trUi("export.format_both"),
                                static_cast<int>(ExportOutputFormat::Both));
    outputFormatCombo_->setCurrentIndex(0);
    connect(outputFormatCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ExportSettingsDialog::onOutputFormatChanged);
    formLayout->addRow(AppLocale::trUi("export.output_format"), outputFormatCombo_);

    sortOrderLabel_ = new QLabel(AppLocale::trUi("export.sort_order"), this);
    sortOrderCombo_ = new QComboBox(this);
    sortOrderCombo_->setMinimumWidth(200);
    sortOrderCombo_->addItem(AppLocale::trUi("export.sort_chronological"),
                             QStringLiteral("chronological"));
    sortOrderCombo_->addItem(AppLocale::trUi("export.sort_by_team"),
                             QStringLiteral("by_team"));
    sortOrderLabel_->hide();
    sortOrderCombo_->hide();
    formLayout->addRow(sortOrderLabel_, sortOrderCombo_);

    exportLanguageCombo_ = new QComboBox(this);
    exportLanguageCombo_->setMinimumWidth(200);
    exportLanguageCombo_->addItem(AppLocale::trUi("setup.lang_en"),
                                  static_cast<int>(AppLocale::Language::English));
    exportLanguageCombo_->addItem(AppLocale::trUi("setup.lang_es"),
                                  static_cast<int>(AppLocale::Language::Spanish));
    exportLanguageCombo_->setCurrentIndex(
        AppLocale::currentLanguage() == AppLocale::Language::Spanish ? 1 : 0);
    formLayout->addRow(AppLocale::trUi("export.overlay_language"), exportLanguageCombo_);

    includeBottomOverlayCheckBox_ =
        new QCheckBox(AppLocale::trUi("export.include_bottom_overlay"), this);
    includeBottomOverlayCheckBox_->setCursor(Qt::PointingHandCursor);
    includeBottomOverlayCheckBox_->setChecked(true);
    formLayout->addRow(QString(), includeBottomOverlayCheckBox_);

    includeScoreboardOverlayCheckBox_ =
        new QCheckBox(AppLocale::trUi("export.include_scoreboard_overlay"), this);
    includeScoreboardOverlayCheckBox_->setCursor(Qt::PointingHandCursor);
    includeScoreboardOverlayCheckBox_->setChecked(true);
    formLayout->addRow(QString(), includeScoreboardOverlayCheckBox_);

    includeNotesCheckBox_ =
        new QCheckBox(AppLocale::trUi("export.include_note"), this);
    includeNotesCheckBox_->setCursor(Qt::PointingHandCursor);
    includeNotesCheckBox_->setChecked(true);
    formLayout->addRow(QString(), includeNotesCheckBox_);

    includeAudioTrackCheckBox_ =
        new QCheckBox(AppLocale::trUi("export.include_audio_track"), this);
    includeAudioTrackCheckBox_->setCursor(Qt::PointingHandCursor);
    includeAudioTrackCheckBox_->setChecked(true);
    formLayout->addRow(QString(), includeAudioTrackCheckBox_);

    includeBrandingOverlayCheckBox_ =
        new QCheckBox(AppLocale::trUi("export.include_ava_overlay"), this);
    includeBrandingOverlayCheckBox_->setCursor(Qt::PointingHandCursor);
    includeBrandingOverlayCheckBox_->setChecked(true);
    formLayout->addRow(QString(), includeBrandingOverlayCheckBox_);

    clipCountLabel_ = new QLabel(this);
    Style::setRole(clipCountLabel_, "muted");
    formLayout->addRow(QString(), clipCountLabel_);

    auto* pathRow = new QHBoxLayout();
    pathRow->setSpacing(8);
    outputPathEdit_ = new QLineEdit(this);
    outputPathEdit_->setPlaceholderText(AppLocale::trUi("export.output_placeholder"));
    connect(outputPathEdit_, &QLineEdit::textChanged, this, [this](const QString& text) {
        const QString trimmed = text.trimmed();
        if (trimmed.isEmpty()) {
            lastAutoOutputPathSuggestion_.clear();
            outputPathFollowsSuggestion_ = true;
            return;
        }
        if (trimmed != lastAutoOutputPathSuggestion_) {
            outputPathFollowsSuggestion_ = false;
        }
    });
    pathRow->addWidget(outputPathEdit_, 1);

    browseButton_ = new QPushButton(AppLocale::trUi("export.browse"), this);
    browseButton_->setCursor(Qt::PointingHandCursor);
    Style::setVariant(browseButton_, "secondary");
    connect(browseButton_, &QPushButton::clicked, this, &ExportSettingsDialog::onBrowseOutputPath);
    pathRow->addWidget(browseButton_, 0);
    formLayout->addRow(AppLocale::trUi("export.save_to"), pathRow);

    layout->addLayout(formLayout);
    layout->addStretch(1);

    auto* buttonRow = new QHBoxLayout();
    buttonRow->setSpacing(8);
    buttonRow->addStretch(1);

    closeButton_ = new QPushButton(AppLocale::trUi("export.close"), this);
    closeButton_->setCursor(Qt::PointingHandCursor);
    Style::setVariant(closeButton_, "outline");
    connect(closeButton_, &QPushButton::clicked, this, &QDialog::reject);
    buttonRow->addWidget(closeButton_);

    exportButton_ = new QPushButton(AppLocale::trUi("export.export"), this);
    exportButton_->setCursor(Qt::PointingHandCursor);
    exportButton_->setDefault(true);
    Style::setVariant(exportButton_, "primary");
    connect(exportButton_, &QPushButton::clicked, this, &ExportSettingsDialog::onExportClicked);
    buttonRow->addWidget(exportButton_);

    layout->addLayout(buttonRow);
}

ExportOutputFormat ExportSettingsDialog::selectedOutputFormat() const {
    if (!outputFormatCombo_) return ExportOutputFormat::Mp4;
    return static_cast<ExportOutputFormat>(outputFormatCombo_->currentData().toInt());
}

bool ExportSettingsDialog::queuedClipsHaveMixedTeams() const {
    if (queuedClips_.size() < 2) return false;
    const QString firstTeam = queuedClips_.first().team;
    for (const auto& clip : queuedClips_) {
        if (clip.team != firstTeam) return true;
    }
    return false;
}

void ExportSettingsDialog::onOutputFormatChanged(int /*index*/) {
    updateUiForFormat();
}

void ExportSettingsDialog::updateUiForFormat() {
    const ExportOutputFormat format = selectedOutputFormat();
    const bool isXmlOnly = format == ExportOutputFormat::Xml;

    if (outputPathEdit_) {
        QString placeholder;
        switch (format) {
            case ExportOutputFormat::Mp4:
                placeholder = AppLocale::trUi("export.output_placeholder");
                break;
            case ExportOutputFormat::Xml:
                placeholder = AppLocale::trUi("export.output_placeholder_xml");
                break;
            case ExportOutputFormat::Both:
                placeholder = AppLocale::trUi("export.output_placeholder_both");
                break;
        }
        outputPathEdit_->setPlaceholderText(placeholder);
    }

    const QString xmlTooltip = isXmlOnly
        ? AppLocale::trUi("export.xml_filters_ignored_tooltip")
        : QString();
    const bool showSortOrder = !isXmlOnly && queuedClipsHaveMixedTeams();

    auto setOverlayEnabled = [isXmlOnly, xmlTooltip](QWidget* widget) {
        if (!widget) return;
        widget->setEnabled(!isXmlOnly);
        widget->setToolTip(xmlTooltip);
    };
    setOverlayEnabled(sortOrderLabel_);
    setOverlayEnabled(sortOrderCombo_);
    setOverlayEnabled(exportLanguageCombo_);
    setOverlayEnabled(includeBottomOverlayCheckBox_);
    setOverlayEnabled(includeScoreboardOverlayCheckBox_);
    setOverlayEnabled(includeNotesCheckBox_);
    setOverlayEnabled(includeAudioTrackCheckBox_);
    setOverlayEnabled(includeBrandingOverlayCheckBox_);

    if (sortOrderLabel_) sortOrderLabel_->setVisible(showSortOrder);
    if (sortOrderCombo_) sortOrderCombo_->setVisible(showSortOrder);

    if (clipCountLabel_) {
        if (isXmlOnly) {
            const int tagCount = tagSession_ ? tagSession_->tags().size() : 0;
            clipCountLabel_->setText(
                QStringLiteral("%1 %2")
                    .arg(tagCount)
                    .arg(AppLocale::trUi("export.xml_instances_label")));
            if (exportButton_) exportButton_->setEnabled(tagCount > 0);
        } else {
            clipCountLabel_->setText(
                QStringLiteral("%1 %2")
                    .arg(queuedClips_.size())
                    .arg(AppLocale::trUi("export.clips_label")));
            if (exportButton_) exportButton_->setEnabled(!queuedClips_.isEmpty());
        }
    }

    refreshOutputPathIfFollowingForm();
}

QString ExportSettingsDialog::suggestedBaseName() const {
    if (selectedOutputFormat() == ExportOutputFormat::Xml) {
        if (!tagSession_) return QString();
        return ExportClipBuilder::xmlReportBaseName(tagSession_);
    }
    return ExportClipBuilder::compilationBaseName(tagSession_, queuedClips_);
}

QString ExportSettingsDialog::defaultSuggestedFilePath() const {
    const QString baseName = suggestedBaseName();
    if (baseName.isEmpty()) return QString();

    const QFileInfo sourceInfo(sourceVideoPath_);
    QString directoryPath = defaultOutputDirectoryPath_.trimmed();
    if (directoryPath.isEmpty()) {
        directoryPath = sourceInfo.absolutePath();
    }
    // Mp4 and Both use .mp4 as the user-chosen path; ExportJobManager derives the XML path
    // alongside the MP4 when format is Both.
    return QDir(directoryPath).filePath(baseName + extensionForOutputFormat(selectedOutputFormat()));
}

void ExportSettingsDialog::applySuggestedOutputPathFromForm() {
    if (!outputPathEdit_) return;
    const QString suggestedPath = defaultSuggestedFilePath();
    if (suggestedPath.isEmpty()) return;
    {
        QSignalBlocker blocker(outputPathEdit_);
        outputPathEdit_->setText(suggestedPath);
    }
    lastAutoOutputPathSuggestion_ = suggestedPath;
    outputPathFollowsSuggestion_ = true;
}

void ExportSettingsDialog::refreshOutputPathIfFollowingForm() {
    if (!outputPathEdit_ || sourceVideoPath_.isEmpty()) return;
    if (selectedOutputFormat() == ExportOutputFormat::Xml && !tagSession_) return;

    const QString suggestedPath = defaultSuggestedFilePath();
    if (suggestedPath.isEmpty()) return;

    const QString currentPath = outputPathEdit_->text().trimmed();
    if (outputPathFollowsSuggestion_ || currentPath.isEmpty()) {
        applySuggestedOutputPathFromForm();
        return;
    }

    const QString suggestedBase = suggestedBaseName();
    if (suggestedBase.isEmpty()) return;

    const QFileInfo currentInfo(currentPath);
    if (currentInfo.completeBaseName() != suggestedBase) return;

    const QString updatedPath = QDir(currentInfo.absolutePath())
        .filePath(suggestedBase + extensionForOutputFormat(selectedOutputFormat()));
    if (updatedPath == currentPath) return;

    {
        QSignalBlocker blocker(outputPathEdit_);
        outputPathEdit_->setText(updatedPath);
    }
    lastAutoOutputPathSuggestion_ = updatedPath;
    outputPathFollowsSuggestion_ = true;
}

void ExportSettingsDialog::onBrowseOutputPath() {
    const QString defaultPath = defaultSuggestedFilePath();
    QString filter;
    switch (selectedOutputFormat()) {
        case ExportOutputFormat::Xml:
            filter = QStringLiteral("XML (*.xml);;All files (*.*)");
            break;
        case ExportOutputFormat::Both:
        case ExportOutputFormat::Mp4:
            filter = QStringLiteral("MP4 (*.mp4);;All files (*.*)");
            break;
    }

    const QString path = QFileDialog::getSaveFileName(
        this,
        AppLocale::trUi("export.save_dialog_title"),
        defaultPath,
        filter);

    if (!path.isEmpty()) {
        QSignalBlocker blocker(outputPathEdit_);
        outputPathEdit_->setText(path);
        lastAutoOutputPathSuggestion_.clear();
        outputPathFollowsSuggestion_ = false;
    }
}

void ExportSettingsDialog::onExportClicked() {
    const ExportOutputFormat format = selectedOutputFormat();

    if (format != ExportOutputFormat::Xml && queuedClips_.isEmpty()) {
        QMessageBox::warning(this,
            AppLocale::trUi("export.title"),
            AppLocale::trUi("export.no_clips_selected"));
        return;
    }

    if (format == ExportOutputFormat::Xml && (!tagSession_ || tagSession_->tags().isEmpty())) {
        QMessageBox::warning(this,
            AppLocale::trUi("export.title"),
            AppLocale::trUi("export.no_tags_for_xml"));
        return;
    }

    if (format != ExportOutputFormat::Xml && ClipExporter::findFfmpeg().isEmpty()) {
        QMessageBox::critical(this,
            AppLocale::trUi("export.title"),
            AppLocale::trUi("export.ffmpeg_not_found"));
        return;
    }

    QString outputPath = outputPathEdit_ ? outputPathEdit_->text().trimmed() : QString();
    if (outputPath.isEmpty()) {
        onBrowseOutputPath();
        outputPath = outputPathEdit_ ? outputPathEdit_->text().trimmed() : QString();
        if (outputPath.isEmpty()) return;
    }

    const QString canonicalChosen = QFileInfo(outputPath).absoluteFilePath();
    for (const QString& occupied : occupiedOutputPaths_) {
        if (QFileInfo(occupied).absoluteFilePath() == canonicalChosen) {
            QMessageBox::warning(this,
                AppLocale::trUi("export.title"),
                AppLocale::trUi("export.job_path_in_use"));
            return;
        }
    }

    result_.format = format;
    result_.sortByTeam = sortOrderCombo_
        && sortOrderCombo_->isVisible()
        && sortOrderCombo_->currentData().toString() == QStringLiteral("by_team");
    result_.overlayLanguage = exportLanguageCombo_
        ? static_cast<AppLocale::Language>(exportLanguageCombo_->currentData().toInt())
        : AppLocale::currentLanguage();
    result_.includeBottomOverlay =
        includeBottomOverlayCheckBox_ && includeBottomOverlayCheckBox_->isChecked();
    result_.includeScoreboardOverlay =
        includeScoreboardOverlayCheckBox_ && includeScoreboardOverlayCheckBox_->isChecked();
    result_.includeAudioTrack =
        includeAudioTrackCheckBox_ && includeAudioTrackCheckBox_->isChecked();
    result_.includeBrandingOverlay =
        includeBrandingOverlayCheckBox_ && includeBrandingOverlayCheckBox_->isChecked();
    result_.includeNotesOverlay =
        includeNotesCheckBox_ && includeNotesCheckBox_->isChecked();
    result_.outputPath = outputPath;

    accept();
}
