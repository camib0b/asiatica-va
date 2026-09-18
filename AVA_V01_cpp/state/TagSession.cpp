#include "TagSession.h"

#include "EventDefaults.h"

#include <algorithm>

namespace {

constexpr int kQuarterCount = 4;

int quarterIndexForMainEvent(const QString& mainEvent) {
  for (int quarterIndex = 0; quarterIndex < kQuarterCount; ++quarterIndex) {
    if (mainEvent == EventDefaults::quarterCode(quarterIndex)) return quarterIndex;
  }
  return -1;
}

/// Enforces start >= 0, end >= start, and start <= mark <= end.
/// When \p videoDurationMs is >= 0, start and end are also capped to that duration.
/// Returns true when any of the three timestamps changed.
bool clampTagInterval(TagSession::GameTag& tag, qint64 videoDurationMs = -1) {
  const qint64 originalStartMs = tag.startMs;
  const qint64 originalEndMs = tag.endMs;
  const qint64 originalMarkMs = tag.markMs;

  if (tag.startMs < 0) tag.startMs = 0;
  if (videoDurationMs >= 0 && tag.startMs > videoDurationMs) tag.startMs = videoDurationMs;
  if (tag.endMs < tag.startMs) tag.endMs = tag.startMs;
  if (videoDurationMs >= 0 && tag.endMs > videoDurationMs) tag.endMs = videoDurationMs;
  if (tag.markMs < tag.startMs) tag.markMs = tag.startMs;
  if (tag.markMs > tag.endMs) tag.markMs = tag.endMs;

  return tag.startMs != originalStartMs || tag.endMs != originalEndMs ||
         tag.markMs != originalMarkMs;
}

}  // namespace

TagSession::TagSession(QObject* parent)
    : QObject(parent),
      nextTagId_(1),
      gameStartAnchorMs_(-1),
      currentQuarterIndex_(-1),
      currentQuarterStartMs_(0),
      quarterPhase_(QuarterPhase::NotStarted),
      closedQuarters_{} {}

void TagSession::clear() {
  tags_.clear();
  mainEventCounts_.clear();
  followUpCountsByMainEvent_.clear();
  nextTagId_ = 1;
  if (!matchNote_.isEmpty()) {
    matchNote_.clear();
    emit matchNoteChanged();
  }
  resetGameTimeState();
  emit cleared();
  emit tagsChanged();
}

