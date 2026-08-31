#pragma once

#include <QtGlobal>

namespace LicenseConstants {

/// First launch on a Mac that has never seen AVA gets this many days.
inline constexpr int kTrialDays = 14;

/// After a successful online check, keep working this long without the network.
inline constexpr int kOfflineGraceDays = 7;

/// HTTP timeout for license API calls. Stadium wifi should not freeze the UI.
inline constexpr int kNetworkTimeoutMs = 8000;

inline constexpr const char* kTokenPrefix = "AVA1.";
inline constexpr const char* kStorePrefix = "AVASTORE1.";

inline constexpr const char* kKindTrial = "trial";
inline constexpr const char* kKindPaid = "paid";

inline constexpr const char* kServerStatusOk = "ok";
inline constexpr const char* kServerStatusBlocked = "blocked";

inline constexpr const char* kDefaultDevSigningSecret =
    "ava-dev-signing-secret-not-for-production";

inline constexpr qint64 kSecondsPerDay = 24 * 60 * 60;

} // namespace LicenseConstants
