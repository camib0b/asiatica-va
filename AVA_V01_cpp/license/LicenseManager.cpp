#include "LicenseManager.h"

#include "DeviceId.h"
#include "LicenseConfig.h"
#include "LicenseConstants.h"
#include "LicenseCrypto.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>
#include <QtGlobal>

namespace {

constexpr char kSettingsGroup[] = "license";
constexpr char kStateKey[] = "signedState";
constexpr char kStateFileName[] = "license.state";

qint64 unixNow() {
  return QDateTime::currentSecsSinceEpoch();
}

QString stateFilePath() {
  const QStringList locations = QStandardPaths::standardLocations(QStandardPaths::AppDataLocation);
  if (locations.isEmpty()) return QString();
  QDir().mkpath(locations.first());
  return QDir(locations.first()).filePath(QLatin1String(kStateFileName));
}

QString normalizeEmail(QString email) {
  return email.trimmed().toLower();
}

QString compactKey(QString key) {
  return key.trimmed().simplified().remove(QLatin1Char(' ')).remove(QLatin1Char('\n'));
}

int wholeDaysRemaining(qint64 expiresAt, qint64 nowUnix) {
  if (expiresAt <= nowUnix) return 0;
  const qint64 remaining = expiresAt - nowUnix;
  return static_cast<int>((remaining + LicenseConstants::kSecondsPerDay - 1) /
                          LicenseConstants::kSecondsPerDay);
}

} // namespace

LicenseManager& LicenseManager::instance() {
  static LicenseManager* manager = nullptr;
  if (!manager) {
    manager = new LicenseManager(qApp);
  }
  return *manager;
}

LicenseManager::LicenseManager(QObject* parent) : QObject(parent) {
  network_ = new QNetworkAccessManager(this);
}

void LicenseManager::bootstrap() {
  if (bootstrapped_) return;
  bootstrapped_ = true;
  LicenseConfig::bootstrap();
  if (!LicenseCrypto::selfTest()) {
    qWarning("AVA license crypto self-test failed; keys may not verify.");
  }
  loadOrCreateLocalState();
  evaluateAndNotify();
  if (LicenseConfig::isApiConfigured()) {
    LicenseClaims claims;
    if (LicenseCrypto::verifyToken(store_.token, &claims) &&
        claims.kind == QLatin1String(LicenseConstants::kKindTrial) &&
        store_.lastSuccessfulCheckAt == 0) {
      postJson(QStringLiteral("/v1/trial/start"),
               QJsonDocument(QJsonObject{
                   {QStringLiteral("deviceId"), avaStableDeviceId()},
                   {QStringLiteral("token"), store_.token},
               })
                   .toJson(QJsonDocument::Compact));
    } else if (status_.entitled) {
      refreshFromServer();
    }
  }
}

bool LicenseManager::isEntitled() const {
  return status_.entitled;
}

LicenseUiStatus LicenseManager::uiStatus() const {
  return status_;
}

