#include "EventDefaults.h"

#include <QHash>

namespace EventDefaults {

namespace {

const QHash<QString, EventDuration>& defaultDurationTable() {
  static const QHash<QString, EventDuration> table = {
      // Main grid (canonical English keys; matching GameControls).
      {QStringLiteral("Goal"),         {6000, 8000}},
      {QStringLiteral("Shot"),         {4000, 4000}},
      {QStringLiteral("PC"),           {5000, 8000}},
      {QStringLiteral("PC Foul"),      {4000, 6000}},
      {QStringLiteral("Card"),         {3000, 5000}},
      {QStringLiteral("Pass"),         {2000, 3000}},
      {QStringLiteral("Circle Entry"), {4000, 6000}},
      {QStringLiteral("16-yd"),        {3000, 4000}},
      {QStringLiteral("50-yd"),        {3000, 4000}},
      {QStringLiteral("75-yd"),        {3000, 4000}},
      {QStringLiteral("Turnover"),     {3000, 5000}},
      {QStringLiteral("Special"),      {3000, 5000}},
      {QStringLiteral("PS"),           {4000, 6000}},
      {QStringLiteral("S.O."),         {4000, 6000}},

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

EventDuration defaultFor(const QString& canonicalMainEvent) {
  const auto& table = defaultDurationTable();
  const auto it = table.find(canonicalMainEvent);
  if (it != table.end()) return it.value();
  return kFallback;
}

} // namespace EventDefaults
