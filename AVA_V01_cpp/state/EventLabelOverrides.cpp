#include "EventLabelOverrides.h"

#include "EventLabelResolver.h"

#include <QSet>
#include <QStringList>
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

int clampedStepCount(const FollowUpPath& path) {
  if (path.stepCount <= 0) return 0;
  if (path.stepCount > EventCatalog::kMaxFollowUpDepth) return EventCatalog::kMaxFollowUpDepth;
  return path.stepCount;
}

bool pathExists(const FollowUpPath& path) {
  const EventCatalog::Node* node = EventCatalog::nodeForId(path.mainEventId);
  if (node == nullptr || !node->mainEvent) return false;
  const int count = path.stepCount;
  if (count <= 0 || count > EventCatalog::kMaxFollowUpDepth) return false;
  for (int index = 0; index < count; ++index) {
    const EventCatalog::EventId step = path.steps[static_cast<std::size_t>(index)];
    bool found = false;
    for (int childIndex = 0; childIndex < node->childCount; ++childIndex) {
      if (node->children[static_cast<std::size_t>(childIndex)] != step) continue;
      found = true;
      break;
    }
    if (!found) return false;
    node = EventCatalog::nodeForId(step);
    if (node == nullptr) return false;
  }
  return true;
}

const EventCatalog::Node* parentNode(const FollowUpPath& path) {
  if (!pathExists(path)) return nullptr;
  if (path.stepCount <= 1) return EventCatalog::nodeForId(path.mainEventId);
  return EventCatalog::nodeForId(path.steps[static_cast<std::size_t>(path.stepCount - 2)]);
}

void collectPaths(const EventCatalog::Node& node, FollowUpPath prefix,
                  EventCatalog::EventId target, QVector<FollowUpPath>* found) {
  for (int childIndex = 0; childIndex < node.childCount; ++childIndex) {
    if (prefix.stepCount >= EventCatalog::kMaxFollowUpDepth) return;
    const EventCatalog::EventId childId = node.children[static_cast<std::size_t>(childIndex)];
    FollowUpPath next = prefix;
    next.steps[static_cast<std::size_t>(next.stepCount)] = childId;
    ++next.stepCount;
    if (childId == target) found->append(next);
    const EventCatalog::Node* child = EventCatalog::nodeForId(childId);
    if (child != nullptr && child->childCount > 0) collectPaths(*child, next, target, found);
  }
}

QString displayFor(const Table& table, const FollowUpPath& path, bool spanish) {
  if (table.hasPathLabel(path)) return table.pathLabel(path);
  const EventCatalog::EventId id = path.targetId();
  if (table.hasLabel(id)) return table.label(id);
  return EventCatalog::labelForId(id, spanish);
}

FollowUpPath siblingPath(const FollowUpPath& path, EventCatalog::EventId siblingId) {
  FollowUpPath sibling = path;
  if (sibling.stepCount > 0 && sibling.stepCount <= EventCatalog::kMaxFollowUpDepth) {
    sibling.steps[static_cast<std::size_t>(sibling.stepCount - 1)] = siblingId;
  }
  return sibling;
}

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

Result pathLabelCollision(const FollowUpPath& subject, const Table& overrides) {
  if (overrides.hiddenOnPath(subject)) return acceptedResult();
  const EventCatalog::Node* parent = parentNode(subject);
  if (parent == nullptr) return rejected(RejectReason::UnknownEvent);
  const EventCatalog::EventId subjectId = subject.targetId();
  for (int childIndex = 0; childIndex < parent->childCount; ++childIndex) {
    const EventCatalog::EventId otherId = parent->children[static_cast<std::size_t>(childIndex)];
    if (otherId == subjectId) continue;
    const FollowUpPath otherPath = siblingPath(subject, otherId);
    if (overrides.hiddenOnPath(otherPath)) continue;
    if (sameVisibleLabel(displayFor(overrides, subject, false), displayFor(overrides, otherPath, false))) {
      return rejected(RejectReason::DuplicateLabel, otherId);
    }
    if (sameVisibleLabel(displayFor(overrides, subject, true), displayFor(overrides, otherPath, true))) {
      return rejected(RejectReason::DuplicateLabel, otherId);
    }
  }
  return acceptedResult();
}

}  // namespace

QString FollowUpPath::key() const {
  QStringList parts;
  parts.append(QString::number(static_cast<int>(mainEventId)));
  const int count = clampedStepCount(*this);
  for (int index = 0; index < count; ++index) {
    parts.append(QString::number(static_cast<int>(steps[static_cast<std::size_t>(index)])));
  }
  return parts.join(QLatin1Char(':'));
}

bool FollowUpPath::operator==(const FollowUpPath& other) const {
  if (mainEventId != other.mainEventId || stepCount != other.stepCount) return false;
  const int count = clampedStepCount(*this);
  for (int index = 0; index < count; ++index) {
    if (steps[static_cast<std::size_t>(index)] != other.steps[static_cast<std::size_t>(index)]) {
      return false;
    }
  }
  return true;
}

