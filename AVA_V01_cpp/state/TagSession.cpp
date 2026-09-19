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

/// Start-anchor and Q1–Q4 drive period labels. Timeouts do not.
bool isGameTimeMainEvent(const QString& mainEvent, int* quarterIndexOut = nullptr) {
  if (mainEvent == QLatin1String(EventDefaults::TimeCodes::kStartAnchor)) {
    if (quarterIndexOut) *quarterIndexOut = -1;
    return true;
  }
  const int quarterIndex = quarterIndexForMainEvent(mainEvent);
  if (quarterIndex < 0) return false;
  if (quarterIndexOut) *quarterIndexOut = quarterIndex;
  return true;
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

TagSession::TeamSide TagSession::teamSideFromKey(const QString& teamKey) {
  const QString trimmed = teamKey.trimmed();
  if (trimmed.compare(QLatin1String("Home"), Qt::CaseInsensitive) == 0) {
    return TeamSide::Home;
  }
  if (trimmed.compare(QLatin1String("Away"), Qt::CaseInsensitive) == 0) {
    return TeamSide::Away;
  }
  return TeamSide::Unspecified;
}

void TagSession::clear() {
  tags_.clear();
  nextTagId_ = 1;
  gameTimeTags_.clear();
  rebuildEventCountsFromTags();
  if (!matchNote_.isEmpty()) {
    matchNote_.clear();
    emit matchNoteChanged();
  }
  resetGameTimeState();
  emit cleared();
  emit tagsChanged();
}

void TagSession::clearGameMetadata() {
  const bool alreadyEmpty =
      homeTeamName_.isEmpty() && awayTeamName_.isEmpty() && homeTeamColor_.isEmpty() &&
      awayTeamColor_.isEmpty() && competitionName_.isEmpty() && !gameDate_.isValid() &&
      homeAbbrev_.isEmpty() && awayAbbrev_.isEmpty();
  if (alreadyEmpty) return;

  homeTeamName_.clear();
  awayTeamName_.clear();
  homeTeamColor_.clear();
  awayTeamColor_.clear();
  competitionName_.clear();
  gameDate_ = QDate();
  homeAbbrev_.clear();
  awayAbbrev_.clear();
  emit gameMetadataChanged();
}

void TagSession::setGameTeams(const QString& homeName, const QString& awayName,
                              const QString& homeColor, const QString& awayColor) {
  const QString nextHomeTeamName = homeName.trimmed();
  const QString nextAwayTeamName = awayName.trimmed();
  const QString nextHomeTeamColor = homeColor.trimmed();
  const QString nextAwayTeamColor = awayColor.trimmed();
  if (homeTeamName_ == nextHomeTeamName && awayTeamName_ == nextAwayTeamName &&
      homeTeamColor_ == nextHomeTeamColor && awayTeamColor_ == nextAwayTeamColor) {
    return;
  }

  homeTeamName_ = nextHomeTeamName;
  awayTeamName_ = nextAwayTeamName;
  homeTeamColor_ = nextHomeTeamColor;
  awayTeamColor_ = nextAwayTeamColor;
  emit gameMetadataChanged();
}

void TagSession::setGameMetadata(const QString& competitionName,
                                 const QDate& gameDate,
                                 const QString& homeAbbrev,
                                 const QString& awayAbbrev) {
  const QString nextCompetitionName = competitionName.trimmed();
  const QString nextHomeAbbrev = homeAbbrev.trimmed().toUpper();
  const QString nextAwayAbbrev = awayAbbrev.trimmed().toUpper();
  if (competitionName_ == nextCompetitionName && gameDate_ == gameDate &&
      homeAbbrev_ == nextHomeAbbrev && awayAbbrev_ == nextAwayAbbrev) {
    return;
  }

  competitionName_ = nextCompetitionName;
  gameDate_ = gameDate;
  homeAbbrev_ = nextHomeAbbrev;
  awayAbbrev_ = nextAwayAbbrev;
  emit gameMetadataChanged();
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

  rebuildEventCountsFromTags();
  int gameTimeQuarterIndex = -1;
  if (isGameTimeMainEvent(stored.mainEvent, &gameTimeQuarterIndex)) {
    gameTimeTags_.push_back(
        {stored.id, gameTimeQuarterIndex, stored.startMs, stored.endMs});
    restoreGameTimeStateFromTags();
  }
  emit tagAdded(stored);
  emit tagsChanged();
}

TagSession::ImportResult TagSession::importTags(const QVector<GameTag>& tags,
                                                ImportMode mode,
                                                qint64 videoDurationMs) {
  ImportResult result;

  if (mode == ImportMode::Replace) {
    tags_.clear();
    nextTagId_ = 1;
    gameTimeTags_.clear();
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
  rebuildGameTimeIndexFromTags();
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

void TagSession::rebuildGameTimeIndexFromTags() {
  gameTimeTags_.clear();
  for (const GameTag& tag : tags_) {
    int gameTimeQuarterIndex = -1;
    if (!isGameTimeMainEvent(tag.mainEvent, &gameTimeQuarterIndex)) continue;
    gameTimeTags_.push_back({tag.id, gameTimeQuarterIndex, tag.startMs, tag.endMs});
  }
}

void TagSession::upsertGameTimeTag(const GameTag& tag) {
  int gameTimeQuarterIndex = -1;
  if (!isGameTimeMainEvent(tag.mainEvent, &gameTimeQuarterIndex)) return;
  for (GameTimeTagRecord& existing : gameTimeTags_) {
    if (existing.id != tag.id) continue;
    existing.quarterIndex = gameTimeQuarterIndex;
    existing.startMs = tag.startMs;
    existing.endMs = tag.endMs;
    return;
  }
  gameTimeTags_.push_back({tag.id, gameTimeQuarterIndex, tag.startMs, tag.endMs});
}

void TagSession::removeGameTimeTagId(quint64 id) {
  if (id == 0) return;
  for (int recordIndex = 0; recordIndex < gameTimeTags_.size(); ++recordIndex) {
    if (gameTimeTags_.at(recordIndex).id != id) continue;
    gameTimeTags_.removeAt(recordIndex);
    return;
  }
}

void TagSession::restoreGameTimeStateFromTags() {
  resetGameTimeState();

  for (const GameTimeTagRecord& record : gameTimeTags_) {
    if (record.quarterIndex < 0) {
      if (gameStartAnchorMs_ < 0) gameStartAnchorMs_ = record.startMs;
      continue;
    }
    if (record.quarterIndex >= kQuarterCount) continue;
    closedQuarters_[record.quarterIndex].present = true;
    closedQuarters_[record.quarterIndex].startMs = record.startMs;
    closedQuarters_[record.quarterIndex].endMs = record.endMs;
  }

  int closedPrefixCount = 0;
  while (closedPrefixCount < kQuarterCount && closedQuarters_[closedPrefixCount].present) {
    ++closedPrefixCount;
  }

  if (closedPrefixCount == kQuarterCount) {
    quarterPhase_ = QuarterPhase::GameEnded;
    // No quarter is in progress. currentQuarterIndex_ stays -1 (reset above).
    // periodLabelAtTimestampMs reads closedQuarters_[Q4], not this index.
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

bool TagSession::removeTag(int index) {
  if (!isValidTagIndex(index)) {
    Q_ASSERT_X(false, "TagSession::removeTag", "invalid tag index");
    return false;
  }

  const GameTag removedTag = tags_.at(index);
  tags_.removeAt(index);
  rebuildEventCountsFromTags();
  if (isGameTimeMainEvent(removedTag.mainEvent)) {
    removeGameTimeTagId(removedTag.id);
    restoreGameTimeStateFromTags();
  }
  emit tagsChanged();
  return true;
}

bool TagSession::setTagNote(int index, const QString& note) {
  if (!isValidTagIndex(index)) {
    Q_ASSERT_X(false, "TagSession::setTagNote", "invalid tag index");
    return false;
  }
  if (tags_[index].note == note) return true;
  tags_[index].note = note;
  emit tagNoteChanged(index);
  return true;
}

QString TagSession::tagNote(int index) const {
  if (!isValidTagIndex(index)) {
    Q_ASSERT_X(false, "TagSession::tagNote", "invalid tag index");
    return QString();
  }
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

bool TagSession::setTagInterval(int index, qint64 startMs, qint64 endMs) {
  if (!isValidTagIndex(index)) {
    Q_ASSERT_X(false, "TagSession::setTagInterval", "invalid tag index");
    return false;
  }
  if (startMs < 0) startMs = 0;
  if (endMs < startMs) endMs = startMs;
  GameTag& tag = tags_[index];
  if (tag.startMs == startMs && tag.endMs == endMs) return true;
  tag.startMs = startMs;
  tag.endMs = endMs;
  tag.intervalManuallyEdited = true;
  clampTagInterval(tag);
  if (isGameTimeMainEvent(tag.mainEvent)) {
    upsertGameTimeTag(tag);
    restoreGameTimeStateFromTags();
  }
  emit tagIntervalChanged(index);
  return true;
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
  // O(kQuarterCount) over cached spans. Does not walk tags_ or gameTimeTags_.
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
