#include "LicenseConfig.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>

namespace {

constexpr char kSettingsGroup[] = "license";
constexpr char kApiUrlKey[] = "apiUrl";
constexpr char kConfigFileName[] = "license_server.json";

QString readApiUrlFromJsonFile(const QString& filePath) {
  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly)) return QString();
  const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
  if (!document.isObject()) return QString();
  return document.object().value(QStringLiteral("api_url")).toString().trimmed();
}

QString bundledConfigPath() {
  const QString applicationDirectoryPath = QCoreApplication::applicationDirPath();
  const QString nextToBinary =
      QDir(applicationDirectoryPath).filePath(QLatin1String(kConfigFileName));
  if (QFile::exists(nextToBinary)) return nextToBinary;
  const QString resourcesPath =
      QDir(applicationDirectoryPath)
          .filePath(QStringLiteral("../Resources/%1").arg(QLatin1String(kConfigFileName)));
  if (QFile::exists(resourcesPath)) return resourcesPath;
  return nextToBinary;
}

QString stripTrailingSlash(QString url) {
  while (url.endsWith(QLatin1Char('/'))) {
    url.chop(1);
  }
  return url;
}

} // namespace

namespace LicenseConfig {

QString apiUrl() {
  const QByteArray fromEnvironment = qgetenv("AVA_LICENSE_API_URL");
  if (!fromEnvironment.isEmpty()) {
    return stripTrailingSlash(QString::fromUtf8(fromEnvironment).trimmed());
  }

  QSettings settings;
  settings.beginGroup(QLatin1String(kSettingsGroup));
  const QString stored = settings.value(QLatin1String(kApiUrlKey)).toString().trimmed();
  settings.endGroup();
  if (!stored.isEmpty()) return stripTrailingSlash(stored);

  const QStringList configDirectories =
      QStandardPaths::standardLocations(QStandardPaths::AppConfigLocation);
  for (const QString& directoryPath : configDirectories) {
    const QString fromFile =
        readApiUrlFromJsonFile(QDir(directoryPath).filePath(QLatin1String(kConfigFileName)));
    if (!fromFile.isEmpty()) return stripTrailingSlash(fromFile);
  }

#ifdef AVA_LICENSE_API_URL
  const QString compiled = QString::fromUtf8(AVA_LICENSE_API_URL).trimmed();
  if (!compiled.isEmpty()) return stripTrailingSlash(compiled);
#endif

  return stripTrailingSlash(readApiUrlFromJsonFile(bundledConfigPath()));
}

bool isApiConfigured() {
  const QString url = apiUrl();
  return url.startsWith(QLatin1String("https://")) || url.startsWith(QLatin1String("http://"));
}

void bootstrap() {
  const QStringList configDirectories =
      QStandardPaths::standardLocations(QStandardPaths::AppConfigLocation);
  if (configDirectories.isEmpty()) return;
  const QString destinationDirectory = configDirectories.first();
  QDir().mkpath(destinationDirectory);
  const QString destinationPath =
      QDir(destinationDirectory).filePath(QLatin1String(kConfigFileName));
  if (QFile::exists(destinationPath)) return;
  const QString bundledPath = bundledConfigPath();
  if (!QFile::exists(bundledPath)) return;
  QFile::copy(bundledPath, destinationPath);
}

} // namespace LicenseConfig
