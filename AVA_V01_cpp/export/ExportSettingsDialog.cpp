#include "ExportSettingsDialog.h"

#include "ExportClipBuilder.h"
#include "ExportOutputPath.h"
#include "FfmpegLocator.h"
#include "TagSession.h"
#include "AppLocale.h"
#include "StyleProps.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
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

constexpr auto kMp4Extension = ".mp4";

enum class ExistingOutputChoice {
    Rename,
    Overwrite,
    Cancel,
};

QString displayFileNameWithWrapOpportunities(const QString& path) {
    const QString fileName = QFileInfo(path).fileName();
    QString wrapped;
    wrapped.reserve(fileName.size() + fileName.size() / 12);
    int charactersSinceBreak = 0;
    int index = 0;
    while (index < fileName.size()) {
        int nextIndex = index + 1;
        if (fileName.at(index).isHighSurrogate()
            && nextIndex < fileName.size()
            && fileName.at(nextIndex).isLowSurrogate()) {
            ++nextIndex;
        }
        while (nextIndex < fileName.size() && fileName.at(nextIndex).isMark()) {
            ++nextIndex;
        }
        wrapped.append(fileName.mid(index, nextIndex - index));
        index = nextIndex;
        ++charactersSinceBreak;
        const QChar last = wrapped.isEmpty() ? QChar() : wrapped.at(wrapped.size() - 1);
        if (last.isSpace()) {
            charactersSinceBreak = 0;
            continue;
        }
        if (charactersSinceBreak >= 18) {
            wrapped.append(QChar(0x200B));
            charactersSinceBreak = 0;
        }
    }
    return wrapped;
}

ExistingOutputChoice askExistingOutputChoice(QWidget* parent, const QString& outputPath) {
    QDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("ExportExistsDialog"));
    dialog.setWindowTitle(AppLocale::trUi("export.output_exists_title"));
    dialog.setModal(true);

    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(12);

    auto* messageLabel = new QLabel(AppLocale::trUi("export.output_exists"), &dialog);
    messageLabel->setWordWrap(true);
    messageLabel->setMaximumWidth(440);
    layout->addWidget(messageLabel);

    auto* pathLabel = new QLabel(displayFileNameWithWrapOpportunities(outputPath), &dialog);
    pathLabel->setObjectName(QStringLiteral("ExportExistsPath"));
    pathLabel->setWordWrap(true);
    pathLabel->setTextFormat(Qt::PlainText);
    pathLabel->setMaximumWidth(440);
    pathLabel->setToolTip(QFileInfo(outputPath).fileName());
    Style::setRole(pathLabel, "muted");
    layout->addWidget(pathLabel);

    auto* buttonRow = new QHBoxLayout();
    buttonRow->setSpacing(8);
    buttonRow->addStretch(1);

    auto* cancelButton = new QPushButton(AppLocale::trUi("export.output_cancel"), &dialog);
    auto* overwriteButton = new QPushButton(AppLocale::trUi("export.output_overwrite"), &dialog);
    auto* renameButton = new QPushButton(AppLocale::trUi("export.output_rename"), &dialog);
    cancelButton->setObjectName(QStringLiteral("ExportExistsCancel"));
    overwriteButton->setObjectName(QStringLiteral("ExportExistsOverwrite"));
    renameButton->setObjectName(QStringLiteral("ExportExistsRename"));
    cancelButton->setCursor(Qt::PointingHandCursor);
    overwriteButton->setCursor(Qt::PointingHandCursor);
    renameButton->setCursor(Qt::PointingHandCursor);
    cancelButton->setAutoDefault(false);
    overwriteButton->setAutoDefault(false);
    renameButton->setDefault(true);
    renameButton->setAutoDefault(true);
    Style::setVariant(cancelButton, "outline");
    Style::setVariant(overwriteButton, "secondary");
    Style::setVariant(renameButton, "primary");

    buttonRow->addWidget(cancelButton);
    buttonRow->addWidget(overwriteButton);
    buttonRow->addWidget(renameButton);
    layout->addLayout(buttonRow);

    ExistingOutputChoice choice = ExistingOutputChoice::Cancel;
    QObject::connect(renameButton, &QPushButton::clicked, &dialog, [&dialog, &choice]() {
        choice = ExistingOutputChoice::Rename;
        dialog.accept();
    });
    QObject::connect(overwriteButton, &QPushButton::clicked, &dialog, [&dialog, &choice]() {
        choice = ExistingOutputChoice::Overwrite;
        dialog.accept();
    });
    QObject::connect(cancelButton, &QPushButton::clicked, &dialog, [&dialog, &choice]() {
        choice = ExistingOutputChoice::Cancel;
        dialog.reject();
    });

    cancelButton->ensurePolished();
    overwriteButton->ensurePolished();
    renameButton->ensurePolished();
    const int buttonWidth = cancelButton->sizeHint().width()
        + overwriteButton->sizeHint().width()
        + renameButton->sizeHint().width()
        + 64;
    dialog.setMinimumWidth(qMax(440, buttonWidth));
    dialog.exec();
    return choice;
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
    updateForm();
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

