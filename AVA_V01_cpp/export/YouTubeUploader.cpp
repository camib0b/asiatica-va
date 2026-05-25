#include "YouTubeUploader.h"
#include "YouTubeAuthManager.h"
#include "TagSession.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QUrl>
#include <QUrlQuery>

#include <functional>

namespace {

constexpr char kPlaylistCacheGroup[] = "youtube/playlists";

QString sanitizePlaylistSegment(const QString& raw) {
    QString segment = raw.trimmed();
    const QString forbidden = QStringLiteral("\\/:*?\"<>|\r\n\t");
    for (QChar character : forbidden) {
        segment.replace(character, QLatin1Char('_'));
    }
    while (segment.contains(QStringLiteral("  "))) {
        segment.replace(QStringLiteral("  "), QStringLiteral(" "));
    }
    return segment;
}

QString teamNameOrDefault(const TagSession* session, const QString& teamKey, const QString& fallback) {
    if (!session) {
        return fallback;
    }
    if (teamKey == QStringLiteral("Home")) {
        return session->homeTeamName().isEmpty() ? fallback : session->homeTeamName();
    }
    if (teamKey == QStringLiteral("Away")) {
        return session->awayTeamName().isEmpty() ? fallback : session->awayTeamName();
    }
    return fallback;
}

} // namespace

YouTubeUploader::YouTubeUploader(YouTubeAuthManager* authManager, QObject* parent)
    : QObject(parent)
    , authManager_(authManager) {
}

QString YouTubeUploader::matchFingerprint(const TagSession* session) {
    if (!session) {
        return QString();
    }

    const QString competition = session->competitionName().trimmed();
    const QString home = teamNameOrDefault(session, QStringLiteral("Home"), QStringLiteral("Home"));
    const QString away = teamNameOrDefault(session, QStringLiteral("Away"), QStringLiteral("Away"));
    const QString dateString = session->gameDate().isValid()
        ? session->gameDate().toString(QStringLiteral("yyyy-MM-dd"))
        : QStringLiteral("unknown-date");

    return QStringLiteral("%1|%2|%3|%4")
        .arg(competition, home, away, dateString);
}

QString YouTubeUploader::matchPlaylistTitle(const TagSession* session) {
    if (!session) {
        return QStringLiteral("AVA Match Clips");
    }

    const QString home = sanitizePlaylistSegment(
        teamNameOrDefault(session, QStringLiteral("Home"), QStringLiteral("Home")));
    const QString away = sanitizePlaylistSegment(
        teamNameOrDefault(session, QStringLiteral("Away"), QStringLiteral("Away")));
    const QString competition = sanitizePlaylistSegment(session->competitionName());
    const QString dateString = session->gameDate().isValid()
        ? session->gameDate().toString(QStringLiteral("yyyy-MM-dd"))
        : QString();

    QString title = QStringLiteral("%1 vs %2").arg(home, away);
    if (!competition.isEmpty()) {
        title += QStringLiteral(" - %1").arg(competition);
    }
    if (!dateString.isEmpty()) {
        title += QStringLiteral(" (%1)").arg(dateString);
    }
    return title;
}

