#include "EventDefaults.h"

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

enum class EventKind { Play, StartAnchor, Timeout, Quarter };

struct EventSpec {
  const char* canonical;
  EventDuration factoryDuration;
  EventKind kind;
  int quarterIndex;  ///< 0..3 for quarters; -1 otherwise.
};

constexpr bool isPlayEvent(EventKind kind) { return kind == EventKind::Play; }

constexpr bool isTimeControlKind(EventKind kind) { return kind != EventKind::Play; }

constexpr std::array<EventSpec, 20> kEventCatalog{{
    { "Goal", {10000, 2000}, EventKind::Play, -1 },
    { "Shot", {5000, 5000}, EventKind::Play, -1 },
    { "PC", {2000, 8000}, EventKind::Play, -1 },
    { "PC Foul", {5000, 2000}, EventKind::Play, -1 },
    { "Card", {6000, 3000}, EventKind::Play, -1 },
    { "Pass", {3000, 5000}, EventKind::Play, -1 },
    { "Circle Entry", {8000, 6000}, EventKind::Play, -1 },
    { "16-yd", {3000, 10000}, EventKind::Play, -1 },
    { "50-yd", {3000, 8000}, EventKind::Play, -1 },
    { "75-yd", {3000, 8000}, EventKind::Play, -1 },
    { "Turnover", {4000, 4000}, EventKind::Play, -1 },
    { "Special", {4000, 4000}, EventKind::Play, -1 },
    { "PS", {3000, 4000}, EventKind::Play, -1 },
    { "S.O.", {2000, 9000}, EventKind::Play, -1 },
    { TimeCodes::kStartAnchor, {0, 2000}, EventKind::StartAnchor, -1 },
    { TimeCodes::kTimeout, {0, 0}, EventKind::Timeout, -1 },
    { TimeCodes::kQuarter1, {0, 0}, EventKind::Quarter, 0 },
    { TimeCodes::kQuarter2, {0, 0}, EventKind::Quarter, 1 },
    { TimeCodes::kQuarter3, {0, 0}, EventKind::Quarter, 2 },
    { TimeCodes::kQuarter4, {0, 0}, EventKind::Quarter, 3 },
}};

const EventSpec* specForCanonicalName(const QString& canonicalMainEvent) {
  for (const EventSpec& spec : kEventCatalog) {
    if (canonicalMainEvent == QLatin1String(spec.canonical)) return &spec;
  }
  return nullptr;
}

EventDuration clampDuration(qint64 leadMs, qint64 lagMs) {
  return {std::clamp(leadMs, kMinLeadLagMs, kMaxLeadLagMs),
          std::clamp(lagMs, kMinLeadLagMs, kMaxLeadLagMs)};
}

/// QSettings INI groups treat '.' as a path separator, so "S.O." must be encoded.
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
  for (const EventSpec& spec : kEventCatalog) {
    if (!isPlayEvent(spec.kind)) continue;

    const QString canonicalName = QString::fromLatin1(spec.canonical);
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

} // namespace

bool isQuarterEvent(const QString& canonicalMainEvent) {
  const EventSpec* spec = specForCanonicalName(canonicalMainEvent);
  return spec != nullptr && spec->kind == EventKind::Quarter;
}

bool isTimeControlEvent(const QString& canonicalMainEvent) {
  const EventSpec* spec = specForCanonicalName(canonicalMainEvent);
  return spec != nullptr && isTimeControlKind(spec->kind);
}

QStringList allConfigurableEventTypes() {
  QStringList names;
  names.reserve(static_cast<int>(kEventCatalog.size()));
  for (const EventSpec& spec : kEventCatalog) {
    if (isPlayEvent(spec.kind)) names.append(QString::fromLatin1(spec.canonical));
  }
  return names;
}

EventDuration factoryDefaultFor(const QString& canonicalMainEvent) {
  const EventSpec* spec = specForCanonicalName(canonicalMainEvent);
  if (spec == nullptr) return kFallback;
  return spec->factoryDuration;
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
  for (const EventSpec& spec : kEventCatalog) {
    if (spec.kind == EventKind::Quarter && spec.quarterIndex == quarterIndex) {
      return QString::fromLatin1(spec.canonical);
    }
  }
  return {};
}

void setUserOverride(const QString& canonicalMainEvent, qint64 leadMs, qint64 lagMs) {
  const EventSpec* spec = specForCanonicalName(canonicalMainEvent);
  if (spec == nullptr || !isPlayEvent(spec->kind)) return;

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

} // namespace EventDefaults
