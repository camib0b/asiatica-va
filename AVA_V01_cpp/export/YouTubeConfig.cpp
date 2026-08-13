#include "YouTubeConfig.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>

namespace {

constexpr char kSettingsGroup[] = "youtube/oauth";
constexpr char kClientIdKey[] = "clientId";
constexpr char kApiKeyKey[] = "apiKey";
constexpr char kConfigFileName[] = "youtube_oauth.json";

struct OAuthCredentials {
    QString clientId;
    QString apiKey;
};

OAuthCredentials readCredentialsFromJsonFile(const QString& filePath) {
    OAuthCredentials credentials;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return credentials;
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        return credentials;
    }

    const QJsonObject object = document.object();
    credentials.clientId = object.value(QStringLiteral("client_id")).toString().trimmed();
    credentials.apiKey = object.value(QStringLiteral("api_key")).toString().trimmed();
    return credentials;
}

QString bundledCredentialsPath() {
    const QString applicationDirectoryPath = QCoreApplication::applicationDirPath();
    const QString macOsPath =
        QDir(applicationDirectoryPath).filePath(QLatin1String(kConfigFileName));
    if (QFile::exists(macOsPath)) {
        return macOsPath;
    }

    const QDir applicationDirectory(applicationDirectoryPath);
    const QString resourcesPath =
        applicationDirectory.filePath(QStringLiteral("../Resources/%1").arg(QLatin1String(kConfigFileName)));
    if (QFile::exists(resourcesPath)) {
        return resourcesPath;
    }

    return macOsPath;
}

OAuthCredentials resolveCredentials() {
    const QByteArray clientIdEnvironment = qgetenv("AVA_YOUTUBE_CLIENT_ID");
    const QByteArray apiKeyEnvironment = qgetenv("AVA_YOUTUBE_API_KEY");
    if (!clientIdEnvironment.isEmpty()) {
        OAuthCredentials credentials;
        credentials.clientId = QString::fromUtf8(clientIdEnvironment).trimmed();
        credentials.apiKey = QString::fromUtf8(apiKeyEnvironment).trimmed();
        return credentials;
    }

    QSettings settings;
    settings.beginGroup(QLatin1String(kSettingsGroup));
    const QString storedClientId = settings.value(QLatin1String(kClientIdKey)).toString().trimmed();
    const QString storedApiKey = settings.value(QLatin1String(kApiKeyKey)).toString().trimmed();
    settings.endGroup();
    if (!storedClientId.isEmpty()) {
        OAuthCredentials credentials;
        credentials.clientId = storedClientId;
        credentials.apiKey = storedApiKey;
        return credentials;
    }

    const QStringList configDirectories = QStandardPaths::standardLocations(
        QStandardPaths::AppConfigLocation);
    for (const QString& directoryPath : configDirectories) {
        const QString filePath = QDir(directoryPath).filePath(QLatin1String(kConfigFileName));
        const OAuthCredentials fileCredentials = readCredentialsFromJsonFile(filePath);
        if (!fileCredentials.clientId.isEmpty()) {
            return fileCredentials;
        }
    }

    return readCredentialsFromJsonFile(bundledCredentialsPath());
}

} // namespace

namespace YouTubeConfig {

QString clientId() {
    return resolveCredentials().clientId;
}

QString apiKey() {
    return resolveCredentials().apiKey;
}

bool isConfigured() {
    return !clientId().isEmpty();
}

void bootstrap() {
    const QStringList configDirectories = QStandardPaths::standardLocations(
        QStandardPaths::AppConfigLocation);
    if (configDirectories.isEmpty()) {
        return;
    }

    const QString destinationDirectory = configDirectories.first();
    QDir().mkpath(destinationDirectory);
    const QString destinationPath = QDir(destinationDirectory).filePath(QLatin1String(kConfigFileName));
    if (QFile::exists(destinationPath)) {
        return;
    }

    const QString bundledPath = bundledCredentialsPath();
    if (!QFile::exists(bundledPath)) {
        return;
    }

    QFile::copy(bundledPath, destinationPath);
}

QString setupInstructions() {
    return QStringLiteral(
        "YouTube upload requires a Google OAuth client ID.\n\n"
        "1. Create a project in Google Cloud Console.\n"
        "2. Enable the YouTube Data API v3.\n"
        "3. Create an OAuth client ID (Desktop app type).\n"
        "4. Save credentials to config/youtube_oauth.json and rebuild,\n"
        "   or place youtube_oauth.json in the app config folder.\n\n"
        "See config/youtube_oauth.json.example.");
}

} // namespace YouTubeConfig
