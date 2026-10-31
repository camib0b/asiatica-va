#include "TagSession.h"

#include "EventCatalog.h"
#include "EventDefaults.h"

#include <algorithm>

namespace {

constexpr int kQuarterCount = 4;
constexpr qint64 kStartAnchorDurationMs = 2000;

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

bool isGameTimeTag(const TagSession::GameTag& tag, int* quarterIndexOut = nullptr) {
  if (tag.mainEventId != EventCatalog::EventId::None) return false;
  return isGameTimeMainEvent(tag.unlistedName, quarterIndexOut);
}

EventLabelOverrides::Result publishIfOverrideChanged(TagSession* session,
                                                     const EventLabelOverrides::Table& before,
                                                     const EventLabelOverrides::Table& after,
                                                     const EventLabelOverrides::Result& result) {
  if (result.accepted && before != after) emit session->eventLabelOverridesChanged();
  return result;
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
      tagIdEpoch_(1),
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

QString TagSession::teamSideKey(TeamSide side) {
  switch (side) {
    case TeamSide::Home:
      return QStringLiteral("Home");
    case TeamSide::Away:
      return QStringLiteral("Away");
    case TeamSide::Unspecified:
      return QString();
  }
  return QString();
}

void TagSession::clear() {
  emit aboutToClear();
  tags_.clear();
  nextTagId_ = 1;
  ++tagIdEpoch_;
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

EventLabelOverrides::Result TagSession::setEventLabel(EventCatalog::EventId id,
                                                      const QString& label) {
  const EventLabelOverrides::Table before = eventLabelOverrides_;
  const EventLabelOverrides::Result result = eventLabelOverrides_.setLabel(id, label);
  if (result.accepted) eventLabelOverrides_.clearPathLabelsFor(id);
  return publishIfOverrideChanged(this, before, eventLabelOverrides_, result);
}

EventLabelOverrides::Result TagSession::clearEventLabel(EventCatalog::EventId id) {
  const EventLabelOverrides::Table before = eventLabelOverrides_;
  const EventLabelOverrides::Result result = eventLabelOverrides_.clearLabel(id);
  return publishIfOverrideChanged(this, before, eventLabelOverrides_, result);
}

EventLabelOverrides::Result TagSession::setEventShortcutKey(EventCatalog::EventId id, Qt::Key key) {
  const EventLabelOverrides::Table before = eventLabelOverrides_;
  const EventLabelOverrides::Result result = eventLabelOverrides_.setShortcutKey(id, key);
  return publishIfOverrideChanged(this, before, eventLabelOverrides_, result);
}

EventLabelOverrides::Result TagSession::clearEventShortcutKey(EventCatalog::EventId id) {
  const EventLabelOverrides::Table before = eventLabelOverrides_;
  const EventLabelOverrides::Result result = eventLabelOverrides_.clearShortcutKey(id);
  return publishIfOverrideChanged(this, before, eventLabelOverrides_, result);
}

EventLabelOverrides::Result TagSession::setEventHidden(EventCatalog::EventId id, bool hidden) {
  const EventLabelOverrides::Table before = eventLabelOverrides_;
  const EventLabelOverrides::Result result = eventLabelOverrides_.setHidden(id, hidden);
  if (result.accepted) eventLabelOverrides_.clearPathHiddenFor(id);
  return publishIfOverrideChanged(this, before, eventLabelOverrides_, result);
}

void TagSession::clearEventLabelOverrides() {
  if (eventLabelOverrides_ == EventLabelOverrides::Table{}) return;
  eventLabelOverrides_.clear();
  emit eventLabelOverridesChanged();
}

bool TagSession::replaceEventLabelOverrides(const EventLabelOverrides::Table& overrides) {
  if (eventLabelOverrides_ == overrides) return true;
  EventLabelOverrides::Table replacement;
  if (!replacement.replaceAll(overrides.records(), overrides.pathRecords())) return false;
  eventLabelOverrides_ = replacement;
  emit eventLabelOverridesChanged();
  return true;
}

void TagSession::clearGameMetadata() {
  const bool alreadyEmpty =
      homeTeamName_.isEmpty() && awayTeamName_.isEmpty() && homeTeamColor_.isEmpty() &&
      awayTeamColor_.isEmpty() && !gameDate_.isValid() && homeAbbrev_.isEmpty() &&
      awayAbbrev_.isEmpty();
  if (alreadyEmpty) return;

  homeTeamName_.clear();
  awayTeamName_.clear();
  homeTeamColor_.clear();
  awayTeamColor_.clear();
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

void TagSession::setGameMetadata(const QDate& gameDate,
                                 const QString& homeAbbrev,
                                 const QString& awayAbbrev) {
  const QString nextHomeAbbrev = homeAbbrev.trimmed().toUpper();
  const QString nextAwayAbbrev = awayAbbrev.trimmed().toUpper();
  if (gameDate_ == gameDate && homeAbbrev_ == nextHomeAbbrev && awayAbbrev_ == nextAwayAbbrev) {
    return;
  }

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
    const auto duration =
        EventDefaults::defaultFor(TagEvent::canonicalName(stored.mainEventKey()));
    qint64 start = stored.markMs - duration.leadMs;
    qint64 end = stored.markMs + duration.lagMs;
    stored.startMs = start;
    stored.endMs = end;
  }
  clampTagInterval(stored);
  tags_.push_back(stored);

  rebuildEventCountsFromTags();
  int gameTimeQuarterIndex = -1;
  if (isGameTimeTag(stored, &gameTimeQuarterIndex)) {
    gameTimeTags_.push_back(
        {stored.id, gameTimeQuarterIndex, stored.startMs, stored.endMs});
    restoreGameTimeStateFromTags();
  }
  emit tagAdded(stored);
  emit tagsChanged(stored.id);
}

TagSession::ImportResult TagSession::importTags(const QVector<GameTag>& tags,
                                                ImportMode mode,
                                                qint64 videoDurationMs) {
  ImportResult result;
  emit aboutToReplaceTags();

  if (mode == ImportMode::Replace) {
    tags_.clear();
    nextTagId_ = 1;
    ++tagIdEpoch_;
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

void TagSession::restoreTags(const QVector<GameTag>& tags, quint64 nextTagId) {
  emit aboutToReplaceTags();
  tags_.clear();
  nextTagId_ = 1;
  ++tagIdEpoch_;
  gameTimeTags_.clear();
  resetGameTimeState();

  tags_.reserve(tags.size());
  for (const GameTag& incoming : tags) {
    GameTag stored = incoming;
    assignStableId(stored);
    clampTagInterval(stored);
    tags_.push_back(stored);
  }
  if (nextTagId > nextTagId_) nextTagId_ = nextTagId;

  rebuildEventCountsFromTags();
  rebuildGameTimeIndexFromTags();
  restoreGameTimeStateFromTags();
  emit tagsImported();
  emit tagsChanged();
}

void TagSession::rebuildEventCountsFromTags() {
  mainEventCounts_.clear();
  followUpCountsByMainEvent_.clear();
  for (const GameTag& tag : tags_) {
    const TagEvent::MainEventKey key = tag.mainEventKey();
    const int nextMainCount = mainEventCounts_.value(key, 0) + 1;
    mainEventCounts_.insert(key, nextMainCount);
    const TagEvent::FollowUpChain chain = tag.followUpChain();
    if (!chain.empty()) {
      auto& followUps = followUpCountsByMainEvent_[key];
      const int nextFollowUpCount = followUps.value(chain, 0) + 1;
      followUps.insert(chain, nextFollowUpCount);
    }
  }
}

void TagSession::rebuildGameTimeIndexFromTags() {
  gameTimeTags_.clear();
  for (const GameTag& tag : tags_) {
    int gameTimeQuarterIndex = -1;
    if (!isGameTimeTag(tag, &gameTimeQuarterIndex)) continue;
    gameTimeTags_.push_back({tag.id, gameTimeQuarterIndex, tag.startMs, tag.endMs});
  }
}

void TagSession::upsertGameTimeTag(const GameTag& tag) {
  int gameTimeQuarterIndex = -1;
  if (!isGameTimeTag(tag, &gameTimeQuarterIndex)) return;
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
  gameStartAnchorMs_ = -1;
  for (int quarterIndex = 0; quarterIndex < kQuarterCount; ++quarterIndex) {
    closedQuarters_[quarterIndex] = ClosedQuarterSpan{};
  }

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

  QuarterPhase phase = QuarterPhase::NotStarted;
  int quarterIndex = -1;
  qint64 quarterStartMs = 0;
  if (closedPrefixCount == kQuarterCount) {
    phase = QuarterPhase::GameEnded;
    // No quarter is in progress. periodLabelAtTimestampMs reads
    // closedQuarters_[Q4], not the in-progress index.
  } else if (closedPrefixCount > 0) {
    phase = QuarterPhase::QuarterInProgress;
    quarterIndex = closedPrefixCount;
    quarterStartMs = closedQuarters_[closedPrefixCount - 1].endMs;
  } else if (gameStartAnchorMs_ >= 0) {
    phase = QuarterPhase::QuarterInProgress;
    quarterIndex = 0;
    quarterStartMs = gameStartAnchorMs_;
  }
  commitQuarterPhase(phase, quarterIndex, quarterStartMs);
}

bool TagSession::removeTag(int index) {
  if (!isValidTagIndex(index)) {
    Q_ASSERT_X(false, "TagSession::removeTag", "invalid tag index");
    return false;
  }

  const GameTag removedTag = tags_.at(index);
  tags_.removeAt(index);
  rebuildEventCountsFromTags();
  if (isGameTimeTag(removedTag)) {
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
  if (isGameTimeTag(tag)) {
    upsertGameTimeTag(tag);
    restoreGameTimeStateFromTags();
  }
  emit tagIntervalChanged(index);
  return true;
}

void TagSession::applyDefaultsToUntrimmedTags(const QString& mainEvent, qint64 leadMs, qint64 lagMs) {
  const TagEvent::MainEventKey key = TagEvent::keyFromCanonicalName(mainEvent);
  if (key.isEmpty() || key.isTimeControl()) return;
  if (leadMs < 0) leadMs = 0;
  if (lagMs < 0) lagMs = 0;
  for (int i = 0; i < tags_.size(); ++i) {
    GameTag& tag = tags_[i];
    if (tag.mainEventKey() != key) continue;
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

bool TagSession::isLegalQuarterSnapshot(QuarterPhase phase, int currentQuarterIndex) {
  switch (phase) {
    case QuarterPhase::NotStarted:
    case QuarterPhase::GameEnded:
      return currentQuarterIndex < 0;
    case QuarterPhase::QuarterInProgress:
      return currentQuarterIndex >= 0 && currentQuarterIndex < kQuarterCount;
  }
  return false;
}

bool TagSession::commitQuarterPhase(QuarterPhase phase,
                                    int currentQuarterIndex,
                                    qint64 quarterStartMs) {
  if (!isLegalQuarterSnapshot(phase, currentQuarterIndex)) {
    qWarning("TagSession: refused illegal quarter snapshot phase=%d index=%d",
             static_cast<int>(phase), currentQuarterIndex);
    Q_ASSERT_X(false, "TagSession::commitQuarterPhase", "illegal quarter snapshot");
    return false;
  }

  const bool visibleChange =
      quarterPhase_ != phase || currentQuarterIndex_ != currentQuarterIndex;
  quarterPhase_ = phase;
  currentQuarterIndex_ = currentQuarterIndex;
  currentQuarterStartMs_ = quarterStartMs;
  if (visibleChange) emit quarterPhaseChanged();
  return true;
}

void TagSession::resetGameTimeState() {
  gameStartAnchorMs_ = -1;
  for (int quarterIndex = 0; quarterIndex < kQuarterCount; ++quarterIndex) {
    closedQuarters_[quarterIndex] = ClosedQuarterSpan{};
  }
  commitQuarterPhase(QuarterPhase::NotStarted, -1, 0);
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

bool TagSession::startGame(qint64 anchorMs) {
  if (quarterPhase_ != QuarterPhase::NotStarted) return false;
  if (anchorMs < 0) anchorMs = 0;

  GameTag anchorTag;
  anchorTag.setMainEventKey(TagEvent::unlistedKey(
      QString::fromLatin1(EventDefaults::TimeCodes::kStartAnchor)));
  anchorTag.markMs = anchorMs;
  anchorTag.startMs = anchorMs;
  anchorTag.endMs = anchorMs + kStartAnchorDurationMs;
  anchorTag.period = EventDefaults::quarterCode(0);
  anchorTag.intervalManuallyEdited = true;
  addTag(anchorTag);
  return true;
}

bool TagSession::advanceQuarter(qint64 nowMs) {
  if (quarterPhase_ != QuarterPhase::QuarterInProgress) return false;
  if (currentQuarterIndex_ < 0 || currentQuarterIndex_ >= kQuarterCount) return false;

  const int closingIndex = currentQuarterIndex_;
  const qint64 quarterStartMs = currentQuarterStartMs_;
  const qint64 endMs = nowMs >= quarterStartMs ? nowMs : quarterStartMs;

  GameTag quarterTag;
  const QString quarterCode = EventDefaults::quarterCode(closingIndex);
  quarterTag.setMainEventKey(TagEvent::unlistedKey(quarterCode));
  quarterTag.markMs = quarterStartMs;
  quarterTag.startMs = quarterStartMs;
  quarterTag.endMs = endMs;
  quarterTag.period = quarterCode;
  quarterTag.intervalManuallyEdited = true;
  addTag(quarterTag);
  return true;
}

QString TagSession::currentPeriodLabel() const {
  if (quarterPhase_ == QuarterPhase::GameEnded) {
    return EventDefaults::quarterCode(kQuarterCount - 1);
  }
  if (quarterPhase_ == QuarterPhase::QuarterInProgress &&
      currentQuarterIndex_ >= 0 && currentQuarterIndex_ < kQuarterCount) {
    return EventDefaults::quarterCode(currentQuarterIndex_);
  }
  return QString();
}
