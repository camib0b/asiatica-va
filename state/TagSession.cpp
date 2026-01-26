#include "TagSession.h"

TagSession::TagSession(QObject* parent) : QObject(parent) {}

void TagSession::clear() {
  tags_.clear();
  mainEventCounts_.clear();
  followUpCountsByMainEvent_.clear();
  emit cleared();
  emit statsChanged();
}

void TagSession::addTag(const GameTag& tag) {
  tags_.push_back(tag);

  const int nextMainCount = mainEventCounts_.value(tag.mainEvent, 0) + 1;
  mainEventCounts_.insert(tag.mainEvent, nextMainCount);

  if (!tag.followUpEvent.isEmpty()) {
    auto& followUps = followUpCountsByMainEvent_[tag.mainEvent];
    const int nextFollowUpCount = followUps.value(tag.followUpEvent, 0) + 1;
    followUps.insert(tag.followUpEvent, nextFollowUpCount);
  }

  emit tagAdded(tag);
  emit statsChanged();
}

