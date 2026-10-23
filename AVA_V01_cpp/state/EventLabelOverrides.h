#pragma once

#include "EventCatalog.h"

#include <QHash>
#include <QString>
#include <QVector>

#include <Qt>

#include <array>

/// In-memory renames for one tagging session.
///
/// Keyed by EventCatalog::EventId so a rename keeps the event's identity.
/// Stats grouping, follow-up chains, XML codes, and .clips.json codes stay on
/// that id. Each id entry may carry a label, an optional shortcut key, and a
/// hidden flag. An id-level label or hidden flag applies to every place that
/// id appears. GameControls applies all three. A hidden event stays in the
/// button grid and does not tag. Hidden still participates in validation: a
/// hidden event is not a visible sibling.
///
/// A follow-up that appears under more than one main button can also store a
/// label or a hidden flag on one path (the main event plus the follow-up
/// chain). That record changes only that path. Shortcuts stay on the id.
/// Display and visibility use the path record when the chain is known, and
/// the id record otherwise.
///
/// An accepted label replaces the catalog text in both English and Spanish.
/// Empty labels (after trimming the ends) are rejected. A label is a duplicate
/// when any supported language would show the same text, ignoring case, on
/// two visible siblings. An id-level label is checked under every parent that
/// lists the id. A path label is checked only against the children of the
/// parent on that path. Main Goal and the outcome Goal are different ids and
/// are not siblings, so they may keep sharing a word.
///
/// Overrides last as long as the TagSession. TagSession::clear() drops tags
/// and does not drop overrides. The match session file and control templates
/// persist this table. Nothing here is written to QSettings.
namespace EventLabelOverrides {

enum class RejectReason {
  None = 0,
  UnknownEvent,
  EmptyLabel,
  DuplicateLabel,
};

struct Result {
  bool accepted = false;
  RejectReason reason = RejectReason::UnknownEvent;
  /// The other visible sibling that already shows this label. None otherwise.
  EventCatalog::EventId conflictingEventId = EventCatalog::EventId::None;
};

/// One stored override. Empty label, no shortcut, and hidden == false is not
/// an entry. Follow-up edits use the same record on a follow-up id when the
/// edit applies to every instance of that id.
struct Record {
  EventCatalog::EventId id = EventCatalog::EventId::None;
  QString label;
  bool hasShortcutKey = false;
  Qt::Key shortcutKey = Qt::Key_unknown;
  bool hidden = false;
};

/// One occurrence of a follow-up: the main event and the chain that ends on it.
struct FollowUpPath {
  EventCatalog::EventId mainEventId = EventCatalog::EventId::None;
  std::array<EventCatalog::EventId, EventCatalog::kMaxFollowUpDepth> steps{};
  int stepCount = 0;

  EventCatalog::EventId targetId() const {
    if (stepCount <= 0 || stepCount > EventCatalog::kMaxFollowUpDepth) {
      return EventCatalog::EventId::None;
    }
    return steps[static_cast<std::size_t>(stepCount - 1)];
  }

  bool isEmpty() const { return stepCount <= 0; }

  /// Stable key for this path. Empty paths are not stored.
  QString key() const;

  bool operator==(const FollowUpPath& other) const;
  bool operator!=(const FollowUpPath& other) const { return !(*this == other); }
};

/// A label or hidden flag for one FollowUpPath. Shortcuts are not path-scoped.
/// hasHidden distinguishes "hidden is false" from "no path hidden override".
struct PathRecord {
  FollowUpPath path;
  QString label;
  bool hasHidden = false;
  bool hidden = false;
};

/// True when each step is a child of the previous node, starting at a main event.
bool followUpPathExists(const FollowUpPath& path);

/// Every catalog chain that ends on \p id, in button-grid order, then child order.
QVector<FollowUpPath> catalogPathsEndingAt(EventCatalog::EventId id);

/// True when \p id ends a chain under at least two different main events.
/// A follow-up that only repeats inside one main button's tree is false.
bool sharedAcrossMainEvents(EventCatalog::EventId id);

class Table {
 public:
  /// Stores the trimmed label. Rejects an unknown id, an empty label, and a
  /// duplicate among visible siblings. A hidden event may reuse a visible
  /// sibling's label; showing it again is checked by setHidden().
  Result setLabel(EventCatalog::EventId id, const QString& label);
  /// Drops the label override. The catalog text is shown again. Unknown ids
  /// are rejected. Clearing a label that was not set is accepted and changes
  /// nothing.
  Result clearLabel(EventCatalog::EventId id);