void YouTubeUploader::resolvePlaylistForMatch(
    const TagSession* session,
    const std::function<void(const QString& playlistId, const QString& error)>& callback) {
    const QString fingerprint = matchFingerprint(session);
    if (fingerprint.isEmpty()) {
        callback(QString(), QStringLiteral("Match metadata is missing."));
        return;
    }

    const QString cachedId = cachedPlaylistId(fingerprint);
    if (!cachedId.isEmpty()) {
        callback(cachedId, QString());
        return;
    }

    if (!authManager_) {
        callback(QString(), QStringLiteral("YouTube authentication is unavailable."));
        return;
    }

    authManager_->requestAccessToken([this, session, fingerprint, callback](const QString& accessToken,
                                                                              const QString& error) {
        if (accessToken.isEmpty()) {
            callback(QString(), error);
            return;
        }

        const QString targetTitle = matchPlaylistTitle(session);
        auto* networkManager = new QNetworkAccessManager(this);

        const auto searchNextPage = std::make_shared<std::function<void(const QString&)>>();
        *searchNextPage = [this, networkManager, accessToken, targetTitle, fingerprint, callback, searchNextPage](
                              const QString& pageToken) {
            if (cancelled_) {
                networkManager->deleteLater();
                return;
            }

            QUrl url(QStringLiteral("https://www.googleapis.com/youtube/v3/playlists"));
            QUrlQuery query;
            query.addQueryItem(QStringLiteral("part"), QStringLiteral("snippet"));
            query.addQueryItem(QStringLiteral("mine"), QStringLiteral("true"));
            query.addQueryItem(QStringLiteral("maxResults"), QStringLiteral("50"));
            if (!pageToken.isEmpty()) {
                query.addQueryItem(QStringLiteral("pageToken"), pageToken);
            }
            url.setQuery(query);

            QNetworkRequest request(url);
            request.setRawHeader("Authorization", ("Bearer " + accessToken.toUtf8()));

            QNetworkReply* reply = networkManager->get(request);
            connect(reply, &QNetworkReply::finished, this, [this, reply, networkManager, accessToken, targetTitle,
                                                           fingerprint, callback, searchNextPage]() {
                const QByteArray responseBody = reply->readAll();
                if (reply->error() != QNetworkReply::NoError) {
                    callback(QString(), QStringLiteral("Failed to list YouTube playlists: %1").arg(reply->errorString()));
                    reply->deleteLater();
                    networkManager->deleteLater();
                    return;
                }

                const QJsonObject rootObject = QJsonDocument::fromJson(responseBody).object();
                const QJsonArray items = rootObject.value(QStringLiteral("items")).toArray();
                for (const QJsonValue& itemValue : items) {
                    const QJsonObject itemObject = itemValue.toObject();
                    const QJsonObject snippet = itemObject.value(QStringLiteral("snippet")).toObject();
                    if (snippet.value(QStringLiteral("title")).toString() == targetTitle) {
                        const QString playlistId = itemObject.value(QStringLiteral("id")).toString();
                        cachePlaylistId(fingerprint, playlistId);
                        callback(playlistId, QString());
                        reply->deleteLater();
                        networkManager->deleteLater();
                        return;
                    }
                }

                const QString nextPageToken = rootObject.value(QStringLiteral("nextPageToken")).toString();
                if (!nextPageToken.isEmpty()) {
                    (*searchNextPage)(nextPageToken);
                    reply->deleteLater();
                    return;
                }

                QUrl createUrl(QStringLiteral("https://www.googleapis.com/youtube/v3/playlists"));
                QUrlQuery createQuery;
                createQuery.addQueryItem(QStringLiteral("part"), QStringLiteral("snippet,status"));
                createUrl.setQuery(createQuery);

                QJsonObject createBody;
                QJsonObject snippet;
                snippet.insert(QStringLiteral("title"), targetTitle);
                snippet.insert(QStringLiteral("description"),
                               QStringLiteral("Match clips exported from AVA."));
                createBody.insert(QStringLiteral("snippet"), snippet);
                QJsonObject status;
                status.insert(QStringLiteral("privacyStatus"), QStringLiteral("unlisted"));
                createBody.insert(QStringLiteral("status"), status);

                QNetworkRequest createRequest(createUrl);
                createRequest.setRawHeader("Authorization", ("Bearer " + accessToken.toUtf8()));
                createRequest.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

                QNetworkReply* createReply =
                    networkManager->post(createRequest, QJsonDocument(createBody).toJson(QJsonDocument::Compact));
                connect(createReply, &QNetworkReply::finished, this, [this, createReply, networkManager, fingerprint,
                                                                       callback]() {
                    const QByteArray createResponseBody = createReply->readAll();
                    if (createReply->error() != QNetworkReply::NoError) {
                        callback(QString(),
                                 QStringLiteral("Failed to create YouTube playlist: %1")
                                     .arg(createReply->errorString()));
                        createReply->deleteLater();
                        networkManager->deleteLater();
                        return;
                    }

                    const QJsonObject createObject = QJsonDocument::fromJson(createResponseBody).object();
                    const QString playlistId = createObject.value(QStringLiteral("id")).toString();
                    cachePlaylistId(fingerprint, playlistId);
                    callback(playlistId, QString());
                    createReply->deleteLater();
                    networkManager->deleteLater();
                });

                reply->deleteLater();
            });
        };

        (*searchNextPage)(QString());
    });
}