bool ExportSettingsDialog::queuedClipsHaveMixedTeams() const {
    if (queuedClips_.size() < 2) return false;
    const QString firstTeam = queuedClips_.first().team;
    for (const auto& clip : queuedClips_) {
        if (clip.team != firstTeam) return true;
    }
    return false;
}

void ExportSettingsDialog::updateForm() {
    if (outputPathEdit_) {
        outputPathEdit_->setPlaceholderText(AppLocale::trUi("export.output_placeholder"));
    }

    const bool showSortOrder = queuedClipsHaveMixedTeams();
    if (sortOrderLabel_) sortOrderLabel_->setVisible(showSortOrder);
    if (sortOrderCombo_) sortOrderCombo_->setVisible(showSortOrder);

    if (clipCountLabel_) {
        clipCountLabel_->setText(
            QStringLiteral("%1 %2")
                .arg(queuedClips_.size())
                .arg(AppLocale::trUi("export.clips_label")));
        if (exportButton_) exportButton_->setEnabled(!queuedClips_.isEmpty());
    }

    refreshOutputPathIfFollowingForm();
}

QString ExportSettingsDialog::suggestedBaseName() const {
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
    return QDir(directoryPath).filePath(baseName + QLatin1String(kMp4Extension));
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
        .filePath(suggestedBase + QLatin1String(kMp4Extension));
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
    const QString filter = QStringLiteral("MP4 (*.mp4);;All files (*.*)");

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
    if (queuedClips_.isEmpty()) {
        QMessageBox::warning(this,
            AppLocale::trUi("export.title"),
            AppLocale::trUi("export.no_clips_selected"));
        return;
    }

    if (FfmpegLocator::findFfmpeg().isEmpty()) {
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

    if (ExportOutputPath::pathsReferToSameFile(outputPath, sourceVideoPath_)) {
        QMessageBox sameFileBox(this);
        sameFileBox.setIcon(QMessageBox::Warning);
        sameFileBox.setWindowTitle(AppLocale::trUi("export.title"));
        sameFileBox.setText(AppLocale::trUi("export.output_same_as_source"));
        const QString sameFileMessage = sameFileBox.text();
        const auto sameFileLabels = sameFileBox.findChildren<QLabel*>();
        for (QLabel* label : sameFileLabels) {
            if (label->text() != sameFileMessage) continue;
            label->setWordWrap(true);
            label->setMaximumWidth(440);
        }
        sameFileBox.exec();
        return;
    }

    const auto outputPathIsTaken = [this](const QString& candidate) {
        if (ExportOutputPath::pathsReferToSameFile(candidate, sourceVideoPath_)) return true;
        if (QFileInfo::exists(candidate)) return true;
        for (const QString& occupied : occupiedOutputPaths_) {
            if (ExportOutputPath::pathsReferToSameFile(candidate, occupied)) return true;
        }
        return false;
    };

    if (QFileInfo::exists(outputPath)) {
        const ExistingOutputChoice choice = askExistingOutputChoice(this, outputPath);
        if (choice == ExistingOutputChoice::Cancel) return;
        if (choice == ExistingOutputChoice::Rename) {
            const QString renamedPath = ExportOutputPath::nonCollidingFilePath(outputPath, outputPathIsTaken);
            if (renamedPath.isEmpty()) {
                QMessageBox::warning(this,
                    AppLocale::trUi("export.output_exists_title"),
                    AppLocale::trUi("export.output_rename_failed"));
                return;
            }
            if (outputPathEdit_) {
                QSignalBlocker blocker(outputPathEdit_);
                outputPathEdit_->setText(renamedPath);
            }
            lastAutoOutputPathSuggestion_.clear();
            outputPathFollowsSuggestion_ = false;
            return;
        }
    }

    result_.format = ExportOutputFormat::Mp4;
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
