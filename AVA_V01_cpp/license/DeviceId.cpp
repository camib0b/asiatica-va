#include "DeviceId.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QSysInfo>
#include <QUuid>

namespace {

QString persistedFallbackDeviceId() {
  const QStringList locations = QStandardPaths::standardLocations(QStandardPaths::AppDataLocation);
  if (locations.isEmpty()) {
    return QString();
  }
  const QString directoryPath = locations.first();
  QDir().mkpath(directoryPath);
  const QString filePath = QDir(directoryPath).filePath(QStringLiteral("device-id.txt"));
  QFile file(filePath);
  if (file.open(QIODevice::ReadOnly)) {
    const QString existing = QString::fromUtf8(file.readAll()).trimmed();
    file.close();
    if (!existing.isEmpty()) return existing;
  }
  const QString generated =
      QStringLiteral("fallback-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
  if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    file.write(generated.toUtf8());
    file.write("\n");
    file.close();
  }
  return generated;
}

} // namespace

#ifndef Q_OS_MACOS

QString avaStableDeviceId() {
  QFile machineIdFile(QStringLiteral("/etc/machine-id"));
  if (machineIdFile.open(QIODevice::ReadOnly)) {
    const QByteArray machineId = machineIdFile.readAll().trimmed();
    if (!machineId.isEmpty()) {
      const QByteArray digest = QCryptographicHash::hash(machineId, QCryptographicHash::Sha256);
      return QStringLiteral("linux-") + QString::fromLatin1(digest.toHex().left(32));
    }
  }
  const QByteArray hostBytes = QSysInfo::machineUniqueId();
  if (!hostBytes.isEmpty()) {
    const QByteArray digest = QCryptographicHash::hash(hostBytes, QCryptographicHash::Sha256);
    return QStringLiteral("host-") + QString::fromLatin1(digest.toHex().left(32));
  }
  return persistedFallbackDeviceId();
}

#endif
