#include "EventDefaults.h"

#include "EventCatalog.h"

#include <QByteArray>
#include <QHash>
#include <QSettings>
#include <QString>
#include <QVariant>
#include <QVector>

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

/// The previous writer used this as the QSettings group. Percent-encoding
/// leaves '.' in place ("S.O.") and encodes a space ("PC Foul" -> "PC%20Foul").
/// New groups are the decimal catalog id.
QString legacySettingsGroupForEvent(const QString& canonicalMainEvent) {
  return QString::fromLatin1(canonicalMainEvent.toUtf8().toPercentEncoding("-_"));
}

QString settingsGroupForEventId(EventCatalog::EventId eventId) {
  return QString::number(static_cast<int>(eventId));
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
  QHash<int, EventDuration> overrides;
  bool loaded = false;
};

UserOverrideState& userOverrideState() {
  static UserOverrideState state;
  return state;
}

void writeOverrideGroup(QSettings& settings, const QString& groupName, const EventDuration& duration) {
  settings.beginGroup(groupName);
  settings.setValue(QLatin1String(kLeadSettingsKey), duration.leadMs);
  settings.setValue(QLatin1String(kLagSettingsKey), duration.lagMs);
  settings.endGroup();
}

void removeSettingsGroup(QSettings& settings, const QString& groupName) {
  settings.beginGroup(groupName);
  settings.remove(QString());
  settings.endGroup();
  settings.remove(groupName);
}

void removeLegacyGroups(QSettings& settings, const QString& canonicalName, const QString& idGroup) {
  const QString encoded = legacySettingsGroupForEvent(canonicalName);
  if (encoded != idGroup) removeSettingsGroup(settings, encoded);
  if (canonicalName != encoded && canonicalName != idGroup) {
    removeSettingsGroup(settings, canonicalName);
  }
}

void persistOverride(EventCatalog::EventId eventId, const QString& canonicalName,
                     const EventDuration& duration) {
  QSettings settings;
  settings.beginGroup(QLatin1String(kSettingsGroup));
  const QString idGroup = settingsGroupForEventId(eventId);
  writeOverrideGroup(settings, idGroup, duration);
  removeLegacyGroups(settings, canonicalName, idGroup);
  settings.endGroup();
}

// Caller must hold state.mutex. Copies a canonical-name group onto the catalog
// id, then removes the name group so the value is not read twice.
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
    const QString idGroup = settingsGroupForEventId(node.id);
    const QString encodedGroup = legacySettingsGroupForEvent(canonicalName);
    EventDuration duration;
    const bool loadedFromId = readOverrideDuration(settings, idGroup, &duration);
    const bool loadedFromEncoded =
        !loadedFromId && readOverrideDuration(settings, encodedGroup, &duration);
    const bool loadedFromRawName = !loadedFromId && !loadedFromEncoded &&
                                   encodedGroup != canonicalName &&
                                   readOverrideDuration(settings, canonicalName, &duration);
    if (!loadedFromId && !loadedFromEncoded && !loadedFromRawName) continue;

    state.overrides.insert(static_cast<int>(node.id), duration);
    if (!loadedFromId) writeOverrideGroup(settings, idGroup, duration);
    removeLegacyGroups(settings, canonicalName, idGroup);
  }
  settings.endGroup();
}

QVector<UserOverride> overridesInIdOrder(const QHash<int, EventDuration>& overrides) {
  QVector<UserOverride> listed;
  listed.reserve(overrides.size());
  for (auto iterator = overrides.cbegin(); iterator != overrides.cend(); ++iterator) {
    UserOverride override;
    override.eventId = static_cast<EventCatalog::EventId>(iterator.key());
    override.leadMs = iterator->leadMs;
    override.lagMs = iterator->lagMs;
    listed.append(override);
  }
  std::sort(listed.begin(), listed.end(), [](const UserOverride& left, const UserOverride& right) {
    return static_cast<int>(left.eventId) < static_cast<int>(right.eventId);
  });
  return listed;
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

  const EventCatalog::Node* node = playEvent(canonicalMainEvent);
  if (node == nullptr) return factoryDefaultFor(canonicalMainEvent);

  UserOverrideState& state = userOverrideState();
  std::lock_guard<std::mutex> lock(state.mutex);
  loadOverridesFromSettings(state);
  const auto iterator = state.overrides.constFind(static_cast<int>(node->id));
  if (iterator != state.overrides.cend()) return iterator.value();
  return EventDuration{node->leadMs, node->lagMs};
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
  const EventCatalog::Node* node = playEvent(canonicalMainEvent);
  if (node == nullptr) return;

  const EventDuration clamped = clampDuration(leadMs, lagMs);

  UserOverrideState& state = userOverrideState();
  std::lock_guard<std::mutex> lock(state.mutex);
  loadOverridesFromSettings(state);
  state.overrides.insert(static_cast<int>(node->id), clamped);
  persistOverride(node->id, canonicalMainEvent, clamped);
}

QVector<UserOverride> userOverrides() {
  UserOverrideState& state = userOverrideState();
  std::lock_guard<std::mutex> lock(state.mutex);
  loadOverridesFromSettings(state);
  return overridesInIdOrder(state.overrides);
}

void replaceUserOverrides(const QVector<UserOverride>& overrides) {
  QHash<int, EventDuration> replacement;
  for (const UserOverride& override : overrides) {
    const EventCatalog::Node* node = EventCatalog::nodeForId(override.eventId);
    if (node == nullptr || !node->mainEvent || !node->canonicalName) continue;
    replacement.insert(static_cast<int>(node->id), clampDuration(override.leadMs, override.lagMs));
  }

  UserOverrideState& state = userOverrideState();
  std::lock_guard<std::mutex> lock(state.mutex);
  state.overrides = replacement;
  state.loaded = true;

  QSettings settings;
  settings.remove(QLatin1String(kSettingsGroup));
  settings.beginGroup(QLatin1String(kSettingsGroup));
  for (auto iterator = replacement.cbegin(); iterator != replacement.cend(); ++iterator) {
    writeOverrideGroup(settings, QString::number(iterator.key()), iterator.value());
  }
  settings.endGroup();
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
  state.loaded = false;
  state.overrides.clear();
  loadOverridesFromSettings(state);
}

}  // namespace EventDefaults
