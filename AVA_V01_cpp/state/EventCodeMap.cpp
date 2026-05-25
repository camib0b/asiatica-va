#include "EventCodeMap.h"

#include <QHash>

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

} // namespace

QString shortCodeForMainEvent(const QString& canonicalMainEvent) {
  return canonicalToShortMap().value(canonicalMainEvent);
}

QString mainEventForShortCode(const QString& shortCode) {
  return shortToCanonicalMap().value(shortCode.trimmed().toUpper());
}

} // namespace EventCodeMap
