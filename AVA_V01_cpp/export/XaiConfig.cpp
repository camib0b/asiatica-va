#include "XaiConfig.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QByteArray>
#include <QString>

namespace {

constexpr char kConfigFileName[] = "xai.json";
constexpr char kApiKeyJsonKey[] = "api_key";

QString readApiKeyFromJsonFile(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        return {};
    }

    return document.object().value(QLatin1String(kApiKeyJsonKey)).toString().trimmed();
}

QString apiKeyFromEnvironment() {
    const QByteArray xaiEnvironment = qgetenv("XAI_API_KEY");
    if (!xaiEnvironment.isEmpty()) {
        return QString::fromUtf8(xaiEnvironment).trimmed();
    }

    const QByteArray avaEnvironment = qgetenv("AVA_XAI_API_KEY");
    if (!avaEnvironment.isEmpty()) {
        return QString::fromUtf8(avaEnvironment).trimmed();
    }

    return {};
}

QString primaryConfigDirectory() {
    const QStringList configDirectories =
        QStandardPaths::standardLocations(QStandardPaths::AppConfigLocation);
    if (configDirectories.isEmpty()) {
        return {};
    }
    return configDirectories.first();
}

QString primaryConfigFilePath() {
    const QString directoryPath = primaryConfigDirectory();
    if (directoryPath.isEmpty()) {
        return {};
    }
    return QDir(directoryPath).filePath(QLatin1String(kConfigFileName));
}

bool writeApiKeyToConfigFile(const QString& filePath, const QString& apiKey) {
    if (filePath.isEmpty() || apiKey.isEmpty()) {
        return false;
    }

    const QFileInfo fileInfo(filePath);
    if (!QDir().mkpath(fileInfo.absolutePath())) {
        return false;
    }

    QJsonObject object;
    object.insert(QLatin1String(kApiKeyJsonKey), apiKey);
    const QJsonDocument document(object);

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    if (!file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        file.close();
        QFile::remove(filePath);
        return false;
    }
    const QByteArray jsonData = document.toJson(QJsonDocument::Indented);
    if (file.write(jsonData) != jsonData.size()) {
        file.close();
        QFile::remove(filePath);
        return false;
    }
    return true;
}

QString apiKeyFromConfigFiles() {
    const QStringList configDirectories =
        QStandardPaths::standardLocations(QStandardPaths::AppConfigLocation);
    for (const QString& directoryPath : configDirectories) {
        const QString filePath = QDir(directoryPath).filePath(QLatin1String(kConfigFileName));
        const QString fileKey = readApiKeyFromJsonFile(filePath);
        if (!fileKey.isEmpty()) {
            return fileKey;
        }
    }
    return {};
}

} // namespace

namespace XaiConfig {

QString apiKey() {
    const QString environmentKey = apiKeyFromEnvironment();
    if (!environmentKey.isEmpty()) {
        return environmentKey;
    }
    return apiKeyFromConfigFiles();
}

bool isConfigured() {
    return !apiKey().isEmpty();
}

void bootstrap() {
    const QString environmentKey = apiKeyFromEnvironment();
    if (environmentKey.isEmpty()) {
        return;
    }

    const QString destinationPath = primaryConfigFilePath();
    if (destinationPath.isEmpty() || QFile::exists(destinationPath)) {
        return;
    }

    writeApiKeyToConfigFile(destinationPath, environmentKey);
}

} // namespace XaiConfig
