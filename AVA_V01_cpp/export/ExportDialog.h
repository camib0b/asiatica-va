#pragma once

#include <QDialog>
#include <QString>
#include <QVector>
#include <QtGlobal>

#include "AppLocale.h"
#include "TagSession.h"
#include "YouTubeUploader.h"

class QAudioOutput;
class QCheckBox;
class QComboBox;
class QEvent;
class QLabel;
class QLineEdit;
class QMediaPlayer;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QVideoWidget;

class ClipExporter;
class ClipTrimBar;
class VideoControlsBar;
class YouTubeAuthManager;
class YouTubeUploader;

class ExportDialog final : public QDialog {
    Q_OBJECT

public:
    explicit ExportDialog(TagSession* session,
                          const QString& sourceVideoPath,
                          qint64 videoDurationMs,
                          const QString& defaultOutputDirectoryPath = QString(),
                          QWidget* parent = nullptr);
    ~ExportDialog() override;

    enum class OutputFormat {
        Mp4 = 0,
        Xml = 1,
        Both = 2,
    };

private slots:
    void onEventTypeChanged(int index);
    void onTeamFilterChanged(int index);
    void onOutputFormatChanged(int index);
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

    void onYouTubeConnectClicked();
    void onYouTubeDisconnectClicked();
    void onYouTubeAuthStateChanged();
    void onYouTubeAuthError(const QString& message);
    void onYouTubeUploadProgress(int percent);
    void onYouTubeUploadFinished(bool success, const QString& message, const QString& videoUrl);

    void onPreviewSlowerClicked();
    void onPreviewFasterClicked();
    void onPreviewResetSpeedClicked();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct ClipTrimData {
        TagSession::GameTag tag;
        int tagSessionIndex = -1;
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
    void updateFilterControlsForFormat();
    void setExporting(bool exporting);
    void updatePathFieldForFormat();
    OutputFormat selectedOutputFormat() const;

    void buildTrimDataFromSettings();
    void saveTrimForCurrentClip(bool userInitiatedTrim = false);
    void showClipAtIndex(int index);
    void updateClipNavigation();
    void ensurePreviewPlayer();
    void stopPreviewPlayer();
    void applyPreviewPlaybackRate();
    void updateTrimPageKeyboardShortcutsForCurrentPage();
    void attachTrimPageKeyboardShortcuts();
    void detachTrimPageKeyboardShortcuts();
    void regenerateOverlayTexts();
    QString teamDisplayName(const QString& teamKey) const;
    QString sanitizedExportFileNamePart(const QString& raw) const;
    QString suggestedXmlReportBaseName() const;
    QString suggestedExportBaseName() const;
    QString defaultExportSuggestedFilePath() const;
    void applySuggestedOutputPathFromForm();
    void refreshOutputPathIfFollowingForm();
    void updateYouTubeSection();
    void startYouTubeUpload(const QString& exportedVideoPath);
    YouTubeUploadMetadata buildYouTubeUploadMetadata() const;

    TagSession* tagSession_;
    QString sourceVideoPath_;
    QString defaultOutputDirectoryPath_;
    qint64 videoDurationMs_;

    // Pages
    QStackedWidget* pagesStack_ = nullptr;
    QWidget* settingsPage_ = nullptr;
    QWidget* trimPage_ = nullptr;

    // Settings page widgets
    QComboBox* eventTypeCombo_ = nullptr;
    QComboBox* teamFilterCombo_ = nullptr;
    QComboBox* outputFormatCombo_ = nullptr;
    QLabel* sortOrderLabel_ = nullptr;
    QComboBox* sortOrderCombo_ = nullptr;
    QComboBox* exportLanguageCombo_ = nullptr;
    QCheckBox* includeBottomOverlayCheckBox_ = nullptr;
    QCheckBox* includeScoreboardOverlayCheckBox_ = nullptr;
    QCheckBox* includeAudioTrackCheckBox_ = nullptr;
    QCheckBox* includeAvaOverlayCheckBox_ = nullptr;
    QLabel* clipCountLabel_ = nullptr;
    QLineEdit* outputPathEdit_ = nullptr;
    QPushButton* browseButton_ = nullptr;
    QPushButton* reviewButton_ = nullptr;
    QPushButton* settingsCloseButton_ = nullptr;

    // YouTube upload
    QCheckBox* uploadToYouTubeCheckBox_ = nullptr;
    QLabel* youtubeStatusLabel_ = nullptr;
    QLabel* youtubePlaylistLabel_ = nullptr;
    QPushButton* youtubeConnectButton_ = nullptr;
    QPushButton* youtubeDisconnectButton_ = nullptr;
    YouTubeAuthManager* youtubeAuth_ = nullptr;
    YouTubeUploader* youtubeUploader_ = nullptr;
    bool youtubeUploadPending_ = false;

    // Trim page widgets
    QLabel* clipNavigationLabel_ = nullptr;
    QPushButton* prevClipButton_ = nullptr;
    QPushButton* nextClipButton_ = nullptr;
    QPushButton* discardClipButton_ = nullptr;
    VideoControlsBar* previewControlsBar_ = nullptr;
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

    double previewPlaybackRate_ = 1.0;
    bool trimKeyboardShortcutsInstalled_ = false;
    bool exportInProgress_ = false;

    QString lastAutoOutputPathSuggestion_;
};
