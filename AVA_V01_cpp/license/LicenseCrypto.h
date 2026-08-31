#pragma once

#include "LicenseTypes.h"

#include <QByteArray>
#include <QString>

namespace LicenseCrypto {

QByteArray signingSecret();

QString jsonEscape(const QString& value);
QString canonicalClaimsPayload(const LicenseClaims& claims);
QString canonicalStorePayload(const LicenseStoreRecord& record);

QByteArray hmacSha256(const QByteArray& secret, const QByteArray& message);
QByteArray base64UrlEncode(const QByteArray& data);
QByteArray base64UrlDecode(const QString& text);

QString signClaims(const LicenseClaims& claims);
bool verifyToken(const QString& token, LicenseClaims* claimsOut);

QString signStore(const LicenseStoreRecord& record);
bool verifyStoreBlob(const QString& blob, LicenseStoreRecord* recordOut);

/// Known-vector check used by packaging/self-test. Returns true when crypto matches Python/TS.
bool selfTest();

} // namespace LicenseCrypto