  /// Stores a shortcut. Qt::Key_unknown clears it. Duplicate keys are kept so an
  /// older session file still loads. The editor refuses to save a new conflict.
  /// GameControls gives the key to the first visible button in keyboard order.
  Result setShortcutKey(EventCatalog::EventId id, Qt::Key key);
  Result clearShortcutKey(EventCatalog::EventId id);

  /// Stores the hidden flag. setHidden(false) is rejected when showing the
  /// event would duplicate a visible sibling's label. GameControls leaves the
  /// button in the layout and suppresses tagging while the flag is set.
  Result setHidden(EventCatalog::EventId id, bool hidden);

  void clear();

  /// Entries in ascending catalog-id order.
  QVector<Record> records() const;
  /// Replaces the id records and drops path records. Unknown ids, duplicate
  /// ids, and a label that would show twice on visible siblings are rejected
  /// and the table is left as it was. An empty \p records clears the table.
  bool replaceRecords(const QVector<Record>& records);
  /// Replaces id records and path records together. A path that is not in the
  /// catalog, a repeated path, or a label that collides on that path is
  /// rejected and the table is left as it was.
  bool replaceAll(const QVector<Record>& records, const QVector<PathRecord>& pathRecords);

  /// Stores a label for one path. Other paths and the id label stay as they are.
  Result setPathLabel(const FollowUpPath& path, const QString& label);
  Result clearPathLabel(const FollowUpPath& path);
  /// Stores a hidden flag for one path. When \p hidden matches the id flag,
  /// the path exception is removed.
  Result setPathHidden(const FollowUpPath& path, bool hidden);
  Result clearPathHidden(const FollowUpPath& path);
  /// Drops path label exceptions for every chain that ends on \p id.
  void clearPathLabelsFor(EventCatalog::EventId id);
  /// Drops path hidden exceptions for every chain that ends on \p id.
  void clearPathHiddenFor(EventCatalog::EventId id);

  bool hasLabel(EventCatalog::EventId id) const;
  /// Empty when this id has no label override.
  QString label(EventCatalog::EventId id) const;
  bool hasShortcutKey(EventCatalog::EventId id) const;
  /// Qt::Key_unknown when this id has no shortcut override.
  Qt::Key shortcutKey(EventCatalog::EventId id) const;
  bool hidden(EventCatalog::EventId id) const;

  bool hasPathLabel(const FollowUpPath& path) const;
  /// Empty when this path has no label override.
  QString pathLabel(const FollowUpPath& path) const;
  bool hasPathHidden(const FollowUpPath& path) const;
  /// The path flag when hasPathHidden is true, otherwise false.
  bool pathHiddenFlag(const FollowUpPath& path) const;
  /// Path hidden flag when one is stored, otherwise hidden() of the target id.
  bool hiddenOnPath(const FollowUpPath& path) const;

  /// Path records in ascending main-event id, then chain order.
  QVector<PathRecord> pathRecords() const;

  bool operator==(const Table& other) const;
  bool operator!=(const Table& other) const { return !(*this == other); }

 private:
  struct Entry {
    QString label;
    bool hasShortcutKey = false;
    Qt::Key shortcutKey = Qt::Key_unknown;
    bool hidden = false;

    bool isEmpty() const { return label.isEmpty() && !hasShortcutKey && !hidden; }
  };

  struct PathEntry {
    FollowUpPath path;
    QString label;
    bool hasHidden = false;
    bool hidden = false;

    bool isEmpty() const { return label.isEmpty() && !hasHidden; }
  };

  Table withLabel(EventCatalog::EventId id, const QString& trimmedLabel) const;
  Table withHidden(EventCatalog::EventId id, bool hidden) const;
  void writeEntry(EventCatalog::EventId id, const Entry& entry);
  void writePath(const FollowUpPath& path, const PathEntry& entry);

  QHash<int, Entry> entries_;
  QHash<QString, PathEntry> pathEntries_;
};

}  // namespace EventLabelOverrides
