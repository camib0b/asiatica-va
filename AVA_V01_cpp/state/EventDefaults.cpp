#include "EventDefaults.h"

#include <QHash>
#include <QSettings>

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
      {QStringLiteral("Circle Entry"), {5000, 6000}},
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

QHash<QString, EventDuration>& userOverrides() {
  static QHash<QString, EventDuration> overrides;
  return overrides;
}

bool overridesLoaded = false;

void persistOverride(const QString& canonicalMainEvent, qint64 preMs, qint64 postMs) {
  QSettings settings;
  settings.beginGroup(QLatin1String(kSettingsGroup));
  settings.beginGroup(canonicalMainEvent);
  settings.setValue(QStringLiteral("preMs"), preMs);
  settings.setValue(QStringLiteral("postMs"), postMs);
  settings.endGroup();
  settings.endGroup();
}

void ensureOverridesLoaded() {
  if (overridesLoaded) return;
  overridesLoaded = true;

  QSettings settings;
  settings.beginGroup(QLatin1String(kSettingsGroup));
  const QStringList eventGroups = settings.childGroups();
  for (const QString& eventKey : eventGroups) {
    settings.beginGroup(eventKey);
    const QVariant preValue = settings.value(QStringLiteral("preMs"));
    const QVariant postValue = settings.value(QStringLiteral("postMs"));
    settings.endGroup();
    if (!preValue.isValid() || !postValue.isValid()) continue;

    const qint64 preMs = preValue.toLongLong();
    const qint64 postMs = postValue.toLongLong();
    if (preMs < 0 || postMs < 0) continue;
    userOverrides().insert(eventKey, {preMs, postMs});
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
  ensureOverridesLoaded();
  const auto overrideIt = userOverrides().constFind(canonicalMainEvent);
  if (overrideIt != userOverrides().constEnd()) return overrideIt.value();
  return factoryDefaultFor(canonicalMainEvent);
}

bool hasUserOverride(const QString& canonicalMainEvent) {
  ensureOverridesLoaded();
  return userOverrides().contains(canonicalMainEvent);
}

void setUserOverride(const QString& canonicalMainEvent, qint64 preMs, qint64 postMs) {
  if (isTimeControlEvent(canonicalMainEvent)) return;
  if (preMs < 0) preMs = 0;
  if (postMs < 0) postMs = 0;

  ensureOverridesLoaded();
  userOverrides().insert(canonicalMainEvent, {preMs, postMs});
  persistOverride(canonicalMainEvent, preMs, postMs);
}

void clearUserOverrides() {
  ensureOverridesLoaded();
  userOverrides().clear();

  QSettings settings;
  settings.remove(QLatin1String(kSettingsGroup));
}

void loadFromSettings() {
  ensureOverridesLoaded();
}

} // namespace EventDefaults
