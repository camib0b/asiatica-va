#include "EventDefaults.h"

#include "EventCatalog.h"

#include <QByteArray>
#include <QHash>
#include <QSettings>
#include <QString>
#include <QVariant>

#include <algorithm>
#include <array>
#include <mutex>

namespace EventDefaults {

namespace {

constexpr char kSettingsGroup[] = "clipDurations";
constexpr char kLeadSettingsKey[] = "preMs";
constexpr char kLagSettingsKey[] = "postMs";

constexpr EventDuration kFallback{3000, 4000};

enum class EventKind { StartAnchor, Timeout, Quarter };

struct EventSpec {
  const char* canonical;
  EventDuration factoryDuration;
  EventKind kind;
  int quarterIndex;  ///< 0..3 for quarters; -1 otherwise.
};

// Play-event lead/lag lives on EventCatalog. These codes are not grid events.
// Their XML <code> is the canonical name itself.
constexpr std::array<EventSpec, 6> kTimeControlEvents{{
    {TimeCodes::kStartAnchor, {0, 2000}, EventKind::StartAnchor, -1},
    {TimeCodes::kTimeout, {0, 0}, EventKind::Timeout, -1},
    {TimeCodes::kQuarter1, {0, 0}, EventKind::Quarter, 0},
    {TimeCodes::kQuarter2, {0, 0}, EventKind::Quarter, 1},
    {TimeCodes::kQuarter3, {0, 0}, EventKind::Quarter, 2},
    {TimeCodes::kQuarter4, {0, 0}, EventKind::Quarter, 3},
}};

const EventCatalog::Node* playEvent(const QString& canonicalMainEvent) {
  return EventCatalog::mainEventForCanonicalName(canonicalMainEvent);
}

const EventSpec* timeControlEvent(const QString& canonicalMainEvent) {
  for (const EventSpec& spec : kTimeControlEvents) {
    if (canonicalMainEvent == QLatin1String(spec.canonical)) return &spec;
  }
  return nullptr;
}

EventDuration clampDuration(qint64 leadMs, qint64 lagMs) {
  return {std::clamp(leadMs, kMinLeadLagMs, kMaxLeadLagMs),
          std::clamp(lagMs, kMinLeadLagMs, kMaxLeadLagMs)};
}

/// QSettings INI groups treat '.' as a path separator, so "S.O." must be encoded.
/// The group name is the canonical English event name, not the catalog id.
QString settingsGroupForEvent(const QString& canonicalMainEvent) {
  return QString::fromLatin1(canonicalMainEvent.toUtf8().toPercentEncoding("-_"));
}

bool readOverrideDuration(QSettings& settings, const QString& groupName, EventDuration* duration) {
  settings.beginGroup(groupName);
  const QVariant leadValue = settings.value(QLatin1String(kLeadSettingsKey));
  const QVariant lagValue = settings.value(QLatin1String(kLagSettingsKey));
  settings.endGroup();

  if (!leadValue.isValid() || !lagValue.isValid()) return false;
  if (!leadValue.canConvert<qlonglong>() || !lagValue.canConvert<qlonglong>()) return false;

  bool leadOk = false;
  bool lagOk = false;
  const qint64 leadMs = leadValue.toLongLong(&leadOk);
  const qint64 lagMs = lagValue.toLongLong(&lagOk);
  if (!leadOk || !lagOk) return false;

  *duration = clampDuration(leadMs, lagMs);
  return true;
}

struct UserOverrideState {
  std::mutex mutex;
  QHash<QString, EventDuration> overrides;
  bool loaded = false;
};

UserOverrideState& userOverrideState() {
  static UserOverrideState state;
  return state;
}

void persistOverride(const QString& canonicalMainEvent, qint64 leadMs, qint64 lagMs) {
  QSettings settings;
  settings.beginGroup(QLatin1String(kSettingsGroup));
  settings.beginGroup(settingsGroupForEvent(canonicalMainEvent));
  // Historical QSettings keys; kept so existing user clip-duration overrides still load.
  settings.setValue(QLatin1String(kLeadSettingsKey), leadMs);
  settings.setValue(QLatin1String(kLagSettingsKey), lagMs);
  settings.endGroup();
  settings.endGroup();
}

// Caller must hold state.mutex.
void loadOverridesFromSettings(UserOverrideState& state) {
  if (state.loaded) return;
  state.loaded = true;

  QSettings settings;
  settings.beginGroup(QLatin1String(kSettingsGroup));
  const int nodeCount = EventCatalog::nodeCount();
  for (int index = 0; index < nodeCount; ++index) {
    const EventCatalog::Node& node = EventCatalog::nodeAt(index);
    if (!node.mainEvent || !node.canonicalName) continue;

    const QString canonicalName = QString::fromLatin1(node.canonicalName);
    EventDuration duration;
    const bool loadedEncoded =
        readOverrideDuration(settings, settingsGroupForEvent(canonicalName), &duration);
    const bool loadedLegacy =
        !loadedEncoded && readOverrideDuration(settings, canonicalName, &duration);
    if (!loadedEncoded && !loadedLegacy) continue;
    state.overrides.insert(canonicalName, duration);
  }
  settings.endGroup();
}

}  // namespace

bool isQuarterEvent(const QString& canonicalMainEvent) {
  const EventSpec* spec = timeControlEvent(canonicalMainEvent);
  return spec != nullptr && spec->kind == EventKind::Quarter;
}

bool isTimeControlEvent(const QString& canonicalMainEvent) {
  return timeControlEvent(canonicalMainEvent) != nullptr;
}

QStringList allConfigurableEventTypes() {
  QStringList names;
  const int nodeCount = EventCatalog::nodeCount();
  names.reserve(nodeCount);
  for (int index = 0; index < nodeCount; ++index) {
    const EventCatalog::Node& node = EventCatalog::nodeAt(index);
    if (!node.mainEvent || !node.canonicalName) continue;
    names.append(QString::fromLatin1(node.canonicalName));
  }
  return names;
}

EventDuration factoryDefaultFor(const QString& canonicalMainEvent) {
  if (const EventCatalog::Node* node = playEvent(canonicalMainEvent)) {
    return EventDuration{node->leadMs, node->lagMs};
  }
  if (const EventSpec* spec = timeControlEvent(canonicalMainEvent)) return spec->factoryDuration;
  return kFallback;
}

EventDuration defaultFor(const QString& canonicalMainEvent) {
  if (isTimeControlEvent(canonicalMainEvent)) return factoryDefaultFor(canonicalMainEvent);

  UserOverrideState& state = userOverrideState();
  std::lock_guard<std::mutex> lock(state.mutex);
  loadOverridesFromSettings(state);
  const auto iterator = state.overrides.constFind(canonicalMainEvent);
  if (iterator != state.overrides.cend()) return iterator.value();
  return factoryDefaultFor(canonicalMainEvent);
}

QString quarterCode(int quarterIndex) {
  for (const EventSpec& spec : kTimeControlEvents) {
    if (spec.kind == EventKind::Quarter && spec.quarterIndex == quarterIndex) {
      return QString::fromLatin1(spec.canonical);
    }
  }
  return {};
}

void setUserOverride(const QString& canonicalMainEvent, qint64 leadMs, qint64 lagMs) {
  if (playEvent(canonicalMainEvent) == nullptr) return;

  const EventDuration clamped = clampDuration(leadMs, lagMs);

  UserOverrideState& state = userOverrideState();
  std::lock_guard<std::mutex> lock(state.mutex);
  loadOverridesFromSettings(state);
  state.overrides.insert(canonicalMainEvent, clamped);
  persistOverride(canonicalMainEvent, clamped.leadMs, clamped.lagMs);
}

void clearUserOverrides() {
  UserOverrideState& state = userOverrideState();
  std::lock_guard<std::mutex> lock(state.mutex);
  state.overrides.clear();
  state.loaded = true;

  QSettings settings;
  settings.remove(QLatin1String(kSettingsGroup));
}

void loadFromSettings() {
  UserOverrideState& state = userOverrideState();
  std::lock_guard<std::mutex> lock(state.mutex);
  loadOverridesFromSettings(state);
}

}  // namespace EventDefaults
