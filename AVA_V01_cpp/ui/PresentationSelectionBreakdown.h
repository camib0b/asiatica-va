#pragma once

#include "TagEvent.h"

#include <QString>
#include <QVector>

class TagSession;

/// Groups the clips checked in the presenting list by catalog event.
///
/// The key is the catalog id (or the unlisted name when the clip is not a
/// catalog event). The label is the text the coach sees, including a session
/// rename. Order is the catalog array order, which is not the keyboard order
/// and not alphabetical. Unlisted names follow the catalog events.
namespace PresentationSelectionBreakdown {

struct EventCount {
  TagEvent::MainEventKey eventKey;
  QString displayLabel;
  int selectedCount = 0;
};

struct Result {
  /// Events with at least one selected clip, in catalog order.
  QVector<EventCount> events;
  /// True when the summary should open the event list. A selection that is
  /// already one event on the current list stays plain.
  bool interactive = false;
};

/// \p filterIsAllEvents ignores \p filteredEvent. Time-control keys and empty
/// keys are skipped. A null \p session uses catalog labels.
Result fromSelectedKeys(const QVector<TagEvent::MainEventKey>& selectedKeys,
                        const TagEvent::MainEventKey& filteredEvent, bool filterIsAllEvents,
                        const TagSession* session);

/// Drops counts below 1, fills a blank label from the canonical name, merges
/// duplicate keys, and sorts into catalog order.
QVector<EventCount> inCatalogOrder(QVector<EventCount> events);

/// Dev-only rows: long names, a count of 1, a large count, and a zero that
/// is removed. Interactive, because more than one event remains.
Result worstCaseResult();

/// Dev-only single row, count 1, marked interactive so the popup can be opened
/// on its own. Production does not mark a single on-screen event interactive.
Result oneEventResult();

}  // namespace PresentationSelectionBreakdown