void YouTubeUploader::uploadVideo(const QString& filePath,
                                    const YouTubeUploadMetadata& metadata,
                                    const QString& playlistId) {
    cancelled_ = false;

    if (!authManager_) {
        emit uploadFinished(false, QStringLiteral("YouTube authentication is unavailable."), QString());
        return;
    }

    if (playlistId.isEmpty()) {
        emit uploadFinished(false, QStringLiteral("YouTube playlist is missing."), QString());
        return;
    }

    const QFileInfo fileInfo(filePath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        emit uploadFinished(false, QStringLiteral("Exported video file was not found."), QString());
        return;
    }

    authManager_->requestAccessToken([this, filePath, metadata, playlistId](const QString& accessToken,
                                                                              const QString& error) {
        if (accessToken.isEmpty()) {
            emit uploadFinished(false, error, QString());
            return;
        }
        startResumableUpload(accessToken, filePath, metadata, playlistId);
    });
}

void YouTubeUploader::cancelUpload() {
    cancelled_ = true;
    if (activeReply_) {
        activeReply_->abort();
    }
}

void YouTubeUploader::startResumableUpload(const QString& accessToken,
                                           const QString& filePath,
                                           const YouTubeUploadMetadata& metadata,
                                           const QString& playlistId) {
    QUrl url(QStringLiteral("https://www.googleapis.com/upload/youtube/v3/videos"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("uploadType"), QStringLiteral("resumable"));
    query.addQueryItem(QStringLiteral("part"), QStringLiteral("snippet,status"));
    url.setQuery(query);

    QJsonObject body;
    QJsonObject snippet;
    snippet.insert(QStringLiteral("title"), metadata.title);
    snippet.insert(QStringLiteral("description"), metadata.description);
    body.insert(QStringLiteral("snippet"), snippet);
    QJsonObject status;
    status.insert(QStringLiteral("privacyStatus"), metadata.privacyStatus);
    body.insert(QStringLiteral("status"), status);

    auto* networkManager = new QNetworkAccessManager(this);
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", ("Bearer " + accessToken.toUtf8()));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    const QByteArray requestBody = QJsonDocument(body).toJson(QJsonDocument::Compact);
    QNetworkReply* reply = networkManager->post(request, requestBody);
    activeReply_ = reply;

    connect(reply, &QNetworkReply::finished, this, [this, reply, networkManager, accessToken, filePath, playlistId]() {
        activeReply_ = nullptr;
        if (cancelled_) {
            emit uploadFinished(false, QStringLiteral("YouTube upload cancelled."), QString());
            reply->deleteLater();
            networkManager->deleteLater();
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            emit uploadFinished(false,
                                QStringLiteral("Failed to start YouTube upload: %1").arg(reply->errorString()),
                                QString());
            reply->deleteLater();
            networkManager->deleteLater();
            return;
        }

        const QUrl uploadUrl = reply->header(QNetworkRequest::LocationHeader).toUrl();
        if (!uploadUrl.isValid()) {
            emit uploadFinished(false, QStringLiteral("YouTube upload URL was not returned."), QString());
            reply->deleteLater();
            networkManager->deleteLater();
            return;
        }

        uploadFileContents(uploadUrl.toString(), accessToken, filePath, playlistId);
        reply->deleteLater();
        networkManager->deleteLater();
    });
}

void YouTubeUploader::uploadFileContents(const QString& uploadUrl,
                                         const QString& accessToken,
                                         const QString& filePath,
                                         const QString& playlistId) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        emit uploadFinished(false, QStringLiteral("Could not read exported video for upload."), QString());
        return;
    }

    const QByteArray fileData = file.readAll();
    file.close();

    auto* networkManager = new QNetworkAccessManager(this);
    QNetworkRequest request((QUrl(uploadUrl)));
    request.setRawHeader("Authorization", ("Bearer " + accessToken.toUtf8()));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("video/*"));
    request.setHeader(QNetworkRequest::ContentLengthHeader, fileData.size());

    QNetworkReply* reply = networkManager->put(request, fileData);
    activeReply_ = reply;

    connect(reply, &QNetworkReply::uploadProgress, this, [this](qint64 bytesSent, qint64 bytesTotal) {
        if (bytesTotal <= 0) {
            return;
        }
        const int percent = static_cast<int>((bytesSent * 100) / bytesTotal);
        emit progressChanged(percent);
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply, networkManager, accessToken, playlistId]() {
        activeReply_ = nullptr;
        if (cancelled_) {
            emit uploadFinished(false, QStringLiteral("YouTube upload cancelled."), QString());
            reply->deleteLater();
            networkManager->deleteLater();
            return;
        }

        const QByteArray responseBody = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            emit uploadFinished(false,
                                QStringLiteral("YouTube upload failed: %1").arg(reply->errorString()),
                                QString());
            reply->deleteLater();
            networkManager->deleteLater();
            return;
        }

        const QJsonObject videoObject = QJsonDocument::fromJson(responseBody).object();
        const QString videoId = videoObject.value(QStringLiteral("id")).toString();
        if (videoId.isEmpty()) {
            emit uploadFinished(false, QStringLiteral("YouTube did not return a video ID."), QString());
            reply->deleteLater();
            networkManager->deleteLater();
            return;
        }

        addVideoToPlaylist(accessToken, playlistId, videoId);
        reply->deleteLater();
        networkManager->deleteLater();
    });
}

