#include "TagSession.h"

#include "EventDefaults.h"

TagSession::TagSession(QObject* parent) : QObject(parent) {}

void TagSession::clear() {
  tags_.clear();
  mainEventCounts_.clear();
  followUpCountsByMainEvent_.clear();
  resetGameTimeState();
  emit cleared();
  emit statsChanged();
}

void TagSession::clearTeamInfo() {
  homeTeamName_.clear();
  awayTeamName_.clear();
  homeTeamColor_.clear();
  awayTeamColor_.clear();
  competitionName_.clear();
  gameDate_ = QDate();
  homeAbbrev_.clear();
  awayAbbrev_.clear();
}

void TagSession::setGameTeams(const QString& homeName, const QString& awayName,
                              const QString& homeColor, const QString& awayColor) {
  homeTeamName_ = homeName.trimmed();
  awayTeamName_ = awayName.trimmed();
  homeTeamColor_ = homeColor.trimmed();
  awayTeamColor_ = awayColor.trimmed();
}

void TagSession::setGameMetadata(const QString& competitionName,
                                 const QDate& gameDate,
                                 const QString& homeAbbrev,
                                 const QString& awayAbbrev) {
  competitionName_ = competitionName.trimmed();
  gameDate_ = gameDate;
  homeAbbrev_ = homeAbbrev.trimmed().toUpper();
  awayAbbrev_ = awayAbbrev.trimmed().toUpper();
}

void TagSession::addTag(const GameTag& tag) {
  GameTag stored = tag;
  if (stored.startMs == 0 && stored.endMs == 0) {
    // Caller did not provide an explicit interval; seed from per-event-type defaults.
    const auto duration = EventDefaults::defaultFor(stored.mainEvent);
    qint64 start = stored.positionMs - duration.preMs;
    qint64 end = stored.positionMs + duration.postMs;
    if (start < 0) start = 0;
    if (end < start) end = start;
    stored.startMs = start;
    stored.endMs = end;
  }
  tags_.push_back(stored);

  const int nextMainCount = mainEventCounts_.value(stored.mainEvent, 0) + 1;
  mainEventCounts_.insert(stored.mainEvent, nextMainCount);

  if (!stored.followUpEvent.isEmpty()) {
    auto& followUps = followUpCountsByMainEvent_[stored.mainEvent];
    const int nextFollowUpCount = followUps.value(stored.followUpEvent, 0) + 1;
    followUps.insert(stored.followUpEvent, nextFollowUpCount);
  }

  emit tagAdded(stored);
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

void TagSession::setTagInterval(int index, qint64 startMs, qint64 endMs) {
  if (index < 0 || index >= tags_.size()) return;
  if (startMs < 0) startMs = 0;
  if (endMs < startMs) endMs = startMs;
  GameTag& tag = tags_[index];
  if (tag.startMs == startMs && tag.endMs == endMs) return;
  tag.startMs = startMs;
  tag.endMs = endMs;
  tag.intervalManuallyEdited = true;
  emit tagIntervalChanged(index);
}

void TagSession::applyDefaultsToUntrimmedTags(const QString& mainEvent, qint64 preMs, qint64 postMs) {
  if (mainEvent.isEmpty()) return;
  if (EventDefaults::isTimeControlEvent(mainEvent)) return;
  if (preMs < 0) preMs = 0;
  if (postMs < 0) postMs = 0;
  for (int i = 0; i < tags_.size(); ++i) {
    GameTag& tag = tags_[i];
    if (tag.mainEvent != mainEvent) continue;
    if (tag.intervalManuallyEdited) continue;
    qint64 start = tag.positionMs - preMs;
    qint64 end = tag.positionMs + postMs;
    if (start < 0) start = 0;
    if (end < start) end = start;
    if (tag.startMs == start && tag.endMs == end) continue;
    tag.startMs = start;
    tag.endMs = end;
    emit tagIntervalChanged(i);
  }
}

void TagSession::setCurrentQuarter(int index, qint64 startMs) {
  currentQuarterIndex_ = index;
  currentQuarterStartMs_ = startMs;
  quarterPhase_ = QuarterPhase::QuarterInProgress;
}

void TagSession::clearCurrentQuarter() {
  currentQuarterIndex_ = -1;
  currentQuarterStartMs_ = 0;
}

void TagSession::resetGameTimeState() {
  gameStartAnchorMs_ = -1;
  currentQuarterIndex_ = -1;
  currentQuarterStartMs_ = 0;
  quarterPhase_ = QuarterPhase::NotStarted;
}
