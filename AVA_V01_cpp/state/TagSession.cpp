#include "TagSession.h"

#include "EventDefaults.h"

#include <algorithm>

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

TagSession::ImportResult TagSession::importTags(const QVector<GameTag>& tags,
                                                ImportMode mode,
                                                qint64 videoDurationMs) {
  ImportResult result;
  result.skippedCount = 0;

  if (mode == ImportMode::Replace) {
    tags_.clear();
    mainEventCounts_.clear();
    followUpCountsByMainEvent_.clear();
    resetGameTimeState();
  }

  for (const GameTag& incoming : tags) {
    GameTag stored = incoming;
    bool clamped = false;

    if (stored.startMs < 0) {
      stored.startMs = 0;
      clamped = true;
    }
    if (videoDurationMs >= 0 && stored.startMs > videoDurationMs) {
      stored.startMs = videoDurationMs;
      clamped = true;
    }
    if (stored.endMs < stored.startMs) {
      stored.endMs = stored.startMs;
      clamped = true;
    }
    if (videoDurationMs >= 0 && stored.endMs > videoDurationMs) {
      stored.endMs = videoDurationMs;
      clamped = true;
    }
    if (stored.positionMs < stored.startMs) {
      stored.positionMs = stored.startMs;
      clamped = true;
    }
    if (stored.positionMs > stored.endMs) {
      stored.positionMs = stored.endMs;
      clamped = true;
    }

    if (clamped) ++result.clampedCount;
    tags_.push_back(stored);
    ++result.importedCount;
  }

  std::stable_sort(tags_.begin(), tags_.end(),
                   [](const GameTag& a, const GameTag& b) {
                     if (a.startMs != b.startMs) return a.startMs < b.startMs;
                     return a.positionMs < b.positionMs;
                   });

  rebuildStatsFromTags();
  restoreGameTimeStateFromTags();
  emit tagsImported();
  emit statsChanged();
  return result;
}

void TagSession::rebuildStatsFromTags() {
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

  for (const GameTag& tag : tags_) {
    if (tag.mainEvent == QLatin1String(EventDefaults::TimeCodes::kStartAnchor)) {
      gameStartAnchorMs_ = tag.startMs;
      break;
    }
  }

  static const QString kQuarterCodes[4] = {
      QString::fromLatin1(EventDefaults::TimeCodes::kQuarter1),
      QString::fromLatin1(EventDefaults::TimeCodes::kQuarter2),
      QString::fromLatin1(EventDefaults::TimeCodes::kQuarter3),
      QString::fromLatin1(EventDefaults::TimeCodes::kQuarter4),
  };

  bool hasClosedQuarter[4] = {false, false, false, false};
  qint64 quarterStartMs[4] = {0, 0, 0, 0};
  qint64 quarterEndMs[4] = {0, 0, 0, 0};

  for (const GameTag& tag : tags_) {
    for (int i = 0; i < 4; ++i) {
      if (tag.mainEvent == kQuarterCodes[i]) {
        hasClosedQuarter[i] = true;
        quarterStartMs[i] = tag.startMs;
        quarterEndMs[i] = tag.endMs;
        break;
      }
    }
  }

  if (hasClosedQuarter[3]) {
    quarterPhase_ = QuarterPhase::GameEnded;
    return;
  }

  int lastClosedIndex = -1;
  for (int i = 3; i >= 0; --i) {
    if (hasClosedQuarter[i]) {
      lastClosedIndex = i;
      break;
    }
  }

  if (lastClosedIndex >= 0 && lastClosedIndex < 3) {
    const int nextIndex = lastClosedIndex + 1;
    quarterPhase_ = QuarterPhase::QuarterInProgress;
    currentQuarterIndex_ = nextIndex;
    currentQuarterStartMs_ = quarterEndMs[lastClosedIndex];
    return;
  }

  if (lastClosedIndex == -1 && gameStartAnchorMs_ >= 0) {
    quarterPhase_ = QuarterPhase::QuarterInProgress;
    currentQuarterIndex_ = 0;
    currentQuarterStartMs_ = gameStartAnchorMs_;
    return;
  }

  for (int i = 0; i < 4; ++i) {
    if (hasClosedQuarter[i]) continue;
    if (i == 0 && gameStartAnchorMs_ >= 0) {
      quarterPhase_ = QuarterPhase::QuarterInProgress;
      currentQuarterIndex_ = 0;
      currentQuarterStartMs_ = gameStartAnchorMs_;
      return;
    }
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

void TagSession::setTagInterval(int index, qint64 startMs, qint64 endMs, bool userInitiated) {
  if (index < 0 || index >= tags_.size()) return;
  if (startMs < 0) startMs = 0;
  if (endMs < startMs) endMs = startMs;
  GameTag& tag = tags_[index];
  if (userInitiated) {
    tag.intervalManuallyEdited = true;
  }
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
    // Manually trimmed clips (export review or fixed game-time spans) are never touched.
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

QString TagSession::periodLabelAtTimestampMs(qint64 positionMs) const {
  struct QuarterSpan {
    QString label;
    qint64 startMs = 0;
    qint64 endMs = 0;
  };

  QVector<QuarterSpan> closedQuarterSpans;
  closedQuarterSpans.reserve(4);
  for (const GameTag& tag : tags_) {
    const bool isQuarterTag =
        tag.mainEvent == QLatin1String(EventDefaults::TimeCodes::kQuarter1) ||
        tag.mainEvent == QLatin1String(EventDefaults::TimeCodes::kQuarter2) ||
        tag.mainEvent == QLatin1String(EventDefaults::TimeCodes::kQuarter3) ||
        tag.mainEvent == QLatin1String(EventDefaults::TimeCodes::kQuarter4);
    if (!isQuarterTag) continue;
    closedQuarterSpans.append({tag.mainEvent, tag.startMs, tag.endMs});
  }

  std::sort(closedQuarterSpans.begin(), closedQuarterSpans.end(),
            [](const QuarterSpan& lhs, const QuarterSpan& rhs) {
              if (lhs.startMs == rhs.startMs) return lhs.endMs < rhs.endMs;
              return lhs.startMs < rhs.startMs;
            });

  for (const QuarterSpan& quarterSpan : closedQuarterSpans) {
    if (positionMs >= quarterSpan.startMs && positionMs <= quarterSpan.endMs) {
      return quarterSpan.label;
    }
  }

  if (quarterPhase_ == QuarterPhase::QuarterInProgress) {
    const int quarterIndex = currentQuarterIndex_;
    if (quarterIndex >= 0 && quarterIndex < 4 && positionMs >= currentQuarterStartMs_) {
      static const char* kQuarterLabels[4] = {"Q1", "Q2", "Q3", "Q4"};
      return QString::fromLatin1(kQuarterLabels[quarterIndex]);
    }
  }

  return QString();
}
