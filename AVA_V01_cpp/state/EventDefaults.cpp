#include "EventDefaults.h"

#include <QHash>
#include <QSettings>

#include <mutex>

namespace EventDefaults {

namespace {

constexpr char kSettingsGroup[] = "clipDurations";

const QHash<QString, EventDuration>& defaultDurationTable() {
  static const QHash<QString, EventDuration> table = {
      // Main grid (canonical English keys; matching GameControls).
      {QStringLiteral("Goal"),         {10000, 2000}},
      {QStringLiteral("Shot"),         {5000, 5000}},
      {QStringLiteral("PC"),           {2000, 8000}},
      {QStringLiteral("PC Foul"),      {5000, 2000}},
      {QStringLiteral("Card"),         {6000, 3000}},
      {QStringLiteral("Pass"),         {3000, 5000}},
      {QStringLiteral("Circle Entry"), {8000, 6000}},
      {QStringLiteral("16-yd"),        {3000, 10000}},
      {QStringLiteral("50-yd"),        {3000, 8000}},
      {QStringLiteral("75-yd"),        {3000, 8000}},
      {QStringLiteral("Turnover"),     {4000, 4000}},
      {QStringLiteral("Special"),      {4000, 4000}},
      {QStringLiteral("PS"),           {3000, 4000}},
      {QStringLiteral("S.O."),         {2000, 9000}},

      // Time-control codes: caller is expected to override start/end explicitly.
      {QString::fromLatin1(TimeCodes::kStartAnchor), {0, 2000}},
      {QString::fromLatin1(TimeCodes::kTimeout),     {0, 0}},
      {QString::fromLatin1(TimeCodes::kQuarter1),    {0, 0}},
      {QString::fromLatin1(TimeCodes::kQuarter2),    {0, 0}},
      {QString::fromLatin1(TimeCodes::kQuarter3),    {0, 0}},
      {QString::fromLatin1(TimeCodes::kQuarter4),    {0, 0}},
  };
  return table;
}

constexpr EventDuration kFallback{3000, 4000};

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
  settings.beginGroup(canonicalMainEvent);
  // Historical QSettings keys; kept so existing user clip-duration overrides still load.
  settings.setValue(QStringLiteral("preMs"), leadMs);
  settings.setValue(QStringLiteral("postMs"), lagMs);
  settings.endGroup();
  settings.endGroup();
}

// Caller must hold state.mutex.
void loadOverridesFromSettings(UserOverrideState& state) {
  if (state.loaded) return;
  state.loaded = true;

  QSettings settings;
  settings.beginGroup(QLatin1String(kSettingsGroup));
  const QStringList eventGroups = settings.childGroups();
  for (const QString& eventKey : eventGroups) {
    settings.beginGroup(eventKey);
    const QVariant leadValue = settings.value(QStringLiteral("preMs"));
    const QVariant lagValue = settings.value(QStringLiteral("postMs"));
    settings.endGroup();
    if (!leadValue.isValid() || !lagValue.isValid()) continue;

    const qint64 leadMs = leadValue.toLongLong();
    const qint64 lagMs = lagValue.toLongLong();
    if (leadMs < 0 || lagMs < 0) continue;
    state.overrides.insert(eventKey, {leadMs, lagMs});
  }
  settings.endGroup();
}

} // namespace

bool isQuarterEvent(const QString& canonicalMainEvent) {
  return canonicalMainEvent == QLatin1String(TimeCodes::kQuarter1) ||
         canonicalMainEvent == QLatin1String(TimeCodes::kQuarter2) ||
         canonicalMainEvent == QLatin1String(TimeCodes::kQuarter3) ||
         canonicalMainEvent == QLatin1String(TimeCodes::kQuarter4);
}

bool isTimeControlEvent(const QString& canonicalMainEvent) {
  if (isQuarterEvent(canonicalMainEvent)) return true;
  return canonicalMainEvent == QLatin1String(TimeCodes::kStartAnchor) ||
         canonicalMainEvent == QLatin1String(TimeCodes::kTimeout);
}

QStringList allConfigurableEventTypes() {
  return {
      QStringLiteral("Goal"),
      QStringLiteral("Shot"),
      QStringLiteral("PC"),
      QStringLiteral("PC Foul"),
      QStringLiteral("Card"),
      QStringLiteral("Pass"),
      QStringLiteral("Circle Entry"),
      QStringLiteral("16-yd"),
      QStringLiteral("50-yd"),
      QStringLiteral("75-yd"),
      QStringLiteral("Turnover"),
      QStringLiteral("Special"),
      QStringLiteral("PS"),
      QStringLiteral("S.O."),
  };
}

EventDuration factoryDefaultFor(const QString& canonicalMainEvent) {
  const auto& table = defaultDurationTable();
  const auto it = table.find(canonicalMainEvent);
  if (it != table.end()) return it.value();
  return kFallback;
}

EventDuration defaultFor(const QString& canonicalMainEvent) {
  UserOverrideState& state = userOverrideState();
  std::lock_guard<std::mutex> lock(state.mutex);
  loadOverridesFromSettings(state);
  const auto iterator = state.overrides.constFind(canonicalMainEvent);
  if (iterator != state.overrides.cend()) return iterator.value();
  return factoryDefaultFor(canonicalMainEvent);
}

QString quarterCode(int quarterIndex) {
  switch (quarterIndex) {
    case 0: return QString::fromLatin1(TimeCodes::kQuarter1);
    case 1: return QString::fromLatin1(TimeCodes::kQuarter2);
    case 2: return QString::fromLatin1(TimeCodes::kQuarter3);
    case 3: return QString::fromLatin1(TimeCodes::kQuarter4);
    default: return {};
  }
}

void setUserOverride(const QString& canonicalMainEvent, qint64 leadMs, qint64 lagMs) {
  if (isTimeControlEvent(canonicalMainEvent)) return;
  if (leadMs < 0) leadMs = 0;
  if (lagMs < 0) lagMs = 0;

  UserOverrideState& state = userOverrideState();
  std::lock_guard<std::mutex> lock(state.mutex);
  loadOverridesFromSettings(state);
  state.overrides.insert(canonicalMainEvent, {leadMs, lagMs});
  persistOverride(canonicalMainEvent, leadMs, lagMs);
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