bool followUpPathExists(const FollowUpPath& path) { return pathExists(path); }

QVector<FollowUpPath> catalogPathsEndingAt(EventCatalog::EventId id) {
  QVector<FollowUpPath> found;
  const EventCatalog::Node* target = EventCatalog::nodeForId(id);
  if (target == nullptr || target->mainEvent) return found;
  for (int index = 0; index < EventCatalog::buttonGridCount(); ++index) {
    const EventCatalog::Node& mainEvent = EventCatalog::buttonGridNodeAt(index);
    FollowUpPath prefix;
    prefix.mainEventId = mainEvent.id;
    collectPaths(mainEvent, prefix, id, &found);
  }
  return found;
}

bool sharedAcrossMainEvents(EventCatalog::EventId id) {
  int seenMain = -1;
  const QVector<FollowUpPath> paths = catalogPathsEndingAt(id);
  for (const FollowUpPath& path : paths) {
    const int mainId = static_cast<int>(path.mainEventId);
    if (seenMain < 0) {
      seenMain = mainId;
      continue;
    }
    if (seenMain != mainId) return true;
  }
  return false;
}

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

void Table::writePath(const FollowUpPath& path, const PathEntry& entry) {
  const QString key = path.key();
  if (entry.isEmpty()) {
    pathEntries_.remove(key);
    return;
  }
  PathEntry stored = entry;
  stored.path = path;
  pathEntries_.insert(key, stored);
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

void Table::clear() {
  entries_.clear();
  pathEntries_.clear();
}

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

bool Table::replaceRecords(const QVector<Record>& records) { return replaceAll(records, {}); }

bool Table::replaceAll(const QVector<Record>& records, const QVector<PathRecord>& pathRecords) {
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

  QSet<QString> seenPaths;
  seenPaths.reserve(pathRecords.size());
  for (const PathRecord& record : pathRecords) {
    if (!pathExists(record.path)) return false;
    const QString key = record.path.key();
    if (seenPaths.contains(key)) return false;
    seenPaths.insert(key);

    PathEntry entry;
    entry.path = record.path;
    entry.label = record.label.trimmed();
    if (record.hasHidden) {
      entry.hasHidden = true;
      entry.hidden = record.hidden;
    }
    if (entry.isEmpty()) continue;
    prospective.pathEntries_.insert(key, entry);
  }

  const QVector<int> keys = prospective.entries_.keys();
  for (const int key : keys) {
    const auto id = static_cast<EventCatalog::EventId>(key);
    if (prospective.hidden(id)) continue;
    const Result collision = visibleLabelCollision(id, prospective);
    if (!collision.accepted) return false;
  }
  for (auto iterator = prospective.pathEntries_.cbegin(); iterator != prospective.pathEntries_.cend();
       ++iterator) {
    if (prospective.hiddenOnPath(iterator->path)) continue;
    const Result collision = pathLabelCollision(iterator->path, prospective);
    if (!collision.accepted) return false;
  }

  entries_ = prospective.entries_;
  pathEntries_ = prospective.pathEntries_;
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

Result Table::setPathLabel(const FollowUpPath& path, const QString& label) {
  if (!pathExists(path)) return rejected(RejectReason::UnknownEvent);
  const QString trimmed = label.trimmed();
  if (trimmed.isEmpty()) return rejected(RejectReason::EmptyLabel);
  if (hasPathLabel(path) && pathLabel(path) == trimmed) return acceptedResult();

  const bool sameAsId = hasLabel(path.targetId()) && this->label(path.targetId()) == trimmed;
  const QString english = EventCatalog::labelForId(path.targetId(), false);
  const QString spanish = EventCatalog::labelForId(path.targetId(), true);
  const bool sameAsCatalog = !hasLabel(path.targetId()) && trimmed == english && trimmed == spanish;
  if (sameAsId || sameAsCatalog) return clearPathLabel(path);

  Table prospective = *this;
  PathEntry entry = prospective.pathEntries_.value(path.key());
  entry.path = path;
  entry.label = trimmed;
  prospective.writePath(path, entry);
  if (!prospective.hiddenOnPath(path)) {
    const Result collision = pathLabelCollision(path, prospective);
    if (!collision.accepted) return collision;
  }
  pathEntries_ = prospective.pathEntries_;
  return acceptedResult();
}

Result Table::clearPathLabel(const FollowUpPath& path) {
  if (!pathExists(path)) return rejected(RejectReason::UnknownEvent);
  if (!hasPathLabel(path)) return acceptedResult();
  PathEntry entry = pathEntries_.value(path.key());
  entry.label.clear();
  writePath(path, entry);
  return acceptedResult();
}

Result Table::setPathHidden(const FollowUpPath& path, bool hidden) {
  if (!pathExists(path)) return rejected(RejectReason::UnknownEvent);
  if (hidden == this->hidden(path.targetId())) return clearPathHidden(path);
  if (hasPathHidden(path) && pathHiddenFlag(path) == hidden) return acceptedResult();

  Table prospective = *this;
  PathEntry entry = prospective.pathEntries_.value(path.key());
  entry.path = path;
  entry.hasHidden = true;
  entry.hidden = hidden;
  prospective.writePath(path, entry);
  if (!hidden) {
    const Result collision = pathLabelCollision(path, prospective);
    if (!collision.accepted) return collision;
  }
  pathEntries_ = prospective.pathEntries_;
  return acceptedResult();
}

Result Table::clearPathHidden(const FollowUpPath& path) {
  if (!pathExists(path)) return rejected(RejectReason::UnknownEvent);
  if (!hasPathHidden(path)) return acceptedResult();
  PathEntry entry = pathEntries_.value(path.key());
  entry.hasHidden = false;
  entry.hidden = false;
  writePath(path, entry);
  return acceptedResult();
}

void Table::clearPathLabelsFor(EventCatalog::EventId id) {
  const QList<QString> keys = pathEntries_.keys();
  for (const QString& key : keys) {
    const auto iterator = pathEntries_.constFind(key);
    if (iterator == pathEntries_.cend() || iterator->path.targetId() != id) continue;
    PathEntry entry = iterator.value();
    entry.label.clear();
    writePath(entry.path, entry);
  }
}

void Table::clearPathHiddenFor(EventCatalog::EventId id) {
  const QList<QString> keys = pathEntries_.keys();
  for (const QString& key : keys) {
    const auto iterator = pathEntries_.constFind(key);
    if (iterator == pathEntries_.cend() || iterator->path.targetId() != id) continue;
    PathEntry entry = iterator.value();
    entry.hasHidden = false;
    entry.hidden = false;
    writePath(entry.path, entry);
  }
}

bool Table::hasPathLabel(const FollowUpPath& path) const { return !pathLabel(path).isEmpty(); }

QString Table::pathLabel(const FollowUpPath& path) const {
  const auto iterator = pathEntries_.constFind(path.key());
  if (iterator == pathEntries_.cend()) return {};
  return iterator->label;
}

bool Table::hasPathHidden(const FollowUpPath& path) const {
  const auto iterator = pathEntries_.constFind(path.key());
  if (iterator == pathEntries_.cend()) return false;
  return iterator->hasHidden;
}

bool Table::pathHiddenFlag(const FollowUpPath& path) const {
  const auto iterator = pathEntries_.constFind(path.key());
  if (iterator == pathEntries_.cend() || !iterator->hasHidden) return false;
  return iterator->hidden;
}

bool Table::hiddenOnPath(const FollowUpPath& path) const {
  if (hasPathHidden(path)) return pathHiddenFlag(path);
  if (path.stepCount <= 0) return hidden(path.mainEventId);
  return hidden(path.targetId());
}

QVector<PathRecord> Table::pathRecords() const {
  QVector<PathRecord> stored;
  stored.reserve(pathEntries_.size());
  for (auto iterator = pathEntries_.cbegin(); iterator != pathEntries_.cend(); ++iterator) {
    PathRecord record;
    record.path = iterator->path;
    record.label = iterator->label;
    record.hasHidden = iterator->hasHidden;
    record.hidden = iterator->hidden;
    stored.append(record);
  }
  std::sort(stored.begin(), stored.end(), [](const PathRecord& left, const PathRecord& right) {
    if (left.path.mainEventId != right.path.mainEventId) {
      return static_cast<int>(left.path.mainEventId) < static_cast<int>(right.path.mainEventId);
    }
    const int leftCount = left.path.stepCount < right.path.stepCount ? left.path.stepCount
                                                                      : right.path.stepCount;
    const int compared = leftCount < 0 ? 0 : leftCount;
    for (int index = 0; index < compared; ++index) {
      const int leftId = static_cast<int>(left.path.steps[static_cast<std::size_t>(index)]);
      const int rightId = static_cast<int>(right.path.steps[static_cast<std::size_t>(index)]);
      if (leftId != rightId) return leftId < rightId;
    }
    return left.path.stepCount < right.path.stepCount;
  });
  return stored;
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
  if (pathEntries_.size() != other.pathEntries_.size()) return false;
  for (auto iterator = pathEntries_.cbegin(); iterator != pathEntries_.cend(); ++iterator) {
    const auto otherIterator = other.pathEntries_.constFind(iterator.key());
    if (otherIterator == other.pathEntries_.cend()) return false;
    if (iterator->label != otherIterator->label) return false;
    if (iterator->hasHidden != otherIterator->hasHidden) return false;
    if (iterator->hasHidden && iterator->hidden != otherIterator->hidden) return false;
  }
  return true;
}

}  // namespace EventLabelOverrides