void TagSession::clearGameMetadata() {
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

void TagSession::assignStableId(GameTag& tag) {
  if (tag.id == 0) {
    tag.id = nextTagId_++;
    return;
  }
  if (tag.id >= nextTagId_) {
    nextTagId_ = tag.id + 1;
  }
}

void TagSession::addTag(const GameTag& tag) {
  GameTag stored = tag;
  assignStableId(stored);
  if (stored.startMs == 0 && stored.endMs == 0) {
    // Caller did not provide an explicit interval; seed from per-event-type defaults.
    const auto duration = EventDefaults::defaultFor(stored.mainEvent);
    qint64 start = stored.markMs - duration.leadMs;
    qint64 end = stored.markMs + duration.lagMs;
    stored.startMs = start;
    stored.endMs = end;
  }
  clampTagInterval(stored);
  tags_.push_back(stored);

  const int nextMainCount = mainEventCounts_.value(stored.mainEvent, 0) + 1;
  mainEventCounts_.insert(stored.mainEvent, nextMainCount);

  if (!stored.followUpEvent.isEmpty()) {
    auto& followUps = followUpCountsByMainEvent_[stored.mainEvent];
    const int nextFollowUpCount = followUps.value(stored.followUpEvent, 0) + 1;
    followUps.insert(stored.followUpEvent, nextFollowUpCount);
  }

  restoreGameTimeStateFromTags();
  emit tagAdded(stored);
  emit tagsChanged();
}

TagSession::ImportResult TagSession::importTags(const QVector<GameTag>& tags,
                                                ImportMode mode,
                                                qint64 videoDurationMs) {
  ImportResult result;
  result.skippedCount = 0;

  if (mode == ImportMode::Replace) {
    tags_.clear();
    mainEventCounts_.clear();
    followUpCountsByMainEvent_.clear();
    nextTagId_ = 1;
    resetGameTimeState();
  }

  for (const GameTag& incoming : tags) {
    GameTag stored = incoming;
    assignStableId(stored);
    if (clampTagInterval(stored, videoDurationMs)) ++result.clampedCount;
    tags_.push_back(stored);
    ++result.importedCount;
  }

  std::stable_sort(tags_.begin(), tags_.end(),
                   [](const GameTag& a, const GameTag& b) {
                     if (a.startMs != b.startMs) return a.startMs < b.startMs;
                     return a.markMs < b.markMs;
                   });

  rebuildEventCountsFromTags();
  restoreGameTimeStateFromTags();
  emit tagsImported();
  emit tagsChanged();
  return result;
}

void TagSession::rebuildEventCountsFromTags() {
  mainEventCounts_.clear();
  followUpCountsByMainEvent_.clear();
  for (const GameTag& tag : tags_) {
    const int nextMainCount = mainEventCounts_.value(tag.mainEvent, 0) + 1;
    mainEventCounts_.insert(tag.mainEvent, nextMainCount);
    if (!tag.followUpEvent.isEmpty()) {
      auto& followUps = followUpCountsByMainEvent_[tag.mainEvent];
      const int nextFollowUpCount = followUps.value(tag.followUpEvent, 0) + 1;
      followUps.insert(tag.followUpEvent, nextFollowUpCount);
    }
  }
}

void TagSession::restoreGameTimeStateFromTags() {
  gameStartAnchorMs_ = -1;
  currentQuarterIndex_ = -1;
  currentQuarterStartMs_ = 0;
  quarterPhase_ = QuarterPhase::NotStarted;
  for (int quarterIndex = 0; quarterIndex < kQuarterCount; ++quarterIndex) {
    closedQuarters_[quarterIndex] = ClosedQuarterSpan{};
  }

  for (const GameTag& tag : tags_) {
    if (tag.mainEvent == QLatin1String(EventDefaults::TimeCodes::kStartAnchor)) {
      if (gameStartAnchorMs_ < 0) gameStartAnchorMs_ = tag.startMs;
      continue;
    }
    const int quarterIndex = quarterIndexForMainEvent(tag.mainEvent);
    if (quarterIndex < 0) continue;
    closedQuarters_[quarterIndex].present = true;
    closedQuarters_[quarterIndex].startMs = tag.startMs;
    closedQuarters_[quarterIndex].endMs = tag.endMs;
  }

  int closedPrefixCount = 0;
  while (closedPrefixCount < kQuarterCount && closedQuarters_[closedPrefixCount].present) {
    ++closedPrefixCount;
  }

  if (closedPrefixCount == kQuarterCount) {
    quarterPhase_ = QuarterPhase::GameEnded;
  } else if (closedPrefixCount > 0) {
    quarterPhase_ = QuarterPhase::QuarterInProgress;
    currentQuarterIndex_ = closedPrefixCount;
    currentQuarterStartMs_ = closedQuarters_[closedPrefixCount - 1].endMs;
  } else if (gameStartAnchorMs_ >= 0) {
    quarterPhase_ = QuarterPhase::QuarterInProgress;
    currentQuarterIndex_ = 0;
    currentQuarterStartMs_ = gameStartAnchorMs_;
  }
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
  restoreGameTimeStateFromTags();
  emit tagsChanged();
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

void TagSession::setMatchNote(const QString& html) {
  if (matchNote_ == html) return;
  matchNote_ = html;
  emit matchNoteChanged();
}

int TagSession::indexOfTagId(quint64 id) const {
  if (id == 0) return -1;
  for (int index = 0; index < tags_.size(); ++index) {
    if (tags_.at(index).id == id) return index;
  }
  return -1;
}

void TagSession::setTagInterval(int index, qint64 startMs, qint64 endMs) {
  if (index < 0 || index >= tags_.size()) return;
  if (startMs < 0) startMs = 0;
  if (endMs < startMs) endMs = startMs;
  GameTag& tag = tags_[index];
  tag.intervalManuallyEdited = true;
  if (tag.startMs == startMs && tag.endMs == endMs) return;
  tag.startMs = startMs;
  tag.endMs = endMs;
  restoreGameTimeStateFromTags();
  emit tagIntervalChanged(index);
}

void TagSession::applyDefaultsToUntrimmedTags(const QString& mainEvent, qint64 leadMs, qint64 lagMs) {
  if (mainEvent.isEmpty()) return;
  if (EventDefaults::isTimeControlEvent(mainEvent)) return;
  if (leadMs < 0) leadMs = 0;
  if (lagMs < 0) lagMs = 0;
  for (int i = 0; i < tags_.size(); ++i) {
    GameTag& tag = tags_[i];
    if (tag.mainEvent != mainEvent) continue;
    // Manually trimmed clips (export review or fixed game-time spans) are never touched.
    if (tag.intervalManuallyEdited) continue;
    qint64 start = tag.markMs - leadMs;
    qint64 end = tag.markMs + lagMs;
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
  for (int quarterIndex = 0; quarterIndex < kQuarterCount; ++quarterIndex) {
    closedQuarters_[quarterIndex] = ClosedQuarterSpan{};
  }
}

QString TagSession::periodLabelAtTimestampMs(qint64 positionMs) const {
  int matchingQuarterIndex = -1;
  qint64 matchingStartMs = 0;
  qint64 matchingEndMs = 0;
  for (int quarterIndex = 0; quarterIndex < kQuarterCount; ++quarterIndex) {
    const ClosedQuarterSpan& closedQuarter = closedQuarters_[quarterIndex];
    if (!closedQuarter.present) continue;
    if (positionMs < closedQuarter.startMs || positionMs > closedQuarter.endMs) continue;
    const bool isBetterMatch =
        matchingQuarterIndex < 0 || closedQuarter.startMs < matchingStartMs ||
        (closedQuarter.startMs == matchingStartMs && closedQuarter.endMs < matchingEndMs);
    if (!isBetterMatch) continue;
    matchingQuarterIndex = quarterIndex;
    matchingStartMs = closedQuarter.startMs;
    matchingEndMs = closedQuarter.endMs;
  }
  if (matchingQuarterIndex >= 0) return EventDefaults::quarterCode(matchingQuarterIndex);

  // Q1 (or later) in progress: no closed span covers this timestamp yet.
  if (quarterPhase_ == QuarterPhase::QuarterInProgress &&
      currentQuarterIndex_ >= 0 && currentQuarterIndex_ < kQuarterCount &&
      positionMs >= currentQuarterStartMs_) {
    return EventDefaults::quarterCode(currentQuarterIndex_);
  }

  // Game over: timestamps after Q4's recorded end still belong to Q4.
  if (quarterPhase_ == QuarterPhase::GameEnded) {
    const ClosedQuarterSpan& finalQuarter = closedQuarters_[kQuarterCount - 1];
    if (finalQuarter.present && positionMs >= finalQuarter.startMs) {
      return EventDefaults::quarterCode(kQuarterCount - 1);
    }
  }

  return QString();
}
