#pragma once

#include "TagEvent.h"

#include <QList>
#include <QSet>

#include <optional>

/// Drill-down filter from the stats tree. An empty follow-up chain means every
/// follow-up of `mainEvent`.
struct EventPathFilter {
  TagEvent::MainEventKey mainEvent;
  TagEvent::FollowUpChain followUps;
};

/// Which tags the work table shows.
///
/// Precedence is explicit: when `eventPath` is set, it is the only rule.
/// `hiddenMainEvents` applies only while no event path is set. Checkbox state
/// in the menu is a rendering of this value, not a second copy of it.
struct TagFilter {
  QSet<TagEvent::MainEventKey> hiddenMainEvents;
  std::optional<EventPathFilter> eventPath;

  bool allows(const TagEvent::MainEventKey& mainEvent,
              const TagEvent::FollowUpChain& followUps) const;
  bool isActive() const;

  void showAllMainEvents();
  void hideAllMainEvents(const QList<TagEvent::MainEventKey>& mainEvents);
  void clearEventPath();
};