void LicenseManager::activateLicense(const QString& email, const QString& licenseKey) {
  const QString trimmedEmail = normalizeEmail(email);
  const QString token = compactKey(licenseKey);
  if (trimmedEmail.isEmpty() || token.isEmpty()) {
    setUserMessage(QStringLiteral("license.error.email_and_key"));
    return;
  }

  LicenseClaims claims;
  if (LicenseCrypto::verifyToken(token, &claims)) {
    if (normalizeEmail(claims.email) != trimmedEmail) {
      setUserMessage(QStringLiteral("license.error.email_mismatch"));
      return;
    }
    if (claims.kind != QLatin1String(LicenseConstants::kKindPaid)) {
      setUserMessage(QStringLiteral("license.error.invalid_key"));
      return;
    }
    if (!claims.deviceId.isEmpty() && claims.deviceId != avaStableDeviceId()) {
      setUserMessage(QStringLiteral("license.error.other_device"));
      return;
    }
    const qint64 now = effectiveNow();
    if (now >= claims.expiresAt) {
      setUserMessage(QStringLiteral("license.error.expired_key"));
      return;
    }
    store_.token = token;
    store_.deviceId = avaStableDeviceId();
    store_.serverStatus = QString::fromLatin1(LicenseConstants::kServerStatusOk);
    bumpWallClock(now);
    saveStore();
    evaluateAndNotify();
    setUserMessage(QStringLiteral("license.ok.activated"));
    if (LicenseConfig::isApiConfigured()) {
      postJson(QStringLiteral("/v1/license/activate"),
               QJsonDocument(QJsonObject{{QStringLiteral("email"), trimmedEmail},
                                         {QStringLiteral("token"), token},
                                         {QStringLiteral("deviceId"), avaStableDeviceId()}})
                   .toJson(QJsonDocument::Compact));
    }
    return;
  }

  if (!LicenseConfig::isApiConfigured()) {
    setUserMessage(QStringLiteral("license.error.invalid_key"));
    return;
  }

  postJson(QStringLiteral("/v1/license/activate"),
           QJsonDocument(QJsonObject{{QStringLiteral("email"), trimmedEmail},
                                     {QStringLiteral("token"), token},
                                     {QStringLiteral("deviceId"), avaStableDeviceId()}})
               .toJson(QJsonDocument::Compact));
}

void LicenseManager::loadLicenseFile(const QString& filePath) {
  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly)) {
    setUserMessage(QStringLiteral("license.error.file"));
    return;
  }
  const QString contents = QString::fromUtf8(file.readAll()).trimmed();
  // Files Camila sends are: optional comment lines, then the AVA1 token.
  QString token;
  const QStringList lines = contents.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
  for (const QString& line : lines) {
    const QString trimmed = line.trimmed();
    if (trimmed.startsWith(QLatin1String(LicenseConstants::kTokenPrefix))) {
      token = trimmed;
      break;
    }
  }
  if (token.isEmpty()) token = compactKey(contents);
  LicenseClaims claims;
  QString email;
  if (LicenseCrypto::verifyToken(token, &claims)) {
    email = claims.email;
  }
  activateLicense(email, token);
}

void LicenseManager::refreshFromServer() {
  if (!LicenseConfig::isApiConfigured()) return;
  if (store_.token.isEmpty()) return;
  postJson(QStringLiteral("/v1/license/check"),
           QJsonDocument(QJsonObject{{QStringLiteral("deviceId"), avaStableDeviceId()},
                                     {QStringLiteral("token"), store_.token}})
               .toJson(QJsonDocument::Compact));
}

void LicenseManager::loadOrCreateLocalState() {
  const QString currentDevice = avaStableDeviceId();
  QString blob;

  const QString filePath = stateFilePath();
  if (!filePath.isEmpty()) {
    QFile file(filePath);
    if (file.open(QIODevice::ReadOnly)) {
      blob = QString::fromUtf8(file.readAll()).trimmed();
      file.close();
    }
  }
  if (blob.isEmpty()) {
    QSettings settings;
    settings.beginGroup(QLatin1String(kSettingsGroup));
    blob = settings.value(QLatin1String(kStateKey)).toString().trimmed();
    settings.endGroup();
  }

  LicenseStoreRecord record;
  if (!blob.isEmpty() && LicenseCrypto::verifyStoreBlob(blob, &record)) {
    if (record.deviceId == currentDevice && LicenseCrypto::verifyToken(record.token, nullptr)) {
      store_ = record;
      bumpWallClock(unixNow());
      saveStore();
      return;
    }
  }

  startLocalTrial();
}

