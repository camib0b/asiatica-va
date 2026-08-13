#pragma once

#include <QString>

namespace EventCodeMap {

/// Returns the short XML code for a canonical main-event name, or empty when the event
/// has no team-affiliated short code (i.e. it is a neutral / pass-through code).
QString shortCodeForMainEvent(const QString& canonicalMainEvent);

/// Returns the canonical main-event name for a short XML code, or empty when unknown.
QString mainEventForShortCode(const QString& shortCode);

} // namespace EventCodeMap
