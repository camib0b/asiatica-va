#pragma once

#include "AppLocale.h"
#include "ExportJobManager.h"
#include "PresentationQueue.h"
#include "YouTubeUploader.h"

#include <QDialog>
#include <QString>
#include <QStringList>
#include <QVector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

class TagSession;
class YouTubeAuthManager;

class ExportSettingsDialog final : public QDialog {
    Q_OBJECT

public:
    struct Result {
        ExportOutputFormat format = ExportOutputFormat::Mp4;
        bool sortByTeam = false;
        AppLocale::Language overlayLanguage = AppLocale::Language::English;
        bool includeBottomOverlay = true;
        bool includeScoreboardOverlay = true;
        bool includeAudioTrack = true;
        bool includeAvaOverlay = true;
        bool includeNotesOverlay = true;
        QString outputPath;
        bool uploadToYouTube = false;
        YouTubeUploadMetadata youtubeMetadata;
    };

    explicit ExportSettingsDialog(TagSession* session,
                                  const QString& sourceVideoPath,
                                  const QString& defaultOutputDirectoryPath,
                                  const QVector<PresentationQueue::Clip>& queuedClips,
                                  YouTubeAuthManager* youtubeAuth,
                                  const QStringList& occupiedOutputPaths,
                                  QWidget* parent = nullptr);

    Result resultSettings() const { return result_; }

private slots:
    void onOutputFormatChanged(int index);
    void onBrowseOutputPath();
    void onExportClicked();
    void onYouTubeConnectClicked();
    void onYouTubeDisconnectClicked();
    void onYouTubeAuthStateChanged();
    void onYouTubeAuthError(const QString& message);

private:
    void buildUi();
    void updatePathFieldForFormat();
    void updateSortOrderVisibility();
    void updateControlsForFormat();
    void updateClipCount();
    void updateYouTubeSection();
    ExportOutputFormat selectedOutputFormat() const;
    QString suggestedBaseName() const;
    QString defaultSuggestedFilePath() const;
    void applySuggestedOutputPathFromForm();
    void refreshOutputPathIfFollowingForm();
    YouTubeUploadMetadata buildYouTubeUploadMetadata() const;
    bool queuedClipsHaveMixedTeams() const;

    TagSession* tagSession_ = nullptr;
    YouTubeAuthManager* youtubeAuth_ = nullptr;
    QString sourceVideoPath_;
    QString defaultOutputDirectoryPath_;
    QVector<PresentationQueue::Clip> queuedClips_;
    QStringList occupiedOutputPaths_;
    Result result_;
    QString lastAutoOutputPathSuggestion_;

    QComboBox* outputFormatCombo_ = nullptr;
    QLabel* sortOrderLabel_ = nullptr;
    QComboBox* sortOrderCombo_ = nullptr;
    QComboBox* exportLanguageCombo_ = nullptr;
    QCheckBox* includeBottomOverlayCheckBox_ = nullptr;
    QCheckBox* includeScoreboardOverlayCheckBox_ = nullptr;
    QCheckBox* includeAudioTrackCheckBox_ = nullptr;
    QCheckBox* includeAvaOverlayCheckBox_ = nullptr;
    QCheckBox* includeNotesCheckBox_ = nullptr;
    QLabel* clipCountLabel_ = nullptr;
    QLineEdit* outputPathEdit_ = nullptr;
    QPushButton* browseButton_ = nullptr;
    QCheckBox* uploadToYouTubeCheckBox_ = nullptr;
    QLabel* youtubeStatusLabel_ = nullptr;
    QLabel* youtubePlaylistLabel_ = nullptr;
    QPushButton* youtubeConnectButton_ = nullptr;
    QPushButton* youtubeDisconnectButton_ = nullptr;
    QPushButton* closeButton_ = nullptr;
    QPushButton* exportButton_ = nullptr;
};
