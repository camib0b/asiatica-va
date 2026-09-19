#include "EventCodeMap.h"

#include <QHash>
#include <QString>
#include <QVector>
#include <QtGlobal>

#include <optional>

namespace EventCodeMap {

namespace {

struct EventCodePair {
  QString canonicalName;
  QString shortCode;
};

/// Single source of truth. Both lookup hashes are derived from this table.
const QVector<EventCodePair>& eventCodePairs() {
  static const QVector<EventCodePair> pairs = {
      {QStringLiteral("Goal"), QStringLiteral("GOAL")},
      {QStringLiteral("Shot"), QStringLiteral("SHOT")},
      {QStringLiteral("PC"), QStringLiteral("PC")},
      {QStringLiteral("PC Foul"), QStringLiteral("PCF")},
      {QStringLiteral("Card"), QStringLiteral("CARD")},
      {QStringLiteral("Pass"), QStringLiteral("PASS")},
      {QStringLiteral("Circle Entry"), QStringLiteral("ENTRY")},
      {QStringLiteral("16-yd"), QStringLiteral("16YD")},
      {QStringLiteral("50-yd"), QStringLiteral("50YD")},
      {QStringLiteral("75-yd"), QStringLiteral("75YD")},
      {QStringLiteral("Turnover"), QStringLiteral("TO")},
      {QStringLiteral("Special"), QStringLiteral("SPC")},
      {QStringLiteral("PS"), QStringLiteral("PS")},
      {QStringLiteral("S.O."), QStringLiteral("SO")},
  };
  return pairs;
}

QString normalizedCanonicalKey(const QString& canonicalMainEvent) {
  return canonicalMainEvent.trimmed().toLower();
}

QString normalizedShortKey(const QString& shortCode) {
  return shortCode.trimmed().toUpper();
}

struct EventCodeTables {
  QHash<QString, QString> canonicalToShort;
  QHash<QString, QString> shortToCanonical;
};

const EventCodeTables& eventCodeTables() {
  static const EventCodeTables tables = [] {
    EventCodeTables built;
    const QVector<EventCodePair>& pairs = eventCodePairs();
    built.canonicalToShort.reserve(pairs.size());
    built.shortToCanonical.reserve(pairs.size());

    for (const EventCodePair& pair : pairs) {
      const QString canonicalKey = normalizedCanonicalKey(pair.canonicalName);
      const QString shortKey = normalizedShortKey(pair.shortCode);

      if (built.canonicalToShort.contains(canonicalKey)) {
        Q_ASSERT_X(false, "EventCodeMap", "duplicate canonical event name in event code table");
        continue;
      }
      if (built.shortToCanonical.contains(shortKey)) {
        Q_ASSERT_X(false, "EventCodeMap", "duplicate short code in event code table");
        continue;
      }

      built.canonicalToShort.insert(canonicalKey, pair.shortCode);
      built.shortToCanonical.insert(shortKey, pair.canonicalName);
    }

    Q_ASSERT_X(built.canonicalToShort.size() == pairs.size() &&
                   built.shortToCanonical.size() == pairs.size(),
               "EventCodeMap", "event code table is not round-trippable");
    return built;
  }();
  return tables;
}

std::optional<QString> findMappedString(const QHash<QString, QString>& map, const QString& key) {
  const auto iterator = map.constFind(key);
  if (iterator == map.cend()) return std::nullopt;
  return iterator.value();
}

} // namespace

std::optional<QString> shortCodeForMainEvent(const QString& canonicalMainEvent) {
  return findMappedString(eventCodeTables().canonicalToShort,
                          normalizedCanonicalKey(canonicalMainEvent));
}

std::optional<QString> mainEventForShortCode(const QString& shortCode) {
  return findMappedString(eventCodeTables().shortToCanonical, normalizedShortKey(shortCode));
}

} // namespace EventCodeMap