void LicenseManager::saveStore() {
  store_.deviceId = avaStableDeviceId();
  const QString blob = LicenseCrypto::signStore(store_);
  QSettings settings;
  settings.beginGroup(QLatin1String(kSettingsGroup));
  settings.setValue(QLatin1String(kStateKey), blob);
  settings.endGroup();
  const QString filePath = stateFilePath();
  if (filePath.isEmpty()) return;
  QFile file(filePath);
  if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    file.write(blob.toUtf8());
    file.write("\n");
  }
}

void LicenseManager::evaluateAndNotify() {
  const bool wasEntitled = status_.entitled;
  const LicenseLockReason previousReason = status_.lockReason;
  const QString previousKind = status_.kind;
  status_ = evaluate();
  if (status_.entitled != wasEntitled || status_.lockReason != previousReason ||
      status_.kind != previousKind) {
    emit entitlementChanged();
  }
}

LicenseUiStatus LicenseManager::evaluate() const {
  LicenseUiStatus status;
  const qint64 now = effectiveNow();
  LicenseClaims claims;
  if (!LicenseCrypto::verifyToken(store_.token, &claims)) {
    status.lockReason = LicenseLockReason::Invalid;
    return status;
  }
  if (!claims.deviceId.isEmpty() && claims.deviceId != avaStableDeviceId()) {
    status.lockReason = LicenseLockReason::OtherDevice;
    return status;
  }

  status.kind = claims.kind;
  status.email = claims.email;
  status.keyId = claims.keyId;
  status.expiresAt = claims.expiresAt;
  status.lastSuccessfulCheckAt = store_.lastSuccessfulCheckAt;
  status.daysRemaining = wholeDaysRemaining(claims.expiresAt, now);
  if (store_.lastSuccessfulCheckAt > 0) {
    status.graceEndsAt =
        store_.lastSuccessfulCheckAt + LicenseConstants::kOfflineGraceDays * LicenseConstants::kSecondsPerDay;
  }

  if (store_.serverStatus == QLatin1String(LicenseConstants::kServerStatusBlocked)) {
    status.lockReason = claims.kind == QLatin1String(LicenseConstants::kKindTrial)
                            ? LicenseLockReason::TrialEnded
                            : LicenseLockReason::PaidExpired;
    return status;
  }

  if (now >= claims.expiresAt) {
    status.lockReason = claims.kind == QLatin1String(LicenseConstants::kKindTrial)
                            ? LicenseLockReason::TrialEnded
                            : LicenseLockReason::PaidExpired;
    return status;
  }

  if (LicenseConfig::isApiConfigured() && store_.lastSuccessfulCheckAt > 0 &&
      now > store_.lastSuccessfulCheckAt +
                LicenseConstants::kOfflineGraceDays * LicenseConstants::kSecondsPerDay) {
    status.lockReason = LicenseLockReason::GraceExhausted;
    return status;
  }

  status.entitled = true;
  status.lockReason = LicenseLockReason::None;
  return status;
}

qint64 LicenseManager::effectiveNow() const {
  const qint64 wall = unixNow();
  return wall > store_.maxWallClockSeen ? wall : store_.maxWallClockSeen;
}

void LicenseManager::bumpWallClock(qint64 nowUnix) {
  if (nowUnix > store_.maxWallClockSeen) {
    store_.maxWallClockSeen = nowUnix;
  }
}

void LicenseManager::startLocalTrial() {
  const qint64 now = unixNow();
  LicenseClaims claims;
  claims.kind = QString::fromLatin1(LicenseConstants::kKindTrial);
  claims.email = QString();
  claims.deviceId = avaStableDeviceId();
  claims.keyId = QStringLiteral("t_") + QUuid::createUuid().toString(QUuid::WithoutBraces).left(12);
  claims.issuedAt = now;
  claims.expiresAt = now + LicenseConstants::kTrialDays * LicenseConstants::kSecondsPerDay;
  store_.token = LicenseCrypto::signClaims(claims);
  store_.deviceId = claims.deviceId;
  store_.lastSuccessfulCheckAt = 0;
  store_.maxWallClockSeen = now;
  store_.serverStatus = QString::fromLatin1(LicenseConstants::kServerStatusOk);
  saveStore();
}

