#include "YouTubeAuthManager.h"
#include "YouTubeConfig.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>

#include <functional>

namespace {

constexpr char kSettingsGroup[] = "youtube/auth";
constexpr char kRefreshTokenKey[] = "refreshToken";
constexpr char kChannelTitleKey[] = "channelTitle";
constexpr char kChannelIdKey[] = "channelId";

constexpr char kAuthEndpoint[] = "https://accounts.google.com/o/oauth2/v2/auth";
constexpr char kTokenEndpoint[] = "https://oauth2.googleapis.com/token";
constexpr char kChannelEndpoint[] = "https://www.googleapis.com/youtube/v3/channels?part=snippet&mine=true";

constexpr quint16 kPreferredOAuthRedirectPort = 8080;

QString base64UrlEncode(const QByteArray& input) {
    return QString::fromLatin1(input.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

QString generateCodeVerifier() {
    QByteArray randomBytes(32, Qt::Uninitialized);
    for (int index = 0; index < randomBytes.size(); ++index) {
        randomBytes[index] = static_cast<char>(QRandomGenerator::global()->bounded(256));
    }
    return base64UrlEncode(randomBytes);
}

QString codeChallengeForVerifier(const QString& codeVerifier) {
    const QByteArray digest = QCryptographicHash::hash(codeVerifier.toUtf8(), QCryptographicHash::Sha256);
    return base64UrlEncode(digest);
}

QString oauthScopes() {
    return QStringLiteral(
        "https://www.googleapis.com/auth/youtube.upload "
        "https://www.googleapis.com/auth/youtube");
}

QString loopbackRedirectUri(quint16 port) {
    // Google loopback examples use no trailing slash: http://127.0.0.1:PORT
    return QStringLiteral("http://127.0.0.1:%1").arg(port);
}

bool listenOnLoopback(QTcpServer* server, quint16& boundPort) {
    const QHostAddress loopbackAddress(QStringLiteral("127.0.0.1"));
    if (server->listen(loopbackAddress, kPreferredOAuthRedirectPort)) {
        boundPort = kPreferredOAuthRedirectPort;
        return true;
    }
    if (server->listen(loopbackAddress, 0)) {
        boundPort = server->serverPort();
        return true;
    }
    return false;
}

QString googleOAuthTroubleshootingHint() {
    return QStringLiteral(
        "\n\nCheck in Google Cloud Console:\n"
        "1. OAuth client type is Desktop app (not Web application).\n"
        "2. If using Web application, add this Authorized redirect URI:\n"
        "   http://127.0.0.1:8080\n"
        "3. OAuth consent screen includes YouTube scopes.\n"
        "4. Your Google account is listed as a Test user (while app is in Testing).");
}

QString formatGoogleOAuthError(const QString& errorCode, const QString& errorDescription) {
    QString message = errorDescription.trimmed();
    if (message.isEmpty()) {
        message = errorCode.trimmed();
    }
    if (message.isEmpty()) {
        message = QStringLiteral("Google sign-in failed.");
    }

    if (errorCode == QStringLiteral("redirect_uri_mismatch")) {
        message = QStringLiteral(
            "Google rejected the redirect URI (redirect_uri_mismatch).\n\n"
            "AVA uses http://127.0.0.1:8080 for sign-in.\n"
            "Your OAuth client must be type Desktop app, or if Web application,\n"
            "add http://127.0.0.1:8080 to Authorized redirect URIs.");
    } else if (errorCode == QStringLiteral("access_denied")) {
        message = QStringLiteral(
            "Google sign-in was denied. If the app is in Testing mode, add your Google "
            "account under OAuth consent screen → Test users.");
    } else if (errorCode == QStringLiteral("org_internal")) {
        message = QStringLiteral(
            "This Google Cloud project is configured as Internal (organization-only).\n\n"
            "Personal Gmail accounts cannot sign in. In Google Cloud Console go to:\n"
            "APIs & Services → OAuth consent screen\n\n"
            "• Change User type to External, or\n"
            "• Sign in with a Google Workspace account from the same organization.\n\n"
            "If you switch to External while in Testing, add your Gmail address under "
            "Test users.");
    } else if (errorCode == QStringLiteral("invalid_scope")) {
        message = QStringLiteral(
            "YouTube scopes are not enabled on the OAuth consent screen. Add "
            "youtube.upload and youtube scopes in Google Cloud Console.");
    }

    return message + googleOAuthTroubleshootingHint();
}

QString parseHttpRequestQuery(const QString& requestLine, QUrlQuery* queryOut) {
    const int methodEnd = requestLine.indexOf(' ');
    if (methodEnd < 0) {
        return QString();
    }
    const int pathEnd = requestLine.indexOf(' ', methodEnd + 1);
    const QString pathAndQuery = pathEnd > methodEnd
        ? requestLine.mid(methodEnd + 1, pathEnd - methodEnd - 1)
        : requestLine.mid(methodEnd + 1);
    const int queryStart = pathAndQuery.indexOf('?');
    if (queryStart < 0) {
        return QString();
    }

    QUrlQuery query(pathAndQuery.mid(queryStart + 1));
    if (queryOut) {
        *queryOut = query;
    }
    return query.queryItemValue(QStringLiteral("code"));
}

QString parseTokenEndpointError(const QByteArray& responseBody, const QString& networkError) {
    const QJsonObject object = QJsonDocument::fromJson(responseBody).object();
    const QString errorCode = object.value(QStringLiteral("error")).toString();
    const QString errorDescription = object.value(QStringLiteral("error_description")).toString();
    if (!errorCode.isEmpty() || !errorDescription.isEmpty()) {
        return formatGoogleOAuthError(errorCode, errorDescription);
    }
    if (!networkError.isEmpty()) {
        return QStringLiteral("YouTube token exchange failed: %1").arg(networkError)
            + googleOAuthTroubleshootingHint();
    }
    return QStringLiteral("YouTube token exchange failed.") + googleOAuthTroubleshootingHint();
}

} // namespace

YouTubeAuthManager::YouTubeAuthManager(QObject* parent)
    : QObject(parent) {
    loadTokens();
}

bool YouTubeAuthManager::isAuthenticated() const {
    return !refreshToken_.isEmpty();
}

QString YouTubeAuthManager::channelTitle() const {
    return channelTitle_;
}

QString YouTubeAuthManager::channelId() const {
    return channelId_;
}

void YouTubeAuthManager::requestAccessToken(
    const std::function<void(const QString& token, const QString& error)>& callback) {
    if (!isAuthenticated()) {
        callback(QString(), QStringLiteral("Not signed in to YouTube."));
        return;
    }

    const qint64 nowEpochSec = QDateTime::currentSecsSinceEpoch();
    if (!accessToken_.isEmpty() && accessTokenExpiryEpochSec_ > nowEpochSec + 60) {
        callback(accessToken_, QString());
        return;
    }

    refreshAccessToken(callback);
}

void YouTubeAuthManager::startSignIn() {
    if (!YouTubeConfig::isConfigured()) {
        emit authError(YouTubeConfig::setupInstructions());
        return;
    }

    auto* redirectServer = new QTcpServer(this);
    quint16 redirectPort = 0;
    if (!listenOnLoopback(redirectServer, redirectPort)) {
        emit authError(QStringLiteral("Could not start local OAuth redirect server on 127.0.0.1."));
        redirectServer->deleteLater();
        return;
    }

    const QString redirectUri = loopbackRedirectUri(redirectPort);
    const QString codeVerifier = generateCodeVerifier();
    const QString codeChallenge = codeChallengeForVerifier(codeVerifier);

    QUrl authorizationUrl(QString::fromLatin1(kAuthEndpoint));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("client_id"), YouTubeConfig::clientId());
    query.addQueryItem(QStringLiteral("redirect_uri"), redirectUri);
    query.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
    query.addQueryItem(QStringLiteral("scope"), oauthScopes());
    query.addQueryItem(QStringLiteral("access_type"), QStringLiteral("offline"));
    query.addQueryItem(QStringLiteral("prompt"), QStringLiteral("consent"));
    query.addQueryItem(QStringLiteral("code_challenge"), codeChallenge);
    query.addQueryItem(QStringLiteral("code_challenge_method"), QStringLiteral("S256"));
    authorizationUrl.setQuery(query);

    connect(redirectServer, &QTcpServer::newConnection, this,
            [this, redirectServer, codeVerifier, redirectUri]() {
        QTcpSocket* socket = redirectServer->nextPendingConnection();
        if (!socket) {
            return;
        }

        connect(socket, &QTcpSocket::readyRead, this,
                [this, socket, redirectServer, codeVerifier, redirectUri]() {
            const QByteArray requestData = socket->readAll();
            const QString requestText = QString::fromUtf8(requestData);
            const int firstLineEnd = requestText.indexOf('\n');
            const QString requestLine = firstLineEnd >= 0 ? requestText.left(firstLineEnd).trimmed() : requestText;

            QUrlQuery callbackQuery;
            const QString authorizationCode = parseHttpRequestQuery(requestLine, &callbackQuery);
            const QString errorCode = callbackQuery.queryItemValue(QStringLiteral("error"));
            const QString errorDescription =
                callbackQuery.queryItemValue(QStringLiteral("error_description"));

            const QByteArray responseBody =
                "<html><body><h2>YouTube authorization complete.</h2>"
                "<p>You can close this window and return to AVA.</p></body></html>";
            const QByteArray response =
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/html\r\n"
                "Connection: close\r\n"
                "Content-Length: " + QByteArray::number(responseBody.size()) + "\r\n\r\n" + responseBody;
            socket->write(response);
            socket->flush();
            socket->disconnectFromHost();
            redirectServer->close();
            redirectServer->deleteLater();

            if (!errorCode.isEmpty()) {
                emit authError(formatGoogleOAuthError(errorCode, errorDescription));
                return;
            }

            if (authorizationCode.isEmpty()) {
                emit authError(QStringLiteral("YouTube authorization was cancelled or failed.")
                               + googleOAuthTroubleshootingHint());
                return;
            }

            exchangeAuthorizationCode(authorizationCode, codeVerifier, redirectUri);
        });
    });

    if (!QDesktopServices::openUrl(authorizationUrl)) {
        redirectServer->close();
        redirectServer->deleteLater();
        emit authError(QStringLiteral("Could not open the system browser for YouTube sign-in."));
    }
}

void YouTubeAuthManager::signOut() {
    clearStoredTokens();
    accessToken_.clear();
    refreshToken_.clear();
    accessTokenExpiryEpochSec_ = 0;
    channelTitle_.clear();
    channelId_.clear();
    emit authStateChanged();
}

void YouTubeAuthManager::exchangeAuthorizationCode(const QString& authorizationCode,
                                                   const QString& codeVerifier,
                                                   const QString& redirectUri) {
    auto* networkManager = new QNetworkAccessManager(this);
    QNetworkRequest request(QUrl(QString::fromLatin1(kTokenEndpoint)));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/x-www-form-urlencoded"));

    QUrlQuery body;
    body.addQueryItem(QStringLiteral("client_id"), YouTubeConfig::clientId());
    body.addQueryItem(QStringLiteral("code"), authorizationCode);
    body.addQueryItem(QStringLiteral("code_verifier"), codeVerifier);
    body.addQueryItem(QStringLiteral("redirect_uri"), redirectUri);
    body.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("authorization_code"));

