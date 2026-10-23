#include "EventControlsValidation.h"

#include "EventCatalog.h"
#include "EventLabelResolver.h"

#include "../i18n/AppLocale.h"

namespace EventControlsValidation {
namespace {

bool labelHasLineBreak(const QString& text) {
  for (const QChar character : text) {
    if (character == QLatin1Char('\n') || character == QLatin1Char('\r') ||
        character == QLatin1Char('\t') || character.category() == QChar::Separator_Line ||
        character.category() == QChar::Separator_Paragraph) {
      return true;
    }
  }
  return false;
}

bool revertsToCatalogLabel(EventCatalog::EventId id, const QString& trimmed) {
  const bool spanish = AppLocale::currentLanguage() == AppLocale::Language::Spanish;
  return trimmed == EventCatalog::labelForId(id, spanish);
}

bool revertsToPathLabel(const EventLabelOverrides::Table& table,
                        const EventLabelOverrides::FollowUpPath& path, const QString& trimmed) {
  EventLabelOverrides::Table without = table;
  without.clearPathLabel(path);
  return trimmed == EventLabelResolver::labelForPath(path, without);
}

Issue issueFromResult(EventCatalog::EventId id, const EventLabelOverrides::Result& result) {
  Issue issue;
  issue.eventId = id;
  issue.otherEventId = result.conflictingEventId;
  switch (result.reason) {
    case EventLabelOverrides::RejectReason::None:
      issue.kind = IssueKind::None;
      break;
    case EventLabelOverrides::RejectReason::UnknownEvent:
      issue.kind = IssueKind::UnknownEvent;
      break;
    case EventLabelOverrides::RejectReason::EmptyLabel:
      issue.kind = IssueKind::EmptyLabel;
      break;
    case EventLabelOverrides::RejectReason::DuplicateLabel:
      issue.kind = IssueKind::DuplicateLabel;
      break;
  }
  return issue;
}

Issue noneIssue(EventCatalog::EventId id, Qt::Key key = Qt::Key_unknown) {
  Issue issue;
  issue.kind = IssueKind::None;
  issue.eventId = id;
  issue.key = key;
  return issue;
}

bool isReservedKey(Qt::Key key) {
  if (key == Qt::Key_G || key == Qt::Key_H) return true;
  return key >= Qt::Key_0 && key <= Qt::Key_9;
}

Issue parseShortcutText(EventCatalog::EventId id, const QString& text, Qt::Key* key) {
  const QString trimmed = text.trimmed();
  if (trimmed.isEmpty()) {
    *key = Qt::Key_unknown;
    Issue issue;
    issue.kind = IssueKind::EmptyKey;
    issue.eventId = id;
    return issue;
  }
  if (trimmed.size() != 1) {
    *key = Qt::Key_unknown;
    Issue issue;
    issue.kind = IssueKind::NotALetter;
    issue.eventId = id;
    return issue;
  }

  const QChar character = trimmed.at(0);
  if (character.isDigit()) {
    *key = static_cast<Qt::Key>(Qt::Key_0 + character.digitValue());
    Issue issue;
    issue.kind = IssueKind::ReservedKey;
    issue.eventId = id;
    issue.key = *key;
    return issue;
  }

  const QChar upper = character.toUpper();
  if (upper < QLatin1Char('A') || upper > QLatin1Char('Z')) {
    *key = Qt::Key_unknown;
    Issue issue;
    issue.kind = IssueKind::NotALetter;
    issue.eventId = id;
    return issue;
  }
  *key = static_cast<Qt::Key>(Qt::Key_A + (upper.unicode() - QChar(u'A').unicode()));
  if (isReservedKey(*key)) {
    Issue issue;
    issue.kind = IssueKind::ReservedKey;
    issue.eventId = id;
    issue.key = *key;
    return issue;
  }
  return noneIssue(id, *key);
}

Issue duplicateKeyIssue(const EventLabelOverrides::Table& table, EventCatalog::EventId id,
                        Qt::Key key) {
  if (!isMainEvent(id) || key == Qt::Key_unknown) return noneIssue(id, key);
  for (int index = 0; index < EventCatalog::buttonGridCount(); ++index) {
    const EventCatalog::EventId otherId = EventCatalog::buttonGridNodeAt(index).id;
    if (otherId == id || table.hidden(otherId)) continue;
    if (effectiveShortcut(otherId, table) != key) continue;
    Issue issue;
    issue.kind = IssueKind::DuplicateKey;
    issue.eventId = id;
    issue.otherEventId = otherId;
    issue.key = key;
    return issue;
  }
  return noneIssue(id, key);
}

QString reservedKeyReason(Qt::Key key) {
  if (key == Qt::Key_G || key == Qt::Key_H) {
    return AppLocale::trUi("controls_editor.reserved_quarter");
  }
  return AppLocale::trUi("controls_editor.reserved_follow_up");
}

}  // namespace

bool isMainEvent(EventCatalog::EventId id) {
  const EventCatalog::Node* node = EventCatalog::nodeForId(id);
  return node != nullptr && node->mainEvent;
}

Qt::Key effectiveShortcut(EventCatalog::EventId id, const EventLabelOverrides::Table& table) {
  if (table.hasShortcutKey(id)) return table.shortcutKey(id);
  if (!isMainEvent(id)) return Qt::Key_unknown;
  return EventCatalog::shortcutKey(id);
}

QString letterForKey(Qt::Key key) {
  if (key >= Qt::Key_A && key <= Qt::Key_Z) {
    return QString(QChar(static_cast<char16_t>('A' + (key - Qt::Key_A))));
  }
  if (key >= Qt::Key_0 && key <= Qt::Key_9) {
    return QString(QChar(static_cast<char16_t>('0' + (key - Qt::Key_0))));
  }
  return {};
}

Issue inspectLabel(const EventLabelOverrides::Table& table, EventCatalog::EventId id,
                   const QString& text) {
  if (EventCatalog::nodeForId(id) == nullptr) {
    Issue issue;
    issue.kind = IssueKind::UnknownEvent;
    issue.eventId = id;
    return issue;
  }
  const QString trimmed = text.trimmed();
  if (trimmed.isEmpty()) {
    Issue issue;
    issue.kind = IssueKind::EmptyLabel;
    issue.eventId = id;
    return issue;
  }
  if (labelHasLineBreak(text)) {
    Issue issue;
    issue.kind = IssueKind::NotOneLine;
    issue.eventId = id;
    return issue;
  }

  EventLabelOverrides::Table copy = table;
  if (revertsToCatalogLabel(id, trimmed)) {
    copy.clearLabel(id);
    if (!copy.hidden(id)) {
      copy.setHidden(id, true);
      const EventLabelOverrides::Result shown = copy.setHidden(id, false);
      if (!shown.accepted) return issueFromResult(id, shown);
    }
    return noneIssue(id);
  }

  const EventLabelOverrides::Result stored = copy.setLabel(id, trimmed);
  if (!stored.accepted) return issueFromResult(id, stored);
  return noneIssue(id);
}

Issue inspectPathLabel(const EventLabelOverrides::Table& table,
                       const EventLabelOverrides::FollowUpPath& path, const QString& text) {
  const EventCatalog::EventId id = path.targetId();
  if (!EventLabelOverrides::followUpPathExists(path)) {
    Issue issue;
    issue.kind = IssueKind::UnknownEvent;
    issue.eventId = id;
    return issue;
  }
  const QString trimmed = text.trimmed();
  if (trimmed.isEmpty()) {
    Issue issue;
    issue.kind = IssueKind::EmptyLabel;
    issue.eventId = id;
    return issue;
  }
  if (labelHasLineBreak(text)) {
    Issue issue;
    issue.kind = IssueKind::NotOneLine;
    issue.eventId = id;
    return issue;
  }

  EventLabelOverrides::Table copy = table;
  if (revertsToPathLabel(table, path, trimmed)) {
    copy.clearPathLabel(path);
    return noneIssue(id);
  }
  const EventLabelOverrides::Result stored = copy.setPathLabel(path, trimmed);
  if (!stored.accepted) return issueFromResult(id, stored);
  return noneIssue(id);
}

Issue inspectPathShow(const EventLabelOverrides::Table& table,
                      const EventLabelOverrides::FollowUpPath& path) {
  const EventCatalog::EventId id = path.targetId();
  if (!EventLabelOverrides::followUpPathExists(path)) {
    Issue issue;
    issue.kind = IssueKind::UnknownEvent;
    issue.eventId = id;
    return issue;
  }
  if (!table.hiddenOnPath(path)) return noneIssue(id);
  EventLabelOverrides::Table copy = table;
  const EventLabelOverrides::Result shown = copy.setPathHidden(path, false);
  if (!shown.accepted) return issueFromResult(id, shown);
  return noneIssue(id);
}

Issue inspectShortcutText(const EventLabelOverrides::Table& table, EventCatalog::EventId id,
                          const QString& text) {
  if (!isMainEvent(id)) {
    Issue issue;
    issue.kind = EventCatalog::nodeForId(id) == nullptr ? IssueKind::UnknownEvent
                                                        : IssueKind::NotALetter;
    issue.eventId = id;
    return issue;
  }
  Qt::Key key = Qt::Key_unknown;
  const Issue parsed = parseShortcutText(id, text, &key);
  if (parsed.kind != IssueKind::None) return parsed;
  return duplicateKeyIssue(table, id, key);
}

Issue inspectShow(const EventLabelOverrides::Table& table, EventCatalog::EventId id) {
  if (EventCatalog::nodeForId(id) == nullptr) {
    Issue issue;
    issue.kind = IssueKind::UnknownEvent;
    issue.eventId = id;
    return issue;
  }
  if (!table.hidden(id)) return noneIssue(id);

  EventLabelOverrides::Table copy = table;
  const EventLabelOverrides::Result shown = copy.setHidden(id, false);
  if (!shown.accepted) return issueFromResult(id, shown);
  if (!isMainEvent(id)) return noneIssue(id);

  const QVector<Issue> keys = reviewStoredKeys(copy);
  for (const Issue& issue : keys) {
    if (issue.eventId != id && issue.otherEventId != id) continue;
    if (issue.eventId == id) return issue;
    Issue rewritten = issue;
    rewritten.eventId = id;
    rewritten.otherEventId = issue.eventId;
    return rewritten;
  }
  return noneIssue(id);
}

bool commitLabel(EventLabelOverrides::Table* table, EventCatalog::EventId id, const QString& text,
                 Issue* issue) {
  if (!table) {
    if (issue) {
      issue->kind = IssueKind::UnknownEvent;
      issue->eventId = id;
    }
    return false;
  }
  const Issue inspected = inspectLabel(*table, id, text);
  if (issue) *issue = inspected;
  if (inspected.kind != IssueKind::None) return false;
  const QString trimmed = text.trimmed();
  if (revertsToCatalogLabel(id, trimmed)) table->clearLabel(id);
  else table->setLabel(id, trimmed);
  table->clearPathLabelsFor(id);
  return true;
}

bool commitShortcutText(EventLabelOverrides::Table* table, EventCatalog::EventId id,
                        const QString& text, Issue* issue) {
  if (!table) {
    if (issue) {
      issue->kind = IssueKind::UnknownEvent;
      issue->eventId = id;
    }
    return false;
  }
  const Issue inspected = inspectShortcutText(*table, id, text);
  if (issue) *issue = inspected;
  if (inspected.kind != IssueKind::None) return false;
  const Qt::Key catalogKey = EventCatalog::shortcutKey(id);
  if (inspected.key == catalogKey) table->clearShortcutKey(id);
  else table->setShortcutKey(id, inspected.key);
  return true;
}

bool commitShown(EventLabelOverrides::Table* table, EventCatalog::EventId id, bool shown,
                 Issue* issue) {
  if (!table) {
    if (issue) {
      issue->kind = IssueKind::UnknownEvent;
      issue->eventId = id;
    }
    return false;
  }
  if (shown) {
    const Issue inspected = inspectShow(*table, id);
    if (issue) *issue = inspected;
    if (inspected.kind != IssueKind::None) return false;
  }
  const EventLabelOverrides::Result result = table->setHidden(id, !shown);
  if (!result.accepted) {
    if (issue) *issue = issueFromResult(id, result);
    return false;
  }
  table->clearPathHiddenFor(id);
  if (issue) *issue = noneIssue(id);
  return true;
}

bool commitPathLabel(EventLabelOverrides::Table* table, const EventLabelOverrides::FollowUpPath& path,
                     const QString& text, Issue* issue) {
  const EventCatalog::EventId id = path.targetId();
  if (!table) {
    if (issue) {
      issue->kind = IssueKind::UnknownEvent;
      issue->eventId = id;
    }
    return false;
  }
  const Issue inspected = inspectPathLabel(*table, path, text);
  if (issue) *issue = inspected;
  if (inspected.kind != IssueKind::None) return false;
  const QString trimmed = text.trimmed();
  if (revertsToPathLabel(*table, path, trimmed)) table->clearPathLabel(path);
  else table->setPathLabel(path, trimmed);
  return true;
}

bool commitPathShown(EventLabelOverrides::Table* table, const EventLabelOverrides::FollowUpPath& path,
                     bool shown, Issue* issue) {
  const EventCatalog::EventId id = path.targetId();
  if (!table) {
    if (issue) {
      issue->kind = IssueKind::UnknownEvent;
      issue->eventId = id;
    }
    return false;
  }
  if (shown) {
    const Issue inspected = inspectPathShow(*table, path);
    if (issue) *issue = inspected;
    if (inspected.kind != IssueKind::None) return false;
  }
  const EventLabelOverrides::Result result = table->setPathHidden(path, !shown);
  if (!result.accepted) {
    if (issue) *issue = issueFromResult(id, result);
    return false;
  }
  if (issue) *issue = noneIssue(id);
  return true;
}

QVector<Issue> reviewStoredKeys(const EventLabelOverrides::Table& table) {
  QVector<Issue> issues;
  struct Assignment {
    EventCatalog::EventId id = EventCatalog::EventId::None;
    Qt::Key key = Qt::Key_unknown;
  };
  QVector<Assignment> used;
  used.reserve(EventCatalog::buttonGridCount());
  for (int index = 0; index < EventCatalog::buttonGridCount(); ++index) {
    const EventCatalog::EventId id = EventCatalog::buttonGridNodeAt(index).id;
    if (table.hidden(id)) continue;
    const Qt::Key key = effectiveShortcut(id, table);
    if (isReservedKey(key)) {
      Issue issue;
      issue.kind = IssueKind::ReservedKey;
      issue.eventId = id;
      issue.key = key;
      issues.append(issue);
    } else if (key < Qt::Key_A || key > Qt::Key_Z) {
      Issue issue;
      issue.kind = IssueKind::NotALetter;
      issue.eventId = id;
      issue.key = key;
      issues.append(issue);
    } else {
      for (const Assignment& earlier : used) {
        if (earlier.key != key) continue;
        Issue issue;
        issue.kind = IssueKind::DuplicateKey;
        issue.eventId = id;
        issue.otherEventId = earlier.id;
        issue.key = key;
        issues.append(issue);
        break;
      }
    }
    used.append(Assignment{id, key});
  }
  return issues;
}

QString issueMessage(const Issue& issue, const EventLabelOverrides::Table& table) {
  const QString other = issue.otherEventId == EventCatalog::EventId::None
                            ? QString()
                            : EventLabelResolver::labelForId(issue.otherEventId, table);
  const QString letter = letterForKey(issue.key);
  switch (issue.kind) {
    case IssueKind::None:
      return {};
    case IssueKind::EmptyLabel:
      return AppLocale::trUi("controls_editor.empty_name");
    case IssueKind::NotOneLine:
      return AppLocale::trUi("controls_editor.one_line");
    case IssueKind::DuplicateLabel:
      return AppLocale::trUi("controls_editor.duplicate_name").arg(other);
    case IssueKind::DuplicateKey:
      return AppLocale::trUi("controls_editor.duplicate_key").arg(other, letter);
    case IssueKind::ReservedKey:
      return AppLocale::trUi("controls_editor.reserved_key").arg(letter, reservedKeyReason(issue.key));
    case IssueKind::NotALetter:
    case IssueKind::EmptyKey:
      return AppLocale::trUi("controls_editor.letter_key");
    case IssueKind::UnknownEvent:
      return AppLocale::trUi("controls_editor.unknown_event");
  }
  return {};
}

}  // namespace EventControlsValidation
