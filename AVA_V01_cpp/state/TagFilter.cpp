#include "TagFilter.h"

namespace {

bool followUpMatchesPath(const QString& followUpEvent, const QString& filterFollowUp) {
  if (filterFollowUp.isEmpty()) return true;
  if (followUpEvent == filterFollowUp) return true;
  const QString pathPrefix = filterFollowUp + QStringLiteral(" → ");
  return followUpEvent.startsWith(pathPrefix);
}

}  // namespace

bool TagFilter::allows(const QString& mainEvent, const QString& followUpEvent) const {
  if (eventPath.has_value() && !eventPath->mainEvent.isEmpty()) {
    if (mainEvent != eventPath->mainEvent) return false;
    return followUpMatchesPath(followUpEvent, eventPath->followUpEvent);
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

void TagFilter::hideAllMainEvents(const QStringList& mainEvents) {
  hiddenMainEvents.clear();
  for (const QString& mainEvent : mainEvents) {
    if (!mainEvent.isEmpty()) hiddenMainEvents.insert(mainEvent);
  }
}

void TagFilter::clearEventPath() {
  eventPath.reset();
}
