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

void TagSession::removeTag(int index) {
  if (index < 0 || index >= tags_.size()) return;

  const GameTag& tag = tags_.at(index);
  
  // Decrement main event count
  const int currentMainCount = mainEventCounts_.value(tag.mainEvent, 0);
  if (currentMainCount > 0) {
    mainEventCounts_.insert(tag.mainEvent, currentMainCount - 1);
    if (currentMainCount == 1) {
      mainEventCounts_.remove(tag.mainEvent);
    }
  }

  // Decrement follow-up count if present
  if (!tag.followUpEvent.isEmpty()) {
    auto& followUps = followUpCountsByMainEvent_[tag.mainEvent];
    const int currentFollowUpCount = followUps.value(tag.followUpEvent, 0);
    if (currentFollowUpCount > 0) {
      followUps.insert(tag.followUpEvent, currentFollowUpCount - 1);
      if (currentFollowUpCount == 1) {
        followUps.remove(tag.followUpEvent);
        if (followUps.isEmpty()) {
          followUpCountsByMainEvent_.remove(tag.mainEvent);
        }
      }
    }
  }

  tags_.removeAt(index);
  emit statsChanged();
}

void TagSession::setTagNote(int index, const QString& note) {
  if (index < 0 || index >= tags_.size()) return;
  if (tags_[index].note == note) return;
  tags_[index].note = note;
  emit tagNoteChanged(index);
}

QString TagSession::tagNote(int index) const {
  if (index < 0 || index >= tags_.size()) return QString();
  return tags_[index].note;
}

  