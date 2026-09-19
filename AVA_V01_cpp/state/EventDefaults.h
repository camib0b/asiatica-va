#pragma once

#include <QString>
#include <QStringList>
#include <QtGlobal>

namespace EventDefaults {

struct EventDuration {
  qint64 leadMs = 0;  ///< Time before the event mark.
  qint64 lagMs = 0;   ///< Time after the event mark.
};

/// Inclusive bounds for user-editable lead/lag (clip-duration settings and presentation spins).
inline constexpr qint64 kMinLeadLagMs = 0;
inline constexpr qint64 kMaxLeadLagMs = 60000;

/// Canonical event names recognised as game-time anchors / quarter spans.
/// They are written verbatim as the XML <code> for their instances.
namespace TimeCodes {
inline constexpr const char* kStartAnchor = "Inicio";
inline constexpr const char* kTimeout = "TM";
inline constexpr const char* kQuarter1 = "Q1";
inline constexpr const char* kQuarter2 = "Q2";
inline constexpr const char* kQuarter3 = "Q3";
inline constexpr const char* kQuarter4 = "Q4";
} // namespace TimeCodes

/// True when the canonical event represents a quarter span (Q1..Q4).
bool isQuarterEvent(const QString& canonicalMainEvent);

/// True for time-control codes whose start/end are determined by user clicks
/// (start anchor, quarter spans, timeouts) and not by symmetric lead/lag pads.
bool isTimeControlEvent(const QString& canonicalMainEvent);

/// All clip event types shown in Clip Duration Settings (fixed display order).
QStringList allConfigurableEventTypes();

/// Hard-coded factory defaults (ignores user overrides).
EventDuration factoryDefaultFor(const QString& canonicalMainEvent);

/// Effective default: user override when set, otherwise factory default. Thread-safe.
/// Time-control events always use factory defaults; QSettings cannot override them.
EventDuration defaultFor(const QString& canonicalMainEvent);

/// Quarter code Q1..Q4 for index 0..3; empty when the index is out of range.
QString quarterCode(int quarterIndex);

/// Persist a user override and update the in-memory cache. Thread-safe.
/// No-op for unknown or time-control events. Values are clamped to
/// [kMinLeadLagMs, kMaxLeadLagMs].
void setUserOverride(const QString& canonicalMainEvent, qint64 leadMs, qint64 lagMs);

/// Remove all user overrides from memory and QSettings. Thread-safe.
void clearUserOverrides();

/// Load persisted overrides from QSettings (call once at app startup). Thread-safe.
void loadFromSettings();

} // namespace EventDefaults
