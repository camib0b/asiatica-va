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
constexpr char kConfigFileName[] = "youtube_oauth.json";

QString readClientIdFromJsonFile(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        return {};
    }

    return document.object().value(QStringLiteral("client_id")).toString().trimmed();
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

QString resolveClientId() {
    const QByteArray clientIdEnvironment = qgetenv("AVA_YOUTUBE_CLIENT_ID");
    if (!clientIdEnvironment.isEmpty()) {
        return QString::fromUtf8(clientIdEnvironment).trimmed();
    }

    QSettings settings;
    settings.beginGroup(QLatin1String(kSettingsGroup));
    const QString storedClientId = settings.value(QLatin1String(kClientIdKey)).toString().trimmed();
    settings.endGroup();
    if (!storedClientId.isEmpty()) {
        return storedClientId;
    }

    const QStringList configDirectories = QStandardPaths::standardLocations(
        QStandardPaths::AppConfigLocation);
    for (const QString& directoryPath : configDirectories) {
        const QString filePath = QDir(directoryPath).filePath(QLatin1String(kConfigFileName));
        const QString fileClientId = readClientIdFromJsonFile(filePath);
        if (!fileClientId.isEmpty()) {
            return fileClientId;
        }
    }

    return readClientIdFromJsonFile(bundledCredentialsPath());
}

} // namespace

namespace YouTubeConfig {

QString clientId() {
    return resolveClientId();
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
