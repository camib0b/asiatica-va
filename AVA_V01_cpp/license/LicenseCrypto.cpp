#include "LicenseCrypto.h"
#include "LicenseConstants.h"

#include <QCryptographicHash>
#include <QMessageAuthenticationCode>
#include <QStringList>

namespace LicenseCrypto {
namespace {

QString tokenPrefix() {
  return QString::fromLatin1(LicenseConstants::kTokenPrefix);
}

QString storePrefix() {
  return QString::fromLatin1(LicenseConstants::kStorePrefix);
}

QString signPrefixed(const QString& prefix, const QString& canonicalPayload) {
  const QByteArray payloadBytes = canonicalPayload.toUtf8();
  const QByteArray signature = hmacSha256(signingSecret(), payloadBytes);
  return prefix + QString::fromLatin1(base64UrlEncode(payloadBytes)) + QLatin1Char('.') +
         QString::fromLatin1(base64UrlEncode(signature));
}

bool verifyPrefixed(const QString& prefix, const QString& token, QString* canonicalOut) {
  if (!token.startsWith(prefix)) return false;
  const QString rest = token.mid(prefix.size());
  const int dot = rest.lastIndexOf(QLatin1Char('.'));
  if (dot <= 0 || dot == rest.size() - 1) return false;
  const QByteArray payloadBytes = base64UrlDecode(rest.left(dot));
  const QByteArray givenSignature = base64UrlDecode(rest.mid(dot + 1));
  if (payloadBytes.isEmpty() || givenSignature.isEmpty()) return false;
  const QByteArray expectedSignature = hmacSha256(signingSecret(), payloadBytes);
  if (expectedSignature != givenSignature) return false;
  if (canonicalOut) *canonicalOut = QString::fromUtf8(payloadBytes);
  return true;
}

qint64 jsonNumberAfter(const QString& payload, const QString& key) {
  const QString needle = QLatin1Char('"') + key + QStringLiteral("\":");
  const int index = payload.indexOf(needle);
  if (index < 0) return 0;
  int cursor = index + needle.size();
  while (cursor < payload.size() && payload.at(cursor).isSpace()) ++cursor;
  const int start = cursor;
  if (cursor < payload.size() && payload.at(cursor) == QLatin1Char('-')) ++cursor;
  while (cursor < payload.size() && payload.at(cursor).isDigit()) ++cursor;
  bool ok = false;
  const qint64 value = payload.mid(start, cursor - start).toLongLong(&ok);
  return ok ? value : 0;
}

QString jsonStringAfter(const QString& payload, const QString& key) {
  const QString needle = QLatin1Char('"') + key + QStringLiteral("\":\"");
  const int startKey = payload.indexOf(needle);
  if (startKey < 0) return QString();
  int cursor = startKey + needle.size();
  QString value;
  while (cursor < payload.size()) {
    const QChar character = payload.at(cursor);
    if (character == QLatin1Char('"')) break;
    if (character == QLatin1Char('\\') && cursor + 1 < payload.size()) {
      const QChar escaped = payload.at(cursor + 1);
      if (escaped == QLatin1Char('n')) value += QLatin1Char('\n');
      else if (escaped == QLatin1Char('r')) value += QLatin1Char('\r');
      else if (escaped == QLatin1Char('t')) value += QLatin1Char('\t');
      else value += escaped;
      cursor += 2;
      continue;
    }
    value += character;
    ++cursor;
  }
  return value;
}

} // namespace

QByteArray signingSecret() {
#ifdef AVA_LICENSE_SIGNING_SECRET
  const QByteArray compiled = QByteArray(AVA_LICENSE_SIGNING_SECRET);
  if (!compiled.isEmpty()) return compiled;
#endif
  return QByteArray(LicenseConstants::kDefaultDevSigningSecret);
}

QString jsonEscape(const QString& value) {
  QString escaped;
  escaped.reserve(value.size());
  for (QChar character : value) {
    if (character == QLatin1Char('\\')) escaped += QStringLiteral("\\\\");
    else if (character == QLatin1Char('"')) escaped += QStringLiteral("\\\"");
    else if (character == QLatin1Char('\n')) escaped += QStringLiteral("\\n");
    else if (character == QLatin1Char('\r')) escaped += QStringLiteral("\\r");
    else if (character == QLatin1Char('\t')) escaped += QStringLiteral("\\t");
    else escaped += character;
  }
  return escaped;
}

QString canonicalClaimsPayload(const LicenseClaims& claims) {
  return QStringLiteral("{\"deviceId\":\"%1\",\"email\":\"%2\",\"expiresAt\":%3,\"issuedAt\":%4,"
                        "\"keyId\":\"%5\",\"kind\":\"%6\",\"v\":1}")
      .arg(jsonEscape(claims.deviceId), jsonEscape(claims.email),
           QString::number(claims.expiresAt), QString::number(claims.issuedAt),
           jsonEscape(claims.keyId), jsonEscape(claims.kind));
}

QString canonicalStorePayload(const LicenseStoreRecord& record) {
  const QString status = record.serverStatus.isEmpty()
                             ? QString::fromLatin1(LicenseConstants::kServerStatusOk)
                             : record.serverStatus;
  return QStringLiteral("{\"deviceId\":\"%1\",\"lastSuccessfulCheckAt\":%2,\"maxWallClockSeen\":%3,"
                        "\"serverStatus\":\"%4\",\"token\":\"%5\",\"v\":1}")
      .arg(jsonEscape(record.deviceId), QString::number(record.lastSuccessfulCheckAt),
           QString::number(record.maxWallClockSeen), jsonEscape(status), jsonEscape(record.token));
}

QByteArray hmacSha256(const QByteArray& secret, const QByteArray& message) {
  QMessageAuthenticationCode hmac(QCryptographicHash::Sha256, secret);
  hmac.addData(message);
  return hmac.result();
}

QByteArray base64UrlEncode(const QByteArray& data) {
  return data.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

QByteArray base64UrlDecode(const QString& text) {
  return QByteArray::fromBase64(text.toLatin1(),
                                QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

QString signClaims(const LicenseClaims& claims) {
  return signPrefixed(tokenPrefix(), canonicalClaimsPayload(claims));
}

bool verifyToken(const QString& token, LicenseClaims* claimsOut) {
  QString canonical;
  if (!verifyPrefixed(tokenPrefix(), token.trimmed(), &canonical)) return false;
  LicenseClaims claims;
  claims.version = 1;
  claims.deviceId = jsonStringAfter(canonical, QStringLiteral("deviceId"));
  claims.email = jsonStringAfter(canonical, QStringLiteral("email"));
  claims.expiresAt = jsonNumberAfter(canonical, QStringLiteral("expiresAt"));
  claims.issuedAt = jsonNumberAfter(canonical, QStringLiteral("issuedAt"));
  claims.keyId = jsonStringAfter(canonical, QStringLiteral("keyId"));
  claims.kind = jsonStringAfter(canonical, QStringLiteral("kind"));
  if (claims.kind != QLatin1String(LicenseConstants::kKindTrial) &&
      claims.kind != QLatin1String(LicenseConstants::kKindPaid)) {
    return false;
  }
  if (claims.expiresAt <= 0) return false;
  if (claimsOut) *claimsOut = claims;
  return true;
}

QString signStore(const LicenseStoreRecord& record) {
  return signPrefixed(storePrefix(), canonicalStorePayload(record));
}

bool verifyStoreBlob(const QString& blob, LicenseStoreRecord* recordOut) {
  QString canonical;
  if (!verifyPrefixed(storePrefix(), blob.trimmed(), &canonical)) return false;
  LicenseStoreRecord record;
  record.deviceId = jsonStringAfter(canonical, QStringLiteral("deviceId"));
  record.lastSuccessfulCheckAt = jsonNumberAfter(canonical, QStringLiteral("lastSuccessfulCheckAt"));
  record.maxWallClockSeen = jsonNumberAfter(canonical, QStringLiteral("maxWallClockSeen"));
  record.serverStatus = jsonStringAfter(canonical, QStringLiteral("serverStatus"));
  record.token = jsonStringAfter(canonical, QStringLiteral("token"));
  if (record.token.isEmpty()) return false;
  if (recordOut) *recordOut = record;
  return true;
}

bool selfTest() {
  LicenseClaims claims;
  claims.kind = QStringLiteral("paid");
  claims.email = QStringLiteral("coach@example.com");
  claims.deviceId = QString();
  claims.keyId = QStringLiteral("k_test1");
  claims.issuedAt = 1704067200;
  claims.expiresAt = 1893456000;
  const QString expected =
      QStringLiteral("AVA1.eyJkZXZpY2VJZCI6IiIsImVtYWlsIjoiY29hY2hAZXhhbXBsZS5jb20iLCJleHBpcmVzQXQiOjE4OTM0NTYwMDAsImlzc3VlZEF0IjoxNzA0MDY3MjAwLCJrZXlJZCI6ImtfdGVzdDEiLCJraW5kIjoicGFpZCIsInYiOjF9.PLj0fvb--oSW5sAp3FnHUHMdRgraaepgsDWExc0sY8M");
  if (canonicalClaimsPayload(claims) !=
      QLatin1String("{\"deviceId\":\"\",\"email\":\"coach@example.com\",\"expiresAt\":1893456000,"
                    "\"issuedAt\":1704067200,\"keyId\":\"k_test1\",\"kind\":\"paid\",\"v\":1}")) {
    return false;
  }
  if (signingSecret() != QByteArray(LicenseConstants::kDefaultDevSigningSecret)) {
    // Production builds use Camila's secret; skip the published vector.
    LicenseClaims roundTrip;
    return verifyToken(signClaims(claims), &roundTrip) && roundTrip.keyId == claims.keyId;
  }
  const QString token = signClaims(claims);
  if (token != expected) return false;
  LicenseClaims parsed;
  if (!verifyToken(token, &parsed)) return false;
  return parsed.email == claims.email && parsed.expiresAt == claims.expiresAt &&
         parsed.keyId == claims.keyId && parsed.kind == claims.kind;
}

} // namespace LicenseCrypto
