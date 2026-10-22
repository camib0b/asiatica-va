#include "TagFilter.h"

bool TagFilter::allows(const TagEvent::MainEventKey& mainEvent,
                       const TagEvent::FollowUpChain& followUps) const {
  if (eventPath.has_value() && !eventPath->mainEvent.isEmpty()) {
    if (mainEvent != eventPath->mainEvent) return false;
    return followUps.startsWith(eventPath->followUps);
  }
  return !hiddenMainEvents.contains(mainEvent);
}

bool TagFilter::isActive() const {
  if (eventPath.has_value() && !eventPath->mainEvent.isEmpty()) return true;
  return !hiddenMainEvents.isEmpty();
}

void TagFilter::showAllMainEvents() {
  hiddenMainEvents.clear();
}

void TagFilter::hideAllMainEvents(const QList<TagEvent::MainEventKey>& mainEvents) {
  hiddenMainEvents.clear();
  for (const TagEvent::MainEventKey& mainEvent : mainEvents) {
    if (!mainEvent.isEmpty()) hiddenMainEvents.insert(mainEvent);
  }
}

void TagFilter::clearEventPath() {
  eventPath.reset();
}
