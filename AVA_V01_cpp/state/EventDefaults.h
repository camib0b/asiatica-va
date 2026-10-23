#pragma once

#include "EventCatalog.h"

#include <QString>
#include <QStringList>
#include <QVector>
#include <QtGlobal>

namespace EventDefaults {

struct EventDuration {
  qint64 leadMs = 0;  ///< Time before the event mark.
  qint64 lagMs = 0;   ///< Time after the event mark.
};

/// One user lead/lag override stored under the catalog id.
struct UserOverride {
  EventCatalog::EventId eventId = EventCatalog::EventId::None;
  qint64 leadMs = 0;
  qint64 lagMs = 0;
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

/// Factory lead/lag from EventCatalog for play events, and from the time-control
/// table for the start anchor, quarters, and timeouts. Ignores user overrides.
EventDuration factoryDefaultFor(const QString& canonicalMainEvent);

/// Effective default: user override when set, otherwise factory default. Thread-safe.
/// Time-control events always use factory defaults; QSettings cannot override them.
/// Overrides are keyed by catalog id. A value still stored under the canonical
/// English name is copied to the id on load and the name key is removed.
EventDuration defaultFor(const QString& canonicalMainEvent);

/// Quarter code Q1..Q4 for index 0..3; empty when the index is out of range.
QString quarterCode(int quarterIndex);

/// Persist a user override and update the in-memory cache. Thread-safe.
/// No-op for unknown or time-control events. Values are clamped to
/// [kMinLeadLagMs, kMaxLeadLagMs]. The QSettings group is the catalog id.
void setUserOverride(const QString& canonicalMainEvent, qint64 leadMs, qint64 lagMs);

/// User overrides in ascending catalog-id order. Thread-safe.
QVector<UserOverride> userOverrides();

/// Replaces every user override in memory and in QSettings. Unknown ids and
/// events that are not configurable main events are ignored. Values are
/// clamped. An empty list clears the store. Thread-safe.
void replaceUserOverrides(const QVector<UserOverride>& overrides);

/// Remove all user overrides from memory and QSettings. Thread-safe.
void clearUserOverrides();

/// Load persisted overrides from QSettings, migrating canonical-name keys to
/// catalog ids. Safe to call again; each call re-reads. Thread-safe.
void loadFromSettings();

} // namespace EventDefaults
