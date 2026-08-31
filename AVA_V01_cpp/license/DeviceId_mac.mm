#import <Foundation/Foundation.h>
#import <IOKit/IOKitLib.h>
#import <IOKit/IOKitKeys.h>

#include "DeviceId.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QSysInfo>
#include <QUuid>

static QString avaPersistedFallbackDeviceId() {
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

QString avaStableDeviceId() {
#if defined(kIOMainPortDefault)
  const mach_port_t mainPort = kIOMainPortDefault;
#else
  const mach_port_t mainPort = kIOMasterPortDefault;
#endif

  io_service_t service =
      IOServiceGetMatchingService(mainPort, IOServiceMatching("IOPlatformExpertDevice"));
  if (service) {
    CFTypeRef uuidRef = IORegistryEntryCreateCFProperty(
        service, CFSTR(kIOPlatformUUIDKey), kCFAllocatorDefault, 0);
    IOObjectRelease(service);
    if (uuidRef) {
      NSString* uuidString = (__bridge_transfer NSString*)uuidRef;
      if (uuidString != nil && uuidString.length > 0) {
        return QStringLiteral("mac-") + QString::fromNSString(uuidString);
      }
    }
  }

  const QByteArray hostBytes = QSysInfo::machineUniqueId();
  if (!hostBytes.isEmpty()) {
    const QByteArray digest = QCryptographicHash::hash(hostBytes, QCryptographicHash::Sha256);
    return QStringLiteral("mac-host-") + QString::fromLatin1(digest.toHex().left(32));
  }
  return avaPersistedFallbackDeviceId();
}
