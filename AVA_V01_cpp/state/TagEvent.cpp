#include "TagEvent.h"

#include "EventCodeMap.h"
#include "FollowUpCatalog.h"

#include <QStringList>

#include <algorithm>

namespace TagEvent {
namespace {

QStringList payloadSegments(const QString& payload) {
  const QString trimmed = payload.trimmed();
  if (trimmed.isEmpty()) return {};

  QStringList rawParts;
  if (trimmed.contains(AppLocale::kCompoundPathSeparator)) {
    rawParts = trimmed.split(AppLocale::kCompoundPathSeparator, Qt::SkipEmptyParts);
  } else if (trimmed.contains(QStringLiteral("\u2192"))) {
    rawParts = trimmed.split(QStringLiteral("\u2192"), Qt::SkipEmptyParts);
  } else {
    rawParts.append(trimmed);
  }

  QStringList segments;
  segments.reserve(rawParts.size());
  for (const QString& part : rawParts) {
    const QString segment = part.trimmed();
    if (!segment.isEmpty()) segments.append(segment);
  }
  return segments;
}

bool segmentIsTeamLabel(const QString& segment, const QString& homeTeamName,
                        const QString& awayTeamName) {
  const auto matchesLabel = [&segment](const QString& label) {
    const QString trimmed = label.trimmed();
    return !trimmed.isEmpty() && segment.compare(trimmed, Qt::CaseInsensitive) == 0;
  };
  if (matchesLabel(homeTeamName) || matchesLabel(awayTeamName)) return true;
  if (segment.compare(QStringLiteral("home"), Qt::CaseInsensitive) == 0) return true;
  if (segment.compare(QStringLiteral("away"), Qt::CaseInsensitive) == 0) return true;
  return false;
}

bool segmentMatchesNode(const QString& segment, const EventCatalog::Node& node) {
  if (node.canonicalName &&
      segment.compare(QLatin1String(node.canonicalName), Qt::CaseInsensitive) == 0) {
    return true;
  }
  if (node.spanish && node.spanish[0] != '\0' &&
      segment.compare(QString::fromUtf8(node.spanish), Qt::CaseInsensitive) == 0) {
    return true;
  }
  if (node.displayLabel && node.displayLabel[0] != '\0' &&
      segment.compare(QString::fromUtf8(node.displayLabel), Qt::CaseInsensitive) == 0) {
    return true;
  }
  return false;
}

const EventCatalog::Node* matchingChild(const EventCatalog::Node& parent, const QString& segment) {
  for (int index = 0; index < parent.childCount; ++index) {
    const EventCatalog::Node* child =
        EventCatalog::nodeForId(parent.children[static_cast<std::size_t>(index)]);
    if (!child) continue;
    if (segmentMatchesNode(segment, *child)) return child;
  }
  return nullptr;
}

QStringList labelsForChain(const FollowUpChain& chain, bool spanish) {
  QStringList labels;
  const int used = chain.size();
  labels.reserve(used);
  for (int index = 0; index < used; ++index) {
    const QString label = EventCatalog::labelForId(chain.at(index), spanish);
    if (!label.isEmpty()) labels.append(label);
  }
  return labels;
}

QString joinPath(const QStringList& labels) {
  return labels.join(AppLocale::kCompoundPathSeparator);
}

}  // namespace

MainEventKey keyFromCanonicalName(const QString& name) {
  const QString trimmed = name.trimmed();
  if (trimmed.isEmpty()) return {};
  if (const EventCatalog::Node* node = EventCatalog::mainEventForCanonicalName(trimmed)) {
    return catalogKey(node->id);
  }
  return unlistedKey(trimmed);
}

QString canonicalName(EventCatalog::EventId id) {
  return EventCatalog::canonicalName(id);
}

QString canonicalName(const MainEventKey& key) {
  if (key.id != EventCatalog::EventId::None) return EventCatalog::canonicalName(key.id);
  return key.unlistedName;
}

QString canonicalFollowUpPath(const FollowUpChain& chain) {
  QStringList names;
  const int used = chain.size();
  names.reserve(used);
  for (int index = 0; index < used; ++index) {
    const QString name = EventCatalog::canonicalName(chain.at(index));
    if (!name.isEmpty()) names.append(name);
  }
  return joinPath(names);
}

QString displayLabel(EventCatalog::EventId id, AppLocale::Language language) {
  return EventCatalog::labelForId(id, language == AppLocale::Language::Spanish);
}

QString displayLabel(EventCatalog::EventId id) {
  return displayLabel(id, AppLocale::currentLanguage());
}

QString displayLabel(const MainEventKey& key, AppLocale::Language language) {
  if (key.id != EventCatalog::EventId::None) return displayLabel(key.id, language);
  return AppLocale::trEventForLanguage(key.unlistedName, language);
}

QString displayLabel(const MainEventKey& key) {
  return displayLabel(key, AppLocale::currentLanguage());
}

QString displayFollowUpPath(const FollowUpChain& chain, AppLocale::Language language) {
  return joinPath(labelsForChain(chain, language == AppLocale::Language::Spanish));
}

QString displayFollowUpPath(const FollowUpChain& chain) {
  return displayFollowUpPath(chain, AppLocale::currentLanguage());
}

QString displayTagLine(const MainEventKey& key, const FollowUpChain& chain,
                       AppLocale::Language language) {
  const QString mainLabel = displayLabel(key, language);
  const QString followUpLabel = displayFollowUpPath(chain, language);
  if (followUpLabel.isEmpty()) return mainLabel;
  if (mainLabel.isEmpty()) return followUpLabel;
  return mainLabel + AppLocale::kCompoundPathSeparator + followUpLabel;
}

QString displayTagLine(const MainEventKey& key, const FollowUpChain& chain) {
  return displayTagLine(key, chain, AppLocale::currentLanguage());
}

QString legacyFollowUpPayload(EventCatalog::EventId mainEventId, const FollowUpChain& chain,
                              const QString& teamLabel) {
  if (mainEventId == EventCatalog::EventId::None) return {};
  const QString mainEventName = EventCatalog::canonicalName(mainEventId);
  if (mainEventName.isEmpty()) return {};

  QStringList selections;
  const int used = chain.size();
  selections.reserve(used);
  for (int index = 0; index < used; ++index) {
    const QString name = EventCatalog::canonicalName(chain.at(index));
    if (!name.isEmpty()) selections.append(name);
  }
  return FollowUpCatalog::formatPayload(mainEventName, selections, teamLabel);
}

std::optional<FollowUpChain> parseLegacyFollowUpPayload(EventCatalog::EventId mainEventId,
                                                        const QString& payload,
                                                        const QString& homeTeamName,
                                                        const QString& awayTeamName) {
  const QString trimmedPayload = payload.trimmed();
  if (mainEventId == EventCatalog::EventId::None) {
    if (trimmedPayload.isEmpty()) return FollowUpChain{};
    return std::nullopt;
  }

  const EventCatalog::Node* mainNode = EventCatalog::nodeForId(mainEventId);
  if (!mainNode || !mainNode->mainEvent) {
    if (trimmedPayload.isEmpty()) return FollowUpChain{};
    return std::nullopt;
  }
  if (mainNode->payloadLayout == EventCatalog::PayloadLayout::TeamOnly) return FollowUpChain{};

  QStringList segments = payloadSegments(trimmedPayload);
  if (mainNode->payloadLayout == EventCatalog::PayloadLayout::TeamThenChain && !segments.isEmpty() &&
      segmentIsTeamLabel(segments.constFirst(), homeTeamName, awayTeamName)) {
    segments.removeFirst();
  }
  if (mainNode->payloadLayout == EventCatalog::PayloadLayout::ChainThenTeam && !segments.isEmpty() &&
      segmentIsTeamLabel(segments.constLast(), homeTeamName, awayTeamName)) {
    segments.removeLast();
  }
  if (segments.size() > EventCatalog::kMaxFollowUpDepth) return std::nullopt;

  FollowUpChain chain;
  const EventCatalog::Node* parent = mainNode;
  for (const QString& segment : segments) {
    const EventCatalog::Node* child = matchingChild(*parent, segment);
    if (!child) return std::nullopt;
    chain.ids[static_cast<std::size_t>(chain.count)] = child->id;
    ++chain.count;
    parent = child;
  }
  return chain;
}

MainEventKey mainEventKeyFromExportedCode(const QString& code) {
  const QString trimmed = code.trimmed();
  if (trimmed.isEmpty()) return {};
  if (EventDefaults::isTimeControlEvent(trimmed)) return unlistedKey(trimmed);

  const bool hasSign =
      trimmed.endsWith(QLatin1Char('+')) || trimmed.endsWith(QLatin1Char('-'));
  if (hasSign) {
    const QString withoutSign = trimmed.left(trimmed.size() - 1).trimmed();
    const int lastSpace = withoutSign.lastIndexOf(QLatin1Char(' '));
    if (lastSpace >= 0 && lastSpace + 1 < withoutSign.size()) {
      const QString shortCode = withoutSign.mid(lastSpace + 1);
      if (const std::optional<QString> mappedName =
              EventCodeMap::mainEventForShortCode(shortCode)) {
        return keyFromCanonicalName(*mappedName);
      }
    }
  }
  return keyFromCanonicalName(trimmed);
}

QList<MainEventKey> sortedByCanonicalName(QList<MainEventKey> keys) {
  std::sort(keys.begin(), keys.end(), [](const MainEventKey& left, const MainEventKey& right) {
    return canonicalName(left).compare(canonicalName(right), Qt::CaseInsensitive) < 0;
  });
  return keys;
}

}  // namespace TagEvent