void LicenseManager::postJson(const QString& path, const QByteArray& body) {
  if (!LicenseConfig::isApiConfigured()) return;
  setBusy(true);
  QNetworkRequest request{QUrl(LicenseConfig::apiUrl() + path)};
  request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
  request.setTransferTimeout(LicenseConstants::kNetworkTimeoutMs);
  QNetworkReply* reply = network_->post(request, body);
  connect(reply, &QNetworkReply::finished, this, [this, reply, path]() {
    reply->deleteLater();
    setBusy(false);
    if (reply->error() != QNetworkReply::NoError) {
      // Network errors must not lock. Keep last known-good local state.
      return;
    }
    handleServerPayload(path, reply->readAll());
  });
}

void LicenseManager::handleServerPayload(const QString& path, const QByteArray& raw) {
  const QJsonDocument document = QJsonDocument::fromJson(raw);
  if (!document.isObject()) return;
  const QJsonObject object = document.object();
  const bool ok = object.value(QStringLiteral("ok")).toBool(false);
  const QString token = object.value(QStringLiteral("token")).toString();
  const QJsonObject licenseObject = object.value(QStringLiteral("license")).toObject();

  if (ok && (!token.isEmpty() || !licenseObject.isEmpty())) {
    applyServerLicense(licenseObject, token, false);
    return;
  }

  const QString reason = object.value(QStringLiteral("reason")).toString();
  if (path.endsWith(QStringLiteral("/activate")) && !ok) {
    if (reason == QLatin1String("email_mismatch")) {
      setUserMessage(QStringLiteral("license.error.email_mismatch"));
    } else if (reason == QLatin1String("other_device")) {
      setUserMessage(QStringLiteral("license.error.other_device"));
    } else if (reason == QLatin1String("expired")) {
      setUserMessage(QStringLiteral("license.error.expired_key"));
    } else {
      setUserMessage(QStringLiteral("license.error.invalid_key"));
    }
    return;
  }

  if (path.endsWith(QStringLiteral("/check")) && !ok) {
    if (reason == QLatin1String("revoked") || reason == QLatin1String("expired") ||
        reason == QLatin1String("other_device")) {
      applyServerLicense(licenseObject, token, true);
    }
  }
}

void LicenseManager::applyServerLicense(const QJsonObject& licenseObject, const QString& token,
                                        bool markBlockedOnReject) {
  QString nextToken = token;
  if (nextToken.isEmpty()) {
    nextToken = store_.token;
  }
  LicenseClaims claims;
  if (!LicenseCrypto::verifyToken(nextToken, &claims)) {
    return;
  }
  if (!claims.deviceId.isEmpty() && claims.deviceId != avaStableDeviceId()) {
    if (markBlockedOnReject) {
      store_.serverStatus = QString::fromLatin1(LicenseConstants::kServerStatusBlocked);
      saveStore();
      evaluateAndNotify();
    }
    return;
  }

  store_.token = nextToken;
  store_.deviceId = avaStableDeviceId();
  store_.lastSuccessfulCheckAt = unixNow();
  bumpWallClock(store_.lastSuccessfulCheckAt);
  if (markBlockedOnReject) {
    store_.serverStatus = QString::fromLatin1(LicenseConstants::kServerStatusBlocked);
  } else {
    store_.serverStatus = QString::fromLatin1(LicenseConstants::kServerStatusOk);
  }
  Q_UNUSED(licenseObject);
  saveStore();
  evaluateAndNotify();
}

void LicenseManager::setBusy(bool busy) {
  if (busy_ == busy) return;
  busy_ = busy;
  emit busyChanged();
}

void LicenseManager::setUserMessage(const QString& message) {
  lastUserMessage_ = message;
  emit userMessageChanged();
}
