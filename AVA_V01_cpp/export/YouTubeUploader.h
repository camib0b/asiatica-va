#pragma once

#include <QObject>
#include <QString>

#include <functional>

class QNetworkReply;
class TagSession;
class YouTubeAuthManager;

struct YouTubeUploadMetadata {
    QString title;
    QString description;
    QString privacyStatus = QStringLiteral("unlisted");
};

class YouTubeUploader final : public QObject {
    Q_OBJECT

public:
    explicit YouTubeUploader(YouTubeAuthManager* authManager, QObject* parent = nullptr);

    /// Builds a stable key for the current match (competition, teams, date).
    static QString matchFingerprint(const TagSession* session);

    /// Human-readable playlist title derived from match metadata.
    static QString matchPlaylistTitle(const TagSession* session);

    /// Finds or creates the YouTube playlist for the given match session.
    void resolvePlaylistForMatch(const TagSession* session,
                                 const std::function<void(const QString& playlistId,
                                                          const QString& error)>& callback);

    void uploadVideo(const QString& filePath,
                     const YouTubeUploadMetadata& metadata,
                     const QString& playlistId);

    void cancelUpload();

signals:
    void progressChanged(int percent);
    void uploadFinished(bool success, const QString& message, const QString& videoUrl);

private:
    void startResumableUpload(const QString& accessToken,
                              const QString& filePath,
                              const YouTubeUploadMetadata& metadata,
                              const QString& playlistId);
    void uploadFileContents(const QString& uploadUrl,
                            const QString& accessToken,
                            const QString& filePath,
                            const QString& playlistId);
    void addVideoToPlaylist(const QString& accessToken,
                            const QString& playlistId,
                            const QString& videoId);
    void cachePlaylistId(const QString& fingerprint, const QString& playlistId);
    QString cachedPlaylistId(const QString& fingerprint) const;

    YouTubeAuthManager* authManager_ = nullptr;
    QNetworkReply* activeReply_ = nullptr;
    bool cancelled_ = false;
};
