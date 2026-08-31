#pragma once

#include <QString>
#include <QtGlobal>

struct LicenseClaims {
  int version = 1;
  QString kind;
  QString email;
  QString deviceId;
  QString keyId;
  qint64 issuedAt = 0;
  qint64 expiresAt = 0;
};

struct LicenseStoreRecord {
  QString token;
  QString deviceId;
  qint64 lastSuccessfulCheckAt = 0;
  qint64 maxWallClockSeen = 0;
  QString serverStatus;
};

enum class LicenseLockReason {
  None,
  TrialEnded,
  PaidExpired,
  GraceExhausted,
  OtherDevice,
  Invalid,
};

struct LicenseUiStatus {
  bool entitled = false;
  LicenseLockReason lockReason = LicenseLockReason::None;
  QString kind;
  QString email;
  QString keyId;
  qint64 expiresAt = 0;
  qint64 lastSuccessfulCheckAt = 0;
  qint64 graceEndsAt = 0;
  int daysRemaining = 0;
};
