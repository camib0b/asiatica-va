#include "XaiConfig.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QStandardPaths>
#include <QByteArray>
#include <QString>

namespace {

constexpr char kConfigFileName[] = "xai.json";
constexpr char kApiKeyJsonKey[] = "api_key";

// REVIEW periodically: model availability and vision payload limits change over time.
constexpr char kDefaultChatModelName[] = "grok-4.20-0309-non-reasoning";
constexpr int kDefaultMaxMetadataThumbnailBytes = 15 * 1024 * 1024;

QString trimmedEnvironmentValue(const char* variableName) {
    const QByteArray value = qgetenv(variableName);
    if (value.isEmpty()) {
        return {};
    }
    return QString::fromUtf8(value).trimmed();
}

QString readApiKeyFromJsonFile(const QString& filePath) {
    if (!QFile::exists(filePath)) {
        return {};
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning("XaiConfig: cannot read %s: %s",
                 qPrintable(filePath),
                 qPrintable(file.errorString()));
        return {};
    }

    const QByteArray rawData = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        qWarning("XaiConfig: read error for %s: %s",
                 qPrintable(filePath),
                 qPrintable(file.errorString()));
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(rawData, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qWarning("XaiConfig: invalid JSON in %s: %s",
                 qPrintable(filePath),
                 qPrintable(parseError.errorString()));
        return {};
    }
    if (!document.isObject()) {
        qWarning("XaiConfig: expected JSON object in %s", qPrintable(filePath));
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

QString resolveApiKey() {
    const QString environmentKey = apiKeyFromEnvironment();
    if (!environmentKey.isEmpty()) {
        return environmentKey;
    }
    return apiKeyFromConfigFiles();
}

// Persists the API key as owner-readable JSON. Platform keychain storage would be
// stronger, but matches the existing local desktop config pattern (see LicenseConfig).
bool writeApiKeyToConfigFile(const QString& filePath,
                             const QString& apiKey,
                             bool createOnly) {
    if (filePath.isEmpty() || apiKey.isEmpty()) {
        return false;
    }

    const QFileInfo fileInfo(filePath);
    if (!QDir().mkpath(fileInfo.absolutePath())) {
        qWarning("XaiConfig: cannot create config directory %s",
                 qPrintable(fileInfo.absolutePath()));
        return false;
    }

    QJsonObject object;
    object.insert(QLatin1String(kApiKeyJsonKey), apiKey);
    const QByteArray jsonData = QJsonDocument(object).toJson(QJsonDocument::Indented);

    QSaveFile file(filePath);
    QIODevice::OpenMode openMode = QIODevice::WriteOnly;
    if (createOnly) {
        openMode |= QIODevice::NewOnly;
    } else {
        openMode |= QIODevice::Truncate;
    }
    if (!file.open(openMode)) {
        if (createOnly && QFile::exists(filePath)) {
            return true;
        }
        qWarning("XaiConfig: cannot write %s: %s",
                 qPrintable(filePath),
                 qPrintable(file.errorString()));
        return false;
    }
    if (!file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        qWarning("XaiConfig: cannot set owner-only permissions on %s",
                 qPrintable(filePath));
        file.cancelWriting();
        return false;
    }
    if (file.write(jsonData) != jsonData.size()
        || !file.flush()
        || file.error() != QFileDevice::NoError) {
        qWarning("XaiConfig: failed to write %s: %s",
                 qPrintable(filePath),
                 qPrintable(file.errorString()));
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        qWarning("XaiConfig: failed to commit %s: %s",
                 qPrintable(filePath),
                 qPrintable(file.errorString()));
        return false;
    }
    return true;
}

} // namespace

namespace XaiConfig {

QString apiKey() {
    static const QString cached = resolveApiKey();
    return cached;
}

bool isConfigured() {
    return !apiKey().isEmpty();
}

QString chatModelName() {
    const QString avaOverride = trimmedEnvironmentValue("AVA_XAI_CHAT_MODEL");
    if (!avaOverride.isEmpty()) {
        return avaOverride;
    }
    const QString xaiOverride = trimmedEnvironmentValue("XAI_CHAT_MODEL");
    if (!xaiOverride.isEmpty()) {
        return xaiOverride;
    }
    return QLatin1String(kDefaultChatModelName);
}

int maxMetadataThumbnailBytes() {
    const QString avaOverride = trimmedEnvironmentValue("AVA_XAI_MAX_THUMBNAIL_BYTES");
    if (!avaOverride.isEmpty()) {
        bool parsedSuccessfully = false;
        const int parsedBytes = avaOverride.toInt(&parsedSuccessfully);
        if (parsedSuccessfully && parsedBytes > 0) {
            return parsedBytes;
        }
    }
    return kDefaultMaxMetadataThumbnailBytes;
}

void bootstrap() {
    const QString environmentKey = apiKeyFromEnvironment();
    if (environmentKey.isEmpty()) {
        return;
    }

    const QString destinationPath = primaryConfigFilePath();
    if (destinationPath.isEmpty()) {
        qWarning("XaiConfig: no application config directory is available");
        return;
    }

    if (!writeApiKeyToConfigFile(destinationPath, environmentKey, true)) {
        qWarning("XaiConfig: failed to persist environment API key to %s",
                 qPrintable(destinationPath));
    }
}

} // namespace XaiConfig