void YouTubeUploader::addVideoToPlaylist(const QString& accessToken,
                                         const QString& playlistId,
                                         const QString& videoId) {
    QUrl url(QStringLiteral("https://www.googleapis.com/youtube/v3/playlistItems"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("part"), QStringLiteral("snippet"));
    url.setQuery(query);

    QJsonObject body;
    QJsonObject snippet;
    snippet.insert(QStringLiteral("playlistId"), playlistId);
    QJsonObject resourceId;
    resourceId.insert(QStringLiteral("kind"), QStringLiteral("youtube#video"));
    resourceId.insert(QStringLiteral("videoId"), videoId);
    snippet.insert(QStringLiteral("resourceId"), resourceId);
    body.insert(QStringLiteral("snippet"), snippet);

    auto* networkManager = new QNetworkAccessManager(this);
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", ("Bearer " + accessToken.toUtf8()));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QNetworkReply* reply = networkManager->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    activeReply_ = reply;

    connect(reply, &QNetworkReply::finished, this, [this, reply, networkManager, videoId]() {
        activeReply_ = nullptr;
        const QString videoUrl = QStringLiteral("https://www.youtube.com/watch?v=%1").arg(videoId);

        if (reply->error() != QNetworkReply::NoError) {
            emit uploadFinished(false,
                                QStringLiteral("Video uploaded but playlist assignment failed: %1")
                                    .arg(reply->errorString()),
                                videoUrl);
            reply->deleteLater();
            networkManager->deleteLater();
            return;
        }

        emit uploadFinished(true, QStringLiteral("Video uploaded to YouTube."), videoUrl);
        reply->deleteLater();
        networkManager->deleteLater();
    });
}

void YouTubeUploader::cachePlaylistId(const QString& fingerprint, const QString& playlistId) {
    QSettings settings;
    settings.beginGroup(QLatin1String(kPlaylistCacheGroup));
    settings.setValue(fingerprint, playlistId);
    settings.endGroup();
}

QString YouTubeUploader::cachedPlaylistId(const QString& fingerprint) const {
    QSettings settings;
    settings.beginGroup(QLatin1String(kPlaylistCacheGroup));
    const QString playlistId = settings.value(fingerprint).toString();
    settings.endGroup();
    return playlistId;
}
