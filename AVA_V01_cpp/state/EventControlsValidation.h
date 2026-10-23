#pragma once

#include "EventLabelOverrides.h"

#include <QString>
#include <QVector>

#include <Qt>

/// Checks a controls edit before it is written into an EventLabelOverrides table.
///
/// The table itself still stores whatever a session file already contained,
/// including two events that share a key. The editor and the tagging-grid
/// context menu refuse to save a new conflict, an empty name, or a reserved key.
/// This does not add, remove, or reorder catalog nodes.
namespace EventControlsValidation {

enum class IssueKind {
  None = 0,
  EmptyLabel,
  NotOneLine,
  DuplicateLabel,
  DuplicateKey,
  ReservedKey,
  NotALetter,
  EmptyKey,
  UnknownEvent,
};

struct Issue {
  IssueKind kind = IssueKind::None;
  EventCatalog::EventId eventId = EventCatalog::EventId::None;
  EventCatalog::EventId otherEventId = EventCatalog::EventId::None;
  Qt::Key key = Qt::Key_unknown;
};

bool isMainEvent(EventCatalog::EventId id);

/// Override key when one is stored, otherwise the catalog key for a main event.
/// Follow-ups with no override return Qt::Key_unknown.
Qt::Key effectiveShortcut(EventCatalog::EventId id, const EventLabelOverrides::Table& table);

/// "A".."Z" or "0".."9". Empty when \p key is not one of those.
QString letterForKey(Qt::Key key);

Issue inspectLabel(const EventLabelOverrides::Table& table, EventCatalog::EventId id,
                   const QString& text);
Issue inspectShortcutText(const EventLabelOverrides::Table& table, EventCatalog::EventId id,
                          const QString& text);
/// Showing an event that is already visible is fine. Showing a hidden event is
/// refused when the name or the key would collide with a visible sibling.
Issue inspectShow(const EventLabelOverrides::Table& table, EventCatalog::EventId id);

/// Path-scoped label. Checked against the siblings of that one parent.
Issue inspectPathLabel(const EventLabelOverrides::Table& table,
                       const EventLabelOverrides::FollowUpPath& path, const QString& text);
/// Showing a path that is already visible is fine.
Issue inspectPathShow(const EventLabelOverrides::Table& table,
                      const EventLabelOverrides::FollowUpPath& path);

/// Writes the edit when inspection finds nothing wrong. \p issue may be null.
/// commitLabel and commitShown are the all-instances write: they clear path
/// exceptions for that field so every place that id appears follows the id.
bool commitLabel(EventLabelOverrides::Table* table, EventCatalog::EventId id, const QString& text,
                 Issue* issue);
bool commitShortcutText(EventLabelOverrides::Table* table, EventCatalog::EventId id,
                        const QString& text, Issue* issue);
/// \p shown false hides the event. Hiding does not ask about duplicates.
bool commitShown(EventLabelOverrides::Table* table, EventCatalog::EventId id, bool shown,
                 Issue* issue);
/// Writes a label or hidden flag for one path and leaves every other path alone.
bool commitPathLabel(EventLabelOverrides::Table* table,
                     const EventLabelOverrides::FollowUpPath& path, const QString& text,
                     Issue* issue);
bool commitPathShown(EventLabelOverrides::Table* table,
                     const EventLabelOverrides::FollowUpPath& path, bool shown, Issue* issue);

/// Duplicate or reserved keys among visible main events, in keyboard order.
QVector<Issue> reviewStoredKeys(const EventLabelOverrides::Table& table);

/// Bilingual message for \p issue. Empty when the kind is None.
QString issueMessage(const Issue& issue, const EventLabelOverrides::Table& table);

}  // namespace EventControlsValidation
