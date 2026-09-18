#pragma once

#include <QString>

#include <optional>

namespace EventCodeMap {

/// Short XML code for a canonical main-event name. `nullopt` when the event is not in the
/// team-affiliated map (neutral / pass-through / unknown).
std::optional<QString> shortCodeForMainEvent(const QString& canonicalMainEvent);

/// Canonical main-event name for a short XML code. `nullopt` when the code is unknown.
std::optional<QString> mainEventForShortCode(const QString& shortCode);

} // namespace EventCodeMap