    QNetworkReply* reply = networkManager->post(request, body.toString(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply, networkManager]() {
        const QByteArray responseBody = reply->readAll();
        if (reply->error() != QNetworkReply::NoError || responseBody.contains("\"error\"")) {
            emit authError(parseTokenEndpointError(responseBody, reply->errorString()));
            reply->deleteLater();
            networkManager->deleteLater();
            return;
        }

        const QJsonObject tokenObject = QJsonDocument::fromJson(responseBody).object();
        accessToken_ = tokenObject.value(QStringLiteral("access_token")).toString();
        const QString newRefreshToken = tokenObject.value(QStringLiteral("refresh_token")).toString();
        if (!newRefreshToken.isEmpty()) {
            refreshToken_ = newRefreshToken;
        }
        const int expiresInSeconds = tokenObject.value(QStringLiteral("expires_in")).toInt(3600);
        accessTokenExpiryEpochSec_ = QDateTime::currentSecsSinceEpoch() + expiresInSeconds;

        persistTokens();
        fetchChannelInfo();

        reply->deleteLater();
        networkManager->deleteLater();
    });
}

void YouTubeAuthManager::refreshAccessToken(
    const std::function<void(const QString& token, const QString& error)>& callback) {
    if (refreshToken_.isEmpty()) {
        callback(QString(), QStringLiteral("YouTube refresh token is missing."));
        return;
    }

    auto* networkManager = new QNetworkAccessManager(this);
    QNetworkRequest request(QUrl(QString::fromLatin1(kTokenEndpoint)));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/x-www-form-urlencoded"));

    QUrlQuery body;
    body.addQueryItem(QStringLiteral("client_id"), YouTubeConfig::clientId());
    body.addQueryItem(QStringLiteral("refresh_token"), refreshToken_);
    body.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("refresh_token"));

    QNetworkReply* reply = networkManager->post(request, body.toString(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply, networkManager, callback]() {
        const QByteArray responseBody = reply->readAll();
        if (reply->error() != QNetworkReply::NoError || responseBody.contains("\"error\"")) {
            callback(QString(), parseTokenEndpointError(responseBody, reply->errorString()));
            reply->deleteLater();
            networkManager->deleteLater();
            return;
        }

        const QJsonObject tokenObject = QJsonDocument::fromJson(responseBody).object();
        accessToken_ = tokenObject.value(QStringLiteral("access_token")).toString();
        const int expiresInSeconds = tokenObject.value(QStringLiteral("expires_in")).toInt(3600);
        accessTokenExpiryEpochSec_ = QDateTime::currentSecsSinceEpoch() + expiresInSeconds;
        persistTokens();

        callback(accessToken_, QString());
        reply->deleteLater();
        networkManager->deleteLater();
    });
}

