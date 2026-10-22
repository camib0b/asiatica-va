#include "EventCodeMap.h"

#include "EventCatalog.h"

#include <QHash>
#include <QString>

#include <optional>

namespace EventCodeMap {

namespace {

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
    const int nodeCount = EventCatalog::nodeCount();
    built.canonicalToShort.reserve(nodeCount);
    built.shortToCanonical.reserve(nodeCount);

    int mainEventCount = 0;
    for (int index = 0; index < nodeCount; ++index) {
      const EventCatalog::Node& node = EventCatalog::nodeAt(index);
      if (!node.mainEvent) continue;
      ++mainEventCount;

      if (!node.canonicalName || !node.xmlCode || node.xmlCode[0] == '\0') {
        Q_ASSERT_X(false, "EventCodeMap", "main event is missing an XML code");
        continue;
      }

      const QString canonicalName = QString::fromLatin1(node.canonicalName);
      const QString shortCode = QString::fromLatin1(node.xmlCode);
      const QString canonicalKey = normalizedCanonicalKey(canonicalName);
      const QString shortKey = normalizedShortKey(shortCode);

      if (built.canonicalToShort.contains(canonicalKey)) {
        Q_ASSERT_X(false, "EventCodeMap", "duplicate canonical event name in event code table");
        continue;
      }
      if (built.shortToCanonical.contains(shortKey)) {
        Q_ASSERT_X(false, "EventCodeMap", "duplicate short code in event code table");
        continue;
      }

      built.canonicalToShort.insert(canonicalKey, shortCode);
      built.shortToCanonical.insert(shortKey, canonicalName);
    }

    Q_ASSERT_X(built.canonicalToShort.size() == mainEventCount &&
                   built.shortToCanonical.size() == mainEventCount,
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

}  // namespace

std::optional<QString> shortCodeForMainEvent(const QString& canonicalMainEvent) {
  return findMappedString(eventCodeTables().canonicalToShort,
                          normalizedCanonicalKey(canonicalMainEvent));
}

std::optional<QString> mainEventForShortCode(const QString& shortCode) {
  return findMappedString(eventCodeTables().shortToCanonical, normalizedShortKey(shortCode));
}

}  // namespace EventCodeMap
