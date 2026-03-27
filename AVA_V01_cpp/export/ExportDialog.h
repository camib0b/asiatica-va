#pragma once

#include <QDialog>
#include <QString>
#include <QVector>
#include <QtGlobal>

#include "AppLocale.h"
#include "TagSession.h"

class QAudioOutput;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QMediaPlayer;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QVideoWidget;

class ClipExporter;
class ClipTrimBar;

class ExportDialog final : public QDialog {
    Q_OBJECT

public:
    explicit ExportDialog(TagSession* session,
                          const QString& sourceVideoPath,
                          qint64 videoDurationMs,
                          QWidget* parent = nullptr);
    ~ExportDialog() override;

private slots:
    void onEventTypeChanged(int index);
    void onTeamFilterChanged(int index);
    void onBrowseOutputPath();
    void onReviewClipsClicked();
    void onBackToSettingsClicked();
    void onPrevClipClicked();
    void onNextClipClicked();
    void onTogglePreviewPlayPause();
    void onTrimSeekRequested(qint64 posMs);
    void onPreviewPositionChanged(qint64 posMs);
    void onDiscardClipClicked();
    void onExportClicked();
    void onCancelExportClicked();
    void onExportProgress(int currentClip, int totalClips);
    void onExportFinished(bool success, const QString& message);

private:
    struct ClipTrimData {
        TagSession::GameTag tag;
        qint64 startMs;
        qint64 endMs;
        QString overlayText;
        bool includeSecondaryOverlay = false;
        QString secondaryOverlayText;
    };

    void buildSettingsPage();
    void buildTrimPage();
    void populateEventTypes();
    void updateClipCount();
    void updateSortOrderVisibility();
    void setExporting(bool exporting);

    void buildTrimDataFromSettings();
    void saveTrimForCurrentClip();
    void showClipAtIndex(int index);
    void updateClipNavigation();
    void ensurePreviewPlayer();
    void stopPreviewPlayer();
    void regenerateOverlayTexts();
    QString teamDisplayName(const QString& teamKey) const;

    TagSession* tagSession_;
    QString sourceVideoPath_;
    qint64 videoDurationMs_;

    // Pages
    QStackedWidget* pagesStack_ = nullptr;
    QWidget* settingsPage_ = nullptr;
    QWidget* trimPage_ = nullptr;

    // Settings page widgets
    QComboBox* eventTypeCombo_ = nullptr;
    QComboBox* teamFilterCombo_ = nullptr;
    QLabel* sortOrderLabel_ = nullptr;
    QComboBox* sortOrderCombo_ = nullptr;
    QComboBox* exportLanguageCombo_ = nullptr;
    QCheckBox* includeBottomOverlayCheckBox_ = nullptr;
    QLabel* clipCountLabel_ = nullptr;
    QDoubleSpinBox* beforePaddingSpin_ = nullptr;
    QDoubleSpinBox* afterPaddingSpin_ = nullptr;
    QLineEdit* outputPathEdit_ = nullptr;
    QPushButton* browseButton_ = nullptr;
    QPushButton* reviewButton_ = nullptr;
    QPushButton* settingsCloseButton_ = nullptr;

    // Trim page widgets
    QLabel* clipNavigationLabel_ = nullptr;
    QPushButton* prevClipButton_ = nullptr;
    QPushButton* nextClipButton_ = nullptr;
    QPushButton* playPauseButton_ = nullptr;
    QPushButton* discardClipButton_ = nullptr;
    QVideoWidget* previewVideoWidget_ = nullptr;
    QMediaPlayer* previewPlayer_ = nullptr;
    QAudioOutput* previewAudioOutput_ = nullptr;
    ClipTrimBar* clipTrimBar_ = nullptr;
    QCheckBox* includeNoteCheckBox_ = nullptr;
    QLineEdit* noteLineEdit_ = nullptr;
    QProgressBar* progressBar_ = nullptr;
    QLabel* progressLabel_ = nullptr;
    QPushButton* backButton_ = nullptr;
    QPushButton* exportButton_ = nullptr;
    QPushButton* cancelExportButton_ = nullptr;

    // Trim data
    QVector<ClipTrimData> trimData_;
    int currentTrimIndex_ = 0;
    AppLocale::Language exportLanguage_ = AppLocale::Language::English;
    QString translatedEvent_;

    ClipExporter* exporter_ = nullptr;
};
