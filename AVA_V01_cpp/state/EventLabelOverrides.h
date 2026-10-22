#pragma once

#include "EventCatalog.h"

#include <QHash>
#include <QString>

#include <Qt>

/// In-memory renames for one tagging session.
///
/// Keyed by EventCatalog::EventId so a rename keeps the event's identity.
/// Stats, follow-up chains, XML codes, and .clips.json codes stay on that id.
/// Each entry may carry a label, an optional shortcut key, and a hidden flag.
/// Only the label is applied to the UI in this step. The shortcut key and the
/// hidden flag are stored for later steps. Hidden still participates in
/// validation: a hidden event is not a visible sibling.
///
/// An accepted label replaces the catalog text in both English and Spanish.
/// Empty labels (after trimming the ends) are rejected. A label is a duplicate
/// when any supported language would show the same text, ignoring case, on
/// two visible siblings. Siblings are the main events, or the children of one
/// parent. The same follow-up id is checked under every parent that lists it.
/// Main Goal and the outcome Goal are different ids and are not siblings, so
/// they may keep sharing a word.
///
/// Overrides last as long as the TagSession. TagSession::clear() drops tags
/// and does not drop overrides. Nothing here is written to QSettings.
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

  /// Stores a shortcut for a later step. Qt::Key_unknown clears it. Duplicate
  /// keys are not rejected yet, and GameControls does not read this field.
  Result setShortcutKey(EventCatalog::EventId id, Qt::Key key);
  Result clearShortcutKey(EventCatalog::EventId id);

  /// Stores the hidden flag. setHidden(false) is rejected when showing the
  /// event would duplicate a visible sibling's label. Buttons are not hidden
  /// by this flag yet.
  Result setHidden(EventCatalog::EventId id, bool hidden);

  void clear();

  bool hasLabel(EventCatalog::EventId id) const;
  /// Empty when this id has no label override.
  QString label(EventCatalog::EventId id) const;
  bool hasShortcutKey(EventCatalog::EventId id) const;
  /// Qt::Key_unknown when this id has no shortcut override.
  Qt::Key shortcutKey(EventCatalog::EventId id) const;
  bool hidden(EventCatalog::EventId id) const;

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

  Table withLabel(EventCatalog::EventId id, const QString& trimmedLabel) const;
  Table withHidden(EventCatalog::EventId id, bool hidden) const;
  void writeEntry(EventCatalog::EventId id, const Entry& entry);

  QHash<int, Entry> entries_;
};

}  // namespace EventLabelOverrides
