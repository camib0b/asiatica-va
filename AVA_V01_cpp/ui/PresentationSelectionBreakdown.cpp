#include "PresentationSelectionBreakdown.h"

#include "EventCatalog.h"
#include "EventLabelResolver.h"
#include "TagSession.h"

#include <QHash>

#include <algorithm>
#include <utility>

namespace PresentationSelectionBreakdown {
namespace {

/// Catalog array order of main events. GameControls keyboard order is
/// buttonGridNodeAt and is a different sequence.
int catalogOrdinal(const TagEvent::MainEventKey& key) {
  if (key.id == EventCatalog::EventId::None) return 1000000;

  int mainOrdinal = 0;
  int followUpOrdinal = -1;
  const int nodeCount = EventCatalog::nodeCount();
  for (int index = 0; index < nodeCount; ++index) {
    const EventCatalog::Node& node = EventCatalog::nodeAt(index);
    if (node.id == key.id && !node.mainEvent && followUpOrdinal < 0) {
      followUpOrdinal = 100000 + index;
    }
    if (!node.mainEvent) continue;
    if (node.id == key.id) return mainOrdinal;
    ++mainOrdinal;
  }
  if (followUpOrdinal >= 0) return followUpOrdinal;
  return 500000 + static_cast<int>(key.id);
}

bool catalogComesBefore(const TagEvent::MainEventKey& left, const TagEvent::MainEventKey& right) {
  const int leftOrdinal = catalogOrdinal(left);
  const int rightOrdinal = catalogOrdinal(right);
  if (leftOrdinal != rightOrdinal) return leftOrdinal < rightOrdinal;
  if (left.id == EventCatalog::EventId::None && right.id == EventCatalog::EventId::None) {
    const int nameOrder = left.unlistedName.compare(right.unlistedName, Qt::CaseInsensitive);
    if (nameOrder != 0) return nameOrder < 0;
    return left.unlistedName < right.unlistedName;
  }
  return false;
}

EventCount makeCatalogRow(EventCatalog::EventId id, const QString& label, int count) {
  EventCount row;
  row.eventKey = TagEvent::catalogKey(id);
  row.displayLabel = label;
  row.selectedCount = count;
  return row;
}

EventCount makeUnlistedRow(const QString& name, int count) {
  EventCount row;
  row.eventKey = TagEvent::unlistedKey(name);
  row.displayLabel = name;
  row.selectedCount = count;
  return row;
}

}  // namespace

QVector<EventCount> inCatalogOrder(QVector<EventCount> events) {
  QHash<TagEvent::MainEventKey, int> indexByKey;
  QVector<EventCount> merged;
  merged.reserve(events.size());
  for (EventCount event : events) {
    if (event.eventKey.isEmpty() || event.eventKey.isTimeControl()) continue;
    if (event.selectedCount <= 0) continue;
    if (event.displayLabel.trimmed().isEmpty()) {
      event.displayLabel = TagEvent::canonicalName(event.eventKey);
    }
    const auto found = indexByKey.constFind(event.eventKey);
    if (found == indexByKey.cend()) {
      indexByKey.insert(event.eventKey, merged.size());
      merged.append(event);
      continue;
    }
    EventCount& existing = merged[found.value()];
    existing.selectedCount += event.selectedCount;
    if (existing.displayLabel.trimmed().isEmpty()) existing.displayLabel = event.displayLabel;
  }

  std::stable_sort(merged.begin(), merged.end(),
                   [](const EventCount& left, const EventCount& right) {
                     return catalogComesBefore(left.eventKey, right.eventKey);
                   });
  return merged;
}

Result fromSelectedKeys(const QVector<TagEvent::MainEventKey>& selectedKeys,
                        const TagEvent::MainEventKey& filteredEvent, bool filterIsAllEvents,
                        const TagSession* session) {
  QHash<TagEvent::MainEventKey, int> counts;
  for (const TagEvent::MainEventKey& key : selectedKeys) {
    if (key.isEmpty() || key.isTimeControl()) continue;
    counts[key] += 1;
  }

  QVector<EventCount> events;
  events.reserve(counts.size());
  for (auto iterator = counts.cbegin(); iterator != counts.cend(); ++iterator) {
    EventCount row;
    row.eventKey = iterator.key();
    row.selectedCount = iterator.value();
    row.displayLabel = EventLabelResolver::labelForKey(row.eventKey, session);
    events.append(row);
  }

  Result result;
  result.events = inCatalogOrder(std::move(events));

  // Two or more events always need the list. One event needs it only when
  // those clips are outside the current Event filter, so the total in the
  // summary can be opened. All events, or the filter already on that event,
  // stays a plain summary.
  bool selectionOutsideFilter = false;
  if (!filterIsAllEvents) {
    for (const EventCount& event : result.events) {
      if (event.eventKey != filteredEvent) selectionOutsideFilter = true;
    }
  }
  result.interactive = result.events.size() > 1 || selectionOutsideFilter;
  return result;
}

Result worstCaseResult() {
  // Unsorted on purpose. A zero count is included so the popup cannot show it.
  // Labels are the realistic long, short, markup, and non-Latin cases.
  const QVector<EventCount> raw = {
      makeUnlistedRow(QStringLiteral("bartholomew.fitzgerald@northwind-industries-holdings.example.com"),
                      2),
      makeCatalogRow(EventCatalog::EventId::Goal, QStringLiteral("Gol"), 0),
      makeCatalogRow(EventCatalog::EventId::Shootout,
                     QStringLiteral("Senior Product Design Engineer, Platform Infrastructure"), 8),
      makeCatalogRow(EventCatalog::EventId::Shot, QStringLiteral("Benachrichtigungseinstellungen"), 1),
      makeUnlistedRow(QStringLiteral("customer-feedback-from-enterprise-onboarding"), 5),
      makeCatalogRow(EventCatalog::EventId::Pass, QStringLiteral("<script>alert(1)</script>"), 1),
      makeCatalogRow(EventCatalog::EventId::CircleEntry,
                     QStringLiteral("Aleksandra Wiśniewska-Kowalczyk"), 2),
      makeCatalogRow(EventCatalog::EventId::SixteenYard, QStringLiteral("J"), 1),
      makeCatalogRow(EventCatalog::EventId::Special, QStringLiteral("☆"), 1),
      makeCatalogRow(EventCatalog::EventId::PenaltyCorner, QStringLiteral("Corto"), 1284),
      makeCatalogRow(EventCatalog::EventId::Turnover, QStringLiteral("&amp;"), 6),
      makeCatalogRow(EventCatalog::EventId::SeventyFiveYard, QStringLiteral("🦊 Fox"), 1),
      makeCatalogRow(EventCatalog::EventId::PenaltyStroke, QStringLiteral("Line one\nLine two"), 1),
      makeCatalogRow(EventCatalog::EventId::FiftyYard, QStringLiteral("Đặng Thị Ngọc Hân"), 4),
      makeCatalogRow(EventCatalog::EventId::Card, QStringLiteral("Jo"), 3),
      makeCatalogRow(EventCatalog::EventId::PenaltyCornerFoul,
                     QStringLiteral("Konstantin Oberhauser-Wettstein"), 2),
      makeUnlistedRow(QStringLiteral("Untitled import"), 7),
      {TagEvent::unlistedKey(QStringLiteral("Untitled import")), QStringLiteral("   "), 0},
  };

  Result result;
  result.events = inCatalogOrder(raw);
  result.interactive = result.events.size() > 1;
  return result;
}

Result oneEventResult() {
  Result result;
  result.events = {makeCatalogRow(EventCatalog::EventId::Goal, QStringLiteral("J"), 1)};
  result.interactive = true;
  return result;
}

}  // namespace PresentationSelectionBreakdown
