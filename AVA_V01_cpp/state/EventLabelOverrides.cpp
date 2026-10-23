#include "EventLabelOverrides.h"

#include "EventLabelResolver.h"

#include <QSet>
#include <QVector>

#include <algorithm>

namespace EventLabelOverrides {
namespace {

Result acceptedResult() {
  Result result;
  result.accepted = true;
  result.reason = RejectReason::None;
  return result;
}

Result rejected(RejectReason reason,
                EventCatalog::EventId conflictingEventId = EventCatalog::EventId::None) {
  Result result;
  result.accepted = false;
  result.reason = reason;
  result.conflictingEventId = conflictingEventId;
  return result;
}

bool isCatalogId(EventCatalog::EventId id) { return EventCatalog::nodeForId(id) != nullptr; }

struct SiblingGroup {
  QVector<EventCatalog::EventId> ids;
};

QVector<SiblingGroup> siblingGroups(EventCatalog::EventId id) {
  QVector<SiblingGroup> groups;
  const EventCatalog::Node* subject = EventCatalog::nodeForId(id);
  if (!subject) return groups;

  if (subject->mainEvent) {
    SiblingGroup mainEvents;
    for (int index = 0; index < EventCatalog::nodeCount(); ++index) {
      const EventCatalog::Node& node = EventCatalog::nodeAt(index);
      if (!node.mainEvent) continue;
      mainEvents.ids.append(node.id);
    }
    groups.append(mainEvents);
    return groups;
  }

  for (int index = 0; index < EventCatalog::nodeCount(); ++index) {
    const EventCatalog::Node& parent = EventCatalog::nodeAt(index);
    bool listsSubject = false;
    SiblingGroup children;
    for (int childIndex = 0; childIndex < parent.childCount; ++childIndex) {
      const EventCatalog::EventId childId = parent.children[static_cast<std::size_t>(childIndex)];
      children.ids.append(childId);
      if (childId == id) listsSubject = true;
    }
    if (listsSubject) groups.append(children);
  }
  return groups;
}

bool sameVisibleLabel(const QString& left, const QString& right) {
  return !left.isEmpty() && left.compare(right, Qt::CaseInsensitive) == 0;
}

/// The label EventLabelResolver would show for \p id with \p overrides.
Result visibleLabelCollision(EventCatalog::EventId subject, const Table& overrides) {
  if (overrides.hidden(subject)) return acceptedResult();

  const QVector<SiblingGroup> groups = siblingGroups(subject);
  for (const SiblingGroup& group : groups) {
    for (const EventCatalog::EventId otherId : group.ids) {
      if (otherId == subject || overrides.hidden(otherId)) continue;
      const QString englishSubject =
          EventLabelResolver::labelForId(subject, overrides, AppLocale::Language::English);
      const QString englishOther =
          EventLabelResolver::labelForId(otherId, overrides, AppLocale::Language::English);
      if (sameVisibleLabel(englishSubject, englishOther)) {
        return rejected(RejectReason::DuplicateLabel, otherId);
      }
      const QString spanishSubject =
          EventLabelResolver::labelForId(subject, overrides, AppLocale::Language::Spanish);
      const QString spanishOther =
          EventLabelResolver::labelForId(otherId, overrides, AppLocale::Language::Spanish);
      if (sameVisibleLabel(spanishSubject, spanishOther)) {
        return rejected(RejectReason::DuplicateLabel, otherId);
      }
    }
  }
  return acceptedResult();
}

}  // namespace

Table Table::withLabel(EventCatalog::EventId id, const QString& trimmedLabel) const {
  Table updated = *this;
  const int key = static_cast<int>(id);
  Entry entry = updated.entries_.value(key);
  entry.label = trimmedLabel;
  updated.entries_.insert(key, entry);
  return updated;
}

Table Table::withHidden(EventCatalog::EventId id, bool hidden) const {
  Table updated = *this;
  const int key = static_cast<int>(id);
  Entry entry = updated.entries_.value(key);
  entry.hidden = hidden;
  if (entry.isEmpty()) updated.entries_.remove(key);
  else updated.entries_.insert(key, entry);
  return updated;
}

void Table::writeEntry(EventCatalog::EventId id, const Entry& entry) {
  const int key = static_cast<int>(id);
  if (entry.isEmpty()) entries_.remove(key);
  else entries_.insert(key, entry);
}

Result Table::setLabel(EventCatalog::EventId id, const QString& label) {
  if (!isCatalogId(id)) return rejected(RejectReason::UnknownEvent);
  const QString trimmed = label.trimmed();
  if (trimmed.isEmpty()) return rejected(RejectReason::EmptyLabel);
  if (hasLabel(id) && this->label(id) == trimmed) return acceptedResult();

  const Table prospective = withLabel(id, trimmed);
  if (!prospective.hidden(id)) {
    const Result collision = visibleLabelCollision(id, prospective);
    if (!collision.accepted) return collision;
  }
  entries_ = prospective.entries_;
  return acceptedResult();
}

Result Table::clearLabel(EventCatalog::EventId id) {
  if (!isCatalogId(id)) return rejected(RejectReason::UnknownEvent);
  if (!hasLabel(id)) return acceptedResult();
  Entry entry = entries_.value(static_cast<int>(id));
  entry.label.clear();
  writeEntry(id, entry);
  return acceptedResult();
}

Result Table::setShortcutKey(EventCatalog::EventId id, Qt::Key key) {
  if (!isCatalogId(id)) return rejected(RejectReason::UnknownEvent);
  if (key == Qt::Key_unknown) return clearShortcutKey(id);
  if (hasShortcutKey(id) && shortcutKey(id) == key) return acceptedResult();

  Entry entry = entries_.value(static_cast<int>(id));
  entry.hasShortcutKey = true;
  entry.shortcutKey = key;
  writeEntry(id, entry);
  return acceptedResult();
}

Result Table::clearShortcutKey(EventCatalog::EventId id) {
  if (!isCatalogId(id)) return rejected(RejectReason::UnknownEvent);
  if (!hasShortcutKey(id)) return acceptedResult();
  Entry entry = entries_.value(static_cast<int>(id));
  entry.hasShortcutKey = false;
  entry.shortcutKey = Qt::Key_unknown;
  writeEntry(id, entry);
  return acceptedResult();
}

Result Table::setHidden(EventCatalog::EventId id, bool hidden) {
  if (!isCatalogId(id)) return rejected(RejectReason::UnknownEvent);
  if (this->hidden(id) == hidden) return acceptedResult();

  const Table prospective = withHidden(id, hidden);
  if (!hidden) {
    const Result collision = visibleLabelCollision(id, prospective);
    if (!collision.accepted) return collision;
  }
  entries_ = prospective.entries_;
  return acceptedResult();
}

void Table::clear() { entries_.clear(); }

QVector<Record> Table::records() const {
  QVector<int> keys;
  keys.reserve(entries_.size());
  for (auto iterator = entries_.cbegin(); iterator != entries_.cend(); ++iterator) {
    keys.append(iterator.key());
  }
  std::sort(keys.begin(), keys.end());

  QVector<Record> stored;
  stored.reserve(keys.size());
  for (const int key : keys) {
    const auto iterator = entries_.constFind(key);
    if (iterator == entries_.cend()) continue;
    Record record;
    record.id = static_cast<EventCatalog::EventId>(key);
    record.label = iterator->label;
    record.hasShortcutKey = iterator->hasShortcutKey;
    record.shortcutKey = iterator->shortcutKey;
    record.hidden = iterator->hidden;
    stored.append(record);
  }
  return stored;
}

bool Table::replaceRecords(const QVector<Record>& records) {
  Table prospective;
  QSet<int> seenIds;
  seenIds.reserve(records.size());
  for (const Record& record : records) {
    if (!isCatalogId(record.id)) return false;
    const int key = static_cast<int>(record.id);
    if (seenIds.contains(key)) return false;
    seenIds.insert(key);

    Entry entry;
    entry.label = record.label.trimmed();
    if (record.hasShortcutKey && record.shortcutKey != Qt::Key_unknown) {
      entry.hasShortcutKey = true;
      entry.shortcutKey = record.shortcutKey;
    }
    entry.hidden = record.hidden;
    if (entry.isEmpty()) continue;
    prospective.entries_.insert(key, entry);
  }

  const QVector<int> keys = prospective.entries_.keys();
  for (const int key : keys) {
    const auto id = static_cast<EventCatalog::EventId>(key);
    if (prospective.hidden(id)) continue;
    const Result collision = visibleLabelCollision(id, prospective);
    if (!collision.accepted) return false;
  }

  entries_ = prospective.entries_;
  return true;
}

bool Table::hasLabel(EventCatalog::EventId id) const { return !label(id).isEmpty(); }

QString Table::label(EventCatalog::EventId id) const {
  const auto iterator = entries_.constFind(static_cast<int>(id));
  if (iterator == entries_.cend()) return {};
  return iterator->label;
}

bool Table::hasShortcutKey(EventCatalog::EventId id) const {
  const auto iterator = entries_.constFind(static_cast<int>(id));
  if (iterator == entries_.cend()) return false;
  return iterator->hasShortcutKey;
}

Qt::Key Table::shortcutKey(EventCatalog::EventId id) const {
  const auto iterator = entries_.constFind(static_cast<int>(id));
  if (iterator == entries_.cend() || !iterator->hasShortcutKey) return Qt::Key_unknown;
  return iterator->shortcutKey;
}

bool Table::hidden(EventCatalog::EventId id) const {
  const auto iterator = entries_.constFind(static_cast<int>(id));
  if (iterator == entries_.cend()) return false;
  return iterator->hidden;
}

bool Table::operator==(const Table& other) const {
  if (entries_.size() != other.entries_.size()) return false;
  for (auto iterator = entries_.cbegin(); iterator != entries_.cend(); ++iterator) {
    const auto otherIterator = other.entries_.constFind(iterator.key());
    if (otherIterator == other.entries_.cend()) return false;
    if (iterator->label != otherIterator->label) return false;
    if (iterator->hasShortcutKey != otherIterator->hasShortcutKey) return false;
    if (iterator->hasShortcutKey && iterator->shortcutKey != otherIterator->shortcutKey) {
      return false;
    }
    if (iterator->hidden != otherIterator->hidden) return false;
  }
  return true;
}

}  // namespace EventLabelOverrides
