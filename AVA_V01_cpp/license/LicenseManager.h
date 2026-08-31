#pragma once

#include "LicenseTypes.h"

#include <QJsonObject>
#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

class LicenseManager final : public QObject {
  Q_OBJECT

public:
  static LicenseManager& instance();

  /// Load local signed state and start a 14-day trial if this Mac has none.
  /// Network calls are asynchronous; entitlement is decided from local state immediately.
  void bootstrap();

  bool isEntitled() const;
  LicenseUiStatus uiStatus() const;

  /// Paste a signed key from Camila. Email must match the address inside the key.
  /// Works offline when the key HMAC is valid. Server bind is best-effort.
  void activateLicense(const QString& email, const QString& licenseKey);

  void loadLicenseFile(const QString& filePath);

  /// Best-effort online refresh. Network errors do not lock while grace remains.
  void refreshFromServer();

  bool isBusy() const { return busy_; }
  QString lastUserMessage() const { return lastUserMessage_; }

signals:
  void entitlementChanged();
  void userMessageChanged();
  void busyChanged();

private:
  explicit LicenseManager(QObject* parent = nullptr);

  void loadOrCreateLocalState();
  void saveStore();
  void evaluateAndNotify();
  LicenseUiStatus evaluate() const;
  qint64 effectiveNow() const;
  void bumpWallClock(qint64 nowUnix);
  void startLocalTrial();
  void postJson(const QString& path, const QByteArray& body);
  void handleServerPayload(const QString& path, const QByteArray& raw);
  void applyServerLicense(const QJsonObject& licenseObject, const QString& token,
                          bool markBlockedOnReject);
  void setBusy(bool busy);
  void setUserMessage(const QString& message);

  LicenseStoreRecord store_;
  LicenseUiStatus status_;
  QNetworkAccessManager* network_ = nullptr;
  bool busy_ = false;
  QString lastUserMessage_;
  bool bootstrapped_ = false;
};