void YouTubeAuthManager::fetchChannelInfo() {
    if (accessToken_.isEmpty()) {
        emit authStateChanged();
        return;
    }

    auto* networkManager = new QNetworkAccessManager(this);
    QNetworkRequest request(QUrl(QString::fromLatin1(kChannelEndpoint)));
    request.setRawHeader("Authorization", ("Bearer " + accessToken_.toUtf8()));

    QNetworkReply* reply = networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, networkManager]() {
        const QByteArray responseBody = reply->readAll();
        if (reply->error() == QNetworkReply::NoError) {
            const QJsonObject rootObject = QJsonDocument::fromJson(responseBody).object();
            const QJsonArray items = rootObject.value(QStringLiteral("items")).toArray();
            if (!items.isEmpty()) {
                const QJsonObject snippet = items.first().toObject().value(QStringLiteral("snippet")).toObject();
                channelTitle_ = snippet.value(QStringLiteral("title")).toString();
                channelId_ = items.first().toObject().value(QStringLiteral("id")).toString();
                persistTokens();
            }
        }

        emit authStateChanged();
        reply->deleteLater();
        networkManager->deleteLater();
    });
}

void YouTubeAuthManager::persistTokens() {
    QSettings settings;
    settings.beginGroup(QLatin1String(kSettingsGroup));
    settings.setValue(QLatin1String(kRefreshTokenKey), refreshToken_);
    settings.setValue(QLatin1String(kChannelTitleKey), channelTitle_);
    settings.setValue(QLatin1String(kChannelIdKey), channelId_);
    settings.endGroup();
}

void YouTubeAuthManager::loadTokens() {
    QSettings settings;
    settings.beginGroup(QLatin1String(kSettingsGroup));
    refreshToken_ = settings.value(QLatin1String(kRefreshTokenKey)).toString();
    channelTitle_ = settings.value(QLatin1String(kChannelTitleKey)).toString();
    channelId_ = settings.value(QLatin1String(kChannelIdKey)).toString();
    settings.endGroup();
}

void YouTubeAuthManager::clearStoredTokens() {
    QSettings settings;
    settings.beginGroup(QLatin1String(kSettingsGroup));
    settings.remove(QString());
    settings.endGroup();
}
