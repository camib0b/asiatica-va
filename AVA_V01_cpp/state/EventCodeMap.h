#pragma once

#include <QString>

#include <optional>

namespace EventCodeMap {

/// Short XML code for a canonical main-event name. `nullopt` when the event is not in the
/// team-affiliated map (neutral / pass-through / unknown).
///
/// Lookup trims surrounding whitespace and is case-insensitive. The returned code is the
/// canonical short form from the table (e.g. "ENTRY"), not the caller's spelling.
std::optional<QString> shortCodeForMainEvent(const QString& canonicalMainEvent);

/// Canonical main-event name for a short XML code. `nullopt` when the code is unknown.
///
/// Lookup trims surrounding whitespace and uppercases the code. The returned name is the
/// canonical table spelling (e.g. "Circle Entry").
std::optional<QString> mainEventForShortCode(const QString& shortCode);

} // namespace EventCodeMap
