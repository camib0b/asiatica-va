#include "EventCodeMap.h"

#include <QHash>

#include <optional>

namespace EventCodeMap {

namespace {

const QHash<QString, QString>& canonicalToShortMap() {
  static const QHash<QString, QString> map = {
      {QStringLiteral("Goal"),         QStringLiteral("GOAL")},
      {QStringLiteral("Shot"),         QStringLiteral("SHOT")},
      {QStringLiteral("PC"),           QStringLiteral("PC")},
      {QStringLiteral("PC Foul"),      QStringLiteral("PCF")},
      {QStringLiteral("Card"),         QStringLiteral("CARD")},
      {QStringLiteral("Pass"),         QStringLiteral("PASS")},
      {QStringLiteral("Circle Entry"), QStringLiteral("ENTRY")},
      {QStringLiteral("16-yd"),        QStringLiteral("16YD")},
      {QStringLiteral("50-yd"),        QStringLiteral("50YD")},
      {QStringLiteral("75-yd"),        QStringLiteral("75YD")},
      {QStringLiteral("Turnover"),     QStringLiteral("TO")},
      {QStringLiteral("Special"),      QStringLiteral("SPC")},
      {QStringLiteral("PS"),           QStringLiteral("PS")},
      {QStringLiteral("S.O."),         QStringLiteral("SO")},
  };
  return map;
}

const QHash<QString, QString>& shortToCanonicalMap() {
  static const QHash<QString, QString> map = [] {
    QHash<QString, QString> inverted;
    for (auto it = canonicalToShortMap().cbegin(); it != canonicalToShortMap().cend(); ++it) {
      inverted.insert(it.value(), it.key());
    }
    return inverted;
  }();
  return map;
}

std::optional<QString> findMappedString(const QHash<QString, QString>& map,
                                        const QString& key) {
  const auto iterator = map.constFind(key);
  if (iterator == map.cend()) {
    return std::nullopt;
  }
  return iterator.value();
}

} // namespace

std::optional<QString> shortCodeForMainEvent(const QString& canonicalMainEvent) {
  return findMappedString(canonicalToShortMap(), canonicalMainEvent);
}

std::optional<QString> mainEventForShortCode(const QString& shortCode) {
  const QString normalizedShortCode = shortCode.trimmed().toUpper();
  return findMappedString(shortToCanonicalMap(), normalizedShortCode);
}

} // namespace EventCodeMap
