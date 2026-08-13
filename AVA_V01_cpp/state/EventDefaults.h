#pragma once

#include <QString>
#include <QStringList>
#include <QtGlobal>

namespace EventDefaults {

struct EventDuration {
  qint64 preMs = 0;
  qint64 postMs = 0;
};

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
/// (start anchor, quarter spans, timeouts) and not by symmetric pre/post pads.
bool isTimeControlEvent(const QString& canonicalMainEvent);

/// All clip event types shown in Clip Duration Settings (fixed display order).
QStringList allConfigurableEventTypes();

/// Hard-coded factory defaults (ignores user overrides).
EventDuration factoryDefaultFor(const QString& canonicalMainEvent);

/// Effective default: user override when set, otherwise factory default.
EventDuration defaultFor(const QString& canonicalMainEvent);

bool hasUserOverride(const QString& canonicalMainEvent);

/// Persist a user override and update the in-memory cache.
void setUserOverride(const QString& canonicalMainEvent, qint64 preMs, qint64 postMs);

/// Remove all user overrides from memory and QSettings.
void clearUserOverrides();

/// Load persisted overrides from QSettings (call once at app startup).
void loadFromSettings();

} // namespace EventDefaults
