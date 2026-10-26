#include "ShootoutState.h"

#include "EventCatalog.h"

#include <algorithm>

namespace {

ShootoutState::Outcome outcomeFromChain(const TagEvent::FollowUpChain& chain) {
    ShootoutState::Outcome matched = ShootoutState::Outcome::Unknown;
    bool found = false;
    const int used = chain.size();
    for (int index = 0; index < used; ++index) {
        switch (EventCatalog::shootoutAttemptResult(chain.at(index))) {
        case EventCatalog::ShootoutAttemptResult::Converted:
            matched = ShootoutState::Outcome::Converted;
            found = true;
            break;
        case EventCatalog::ShootoutAttemptResult::Missed:
            matched = ShootoutState::Outcome::Missed;
            found = true;
            break;
        case EventCatalog::ShootoutAttemptResult::Replay:
            matched = ShootoutState::Outcome::Replay;
            found = true;
            break;
        case EventCatalog::ShootoutAttemptResult::None:
            break;
        }
    }
    if (!found) return ShootoutState::Outcome::Unknown;
    return matched;
}

bool isKnownResult(ShootoutState::Outcome outcome) {
    return outcome == ShootoutState::Outcome::Converted
        || outcome == ShootoutState::Outcome::Missed;
}

int convertedCount(const QVector<ShootoutState::Outcome>& outcomes) {
    int converted = 0;
    for (const ShootoutState::Outcome outcome : outcomes) {
        if (outcome == ShootoutState::Outcome::Converted) ++converted;
    }
    return converted;
}

int unknownCount(const QVector<ShootoutState::Outcome>& outcomes) {
    int unknown = 0;
    for (const ShootoutState::Outcome outcome : outcomes) {
        if (outcome == ShootoutState::Outcome::Unknown) ++unknown;
    }
    return unknown;
}

bool regulationKnownAndTied(const QVector<ShootoutState::Outcome>& homeSlots,
                            const QVector<ShootoutState::Outcome>& awaySlots) {
    if (homeSlots.size() < ShootoutState::kRegulationRounds
        || awaySlots.size() < ShootoutState::kRegulationRounds) {
        return false;
    }
    int homeGoals = 0;
    int awayGoals = 0;
    for (int index = 0; index < ShootoutState::kRegulationRounds; ++index) {
        const ShootoutState::Outcome homeOutcome = homeSlots.at(index);
        const ShootoutState::Outcome awayOutcome = awaySlots.at(index);
        if (!isKnownResult(homeOutcome) || !isKnownResult(awayOutcome)) return false;
        if (homeOutcome == ShootoutState::Outcome::Converted) ++homeGoals;
        if (awayOutcome == ShootoutState::Outcome::Converted) ++awayGoals;
    }
    return homeGoals == awayGoals;
}

struct ContestStatus {
    int homeConverted = 0;
    int awayConverted = 0;
    bool decided = false;
    TagSession::TeamSide winner = TagSession::TeamSide::Unspecified;
};

ContestStatus contestFromSlots(const QVector<ShootoutState::Outcome>& homeSlots,
                               const QVector<ShootoutState::Outcome>& awaySlots) {
    ContestStatus status;
    status.homeConverted = convertedCount(homeSlots);
    status.awayConverted = convertedCount(awaySlots);

    if (regulationKnownAndTied(homeSlots, awaySlots)) {
        const int homeExtra = homeSlots.size() - ShootoutState::kRegulationRounds;
        const int awayExtra = awaySlots.size() - ShootoutState::kRegulationRounds;
        if (homeExtra == awayExtra && homeExtra > 0
            && status.homeConverted != status.awayConverted) {
            status.decided = true;
            status.winner = status.homeConverted > status.awayConverted
                ? TagSession::TeamSide::Home
                : TagSession::TeamSide::Away;
        }
        return status;
    }

    const int homeRemaining = qMax(
        0, ShootoutState::kRegulationRounds - static_cast<int>(homeSlots.size()));
    const int awayRemaining = qMax(
        0, ShootoutState::kRegulationRounds - static_cast<int>(awaySlots.size()));
    const int homeMaximum = status.homeConverted + unknownCount(homeSlots) + homeRemaining;
    const int awayMaximum = status.awayConverted + unknownCount(awaySlots) + awayRemaining;
    if (status.homeConverted > awayMaximum) {
        status.decided = true;
        status.winner = TagSession::TeamSide::Home;
    } else if (status.awayConverted > homeMaximum) {
        status.decided = true;
        status.winner = TagSession::TeamSide::Away;
    }
    return status;
}

QVector<ShootoutState::Outcome> compactPrefix(
    const QVector<QPair<int, ShootoutState::Outcome>>& placed) {
    QVector<ShootoutState::Outcome> prefix;
    prefix.reserve(placed.size());
    for (int expectedIndex = 0; expectedIndex < placed.size(); ++expectedIndex) {
        const auto match = std::find_if(
            placed.cbegin(), placed.cend(),
            [expectedIndex](const QPair<int, ShootoutState::Outcome>& item) {
                return item.first == expectedIndex;
            });
        if (match == placed.cend()) break;
        prefix.append(match->second);
    }
    return prefix;
}

ShootoutState::MarkerKind markerForOutcome(ShootoutState::Outcome outcome) {
    switch (outcome) {
    case ShootoutState::Outcome::Converted:
        return ShootoutState::MarkerKind::Converted;
    case ShootoutState::Outcome::Missed:
        return ShootoutState::MarkerKind::Missed;
    case ShootoutState::Outcome::Unknown:
        return ShootoutState::MarkerKind::Unknown;
    case ShootoutState::Outcome::Replay:
        return ShootoutState::MarkerKind::NotTaken;
    }
    return ShootoutState::MarkerKind::Unknown;
}

/// When the attempt's result may appear. The mark is the S.O. press. Factory
/// S.O. lead is short, so that press sits near the start of the attempt clip.
/// A goal score stays up for the scoring goal's lag; the attempt uses that
/// same hold at the end of its own clip. If the mark is already inside that
/// hold, the mark is the outcome, the same instant a goal mark flips the
/// match score. Callers that only know the mark leave the interval unusable
/// and the outcome stays the mark.
qint64 attemptOutcomeMs(qint64 markMs, qint64 intervalStartMs, qint64 intervalEndMs) {
    const bool intervalContainsMark = intervalEndMs > intervalStartMs
        && markMs >= intervalStartMs
        && markMs <= intervalEndMs;
    if (!intervalContainsMark) return markMs;

    const EventCatalog::Node* scoringGoal = EventCatalog::scoringGoalMainEvent();
    const qint64 holdMs = scoringGoal != nullptr ? scoringGoal->lagMs : 0;
    if (holdMs <= 0) return markMs;
    if ((intervalEndMs - markMs) <= holdMs) return markMs;

    const qint64 nearEndMs = intervalEndMs - holdMs;
    if (nearEndMs <= intervalStartMs) return markMs;
    return nearEndMs;
}

QVector<ShootoutState::MarkerKind> markersForSide(
    const QVector<QPair<int, ShootoutState::Outcome>>& revealed,
    int inProgressSlot) {
    int highestIndex = -1;
    for (const QPair<int, ShootoutState::Outcome>& placed : revealed) {
        highestIndex = qMax(highestIndex, placed.first);
    }
    if (inProgressSlot >= 0) highestIndex = qMax(highestIndex, inProgressSlot);

    const int count = qMax(ShootoutState::kRegulationRounds, highestIndex + 1);
    QVector<ShootoutState::MarkerKind> markers(count, ShootoutState::MarkerKind::NotTaken);
    for (const QPair<int, ShootoutState::Outcome>& placed : revealed) {
        if (placed.first >= 0 && placed.first < markers.size()) {
            markers[placed.first] = markerForOutcome(placed.second);
        }
    }
    if (inProgressSlot >= 0 && inProgressSlot < markers.size()
        && markers.at(inProgressSlot) == ShootoutState::MarkerKind::NotTaken) {
        markers[inProgressSlot] = ShootoutState::MarkerKind::InProgress;
    }
    return markers;
}

}  // namespace

QVector<ShootoutState::AnnotatedAttempt> ShootoutState::annotate(
    QVector<SourceEvent> events) {
    std::stable_sort(events.begin(), events.end(),
                     [](const ShootoutState::SourceEvent& first,
                        const ShootoutState::SourceEvent& second) {
        if (first.markMs != second.markMs) return first.markMs < second.markMs;
        return first.sourceOrder < second.sourceOrder;
    });

    QVector<ShootoutState::Outcome> homeSlots;
    QVector<ShootoutState::Outcome> awaySlots;
    QVector<AnnotatedAttempt> annotated;
    annotated.reserve(events.size());

    for (const ShootoutState::SourceEvent& event : events) {
        const TagSession::TeamSide team = TagSession::teamSideFromKey(event.teamKey);
        if (team == TagSession::TeamSide::Unspecified) continue;

        const ShootoutState::Outcome outcome = outcomeFromChain(event.followUps);
        QVector<ShootoutState::Outcome>& ownSlots =
            team == TagSession::TeamSide::Home ? homeSlots : awaySlots;
        const int openSlot = ownSlots.size();

        AnnotatedAttempt attempt;
        attempt.tagId = event.tagId;
        attempt.markMs = event.markMs;
        attempt.team = team;
        attempt.outcome = outcome;
        attempt.displaySlotIndex = openSlot;
        attempt.roundKind = ShootoutState::RoundKind::Regulation;
        attempt.roundNumber = openSlot + 1;
        attempt.outcomeMs = attemptOutcomeMs(
            event.markMs, event.intervalStartMs, event.intervalEndMs);
        if (openSlot >= ShootoutState::kRegulationRounds
            && regulationKnownAndTied(homeSlots, awaySlots)) {
            attempt.roundKind = ShootoutState::RoundKind::SuddenDeath;
            attempt.roundNumber = openSlot - ShootoutState::kRegulationRounds + 1;
        }

        if (outcome == ShootoutState::Outcome::Replay) {
            attempt.occupiesSlot = false;
        } else {
            attempt.occupiesSlot = true;
            ownSlots.append(outcome);
        }
        annotated.append(attempt);
    }
    return annotated;
}

ShootoutState::ShootoutState(QVector<SourceEvent> events)
    : attempts_(annotate(std::move(events))) {}

ShootoutState ShootoutState::fromGameTags(const QVector<TagSession::GameTag>& tags) {
    QVector<SourceEvent> events;
    events.reserve(tags.size());
    int sourceOrder = 0;
    for (const TagSession::GameTag& tag : tags) {
        if (!EventCatalog::isShootout(tag.mainEventId)) continue;
        SourceEvent event;
        event.tagId = tag.id;
        event.markMs = tag.markMs;
        event.intervalStartMs = tag.startMs;
        event.intervalEndMs = tag.endMs;
        event.teamKey = tag.team;
        event.followUps = tag.followUpChain();
        event.sourceOrder = sourceOrder;
        ++sourceOrder;
        events.append(event);
    }
    return ShootoutState(std::move(events));
}

qint64 ShootoutState::firstAttemptMarkMs() const {
    if (attempts_.isEmpty()) return -1;
    return attempts_.first().markMs;
}

QString ShootoutState::roundLabel(RoundKind kind,
                                  int roundNumber,
                                  AppLocale::Language language) {
    const char* key = kind == RoundKind::SuddenDeath
        ? "shootout.sudden_death"
        : "shootout.round";
    return AppLocale::trUiForLanguage(key, language).arg(roundNumber);
}

ShootoutState::Snapshot ShootoutState::snapshotRevealedThrough(
    qint64 revealThroughMs,
    AppLocale::Language language,
    quint64 highlightTagId,
    qint64 highlightThroughMs) const {
    Snapshot snapshot;
    if (attempts_.isEmpty()) return snapshot;

    QVector<QPair<int, Outcome>> revealedHome;
    QVector<QPair<int, Outcome>> revealedAway;
    const AnnotatedAttempt* latestRevealed = nullptr;
    const AnnotatedAttempt* inProgress = nullptr;

    for (const AnnotatedAttempt& attempt : attempts_) {
        if (attempt.outcomeMs <= revealThroughMs) {
            latestRevealed = &attempt;
            if (!attempt.occupiesSlot) continue;
            QVector<QPair<int, Outcome>>& revealed =
                attempt.team == TagSession::TeamSide::Home ? revealedHome : revealedAway;
            revealed.append(qMakePair(attempt.displaySlotIndex, attempt.outcome));
            continue;
        }
        const bool matchesClip = highlightTagId != 0 && attempt.tagId == highlightTagId;
        const bool fallsInWindow = attempt.markMs <= highlightThroughMs
            || attempt.outcomeMs <= highlightThroughMs;
        if (inProgress == nullptr && (matchesClip || fallsInWindow)) {
            inProgress = &attempt;
        }
    }

    const QVector<Outcome> homePrefix = compactPrefix(revealedHome);
    const QVector<Outcome> awayPrefix = compactPrefix(revealedAway);
    const ContestStatus contest = contestFromSlots(homePrefix, awayPrefix);
    snapshot.homeConverted = contest.homeConverted;
    snapshot.awayConverted = contest.awayConverted;
    snapshot.decided = contest.decided;
    snapshot.winner = contest.winner;

    const int homeInProgress = (inProgress != nullptr
                                && inProgress->team == TagSession::TeamSide::Home)
        ? inProgress->displaySlotIndex
        : -1;
    const int awayInProgress = (inProgress != nullptr
                                && inProgress->team == TagSession::TeamSide::Away)
        ? inProgress->displaySlotIndex
        : -1;
    snapshot.homeMarkers = markersForSide(revealedHome, homeInProgress);
    snapshot.awayMarkers = markersForSide(revealedAway, awayInProgress);

    const AnnotatedAttempt* labeled = inProgress != nullptr ? inProgress : latestRevealed;
    if (labeled == nullptr) return snapshot;

    snapshot.visible = true;
    snapshot.roundKind = labeled->roundKind;
    snapshot.roundNumber = labeled->roundNumber;
    snapshot.roundLabel = roundLabel(labeled->roundKind, labeled->roundNumber, language);
    snapshot.attemptTeam = labeled->team;
    return snapshot;
}

QVector<ShootoutState::TimedSnapshot> ShootoutState::phasesForClip(
    const ClipWindow& clip,
    AppLocale::Language language) const {
    QVector<TimedSnapshot> phases;
    if (attempts_.isEmpty()) return phases;
    if (clip.endMs < clip.startMs) return phases;

    const qint64 firstMark = attempts_.first().markMs;
    const bool clipReachesShootout = clip.endMs >= firstMark || clip.isShootoutClip;
    if (!clipReachesShootout) return phases;

    const bool showFromClipStart = clip.isShootoutClip || clip.startMs >= firstMark;
    QVector<qint64> boundaries;
    if (showFromClipStart) boundaries.append(clip.startMs);
    for (const AnnotatedAttempt& attempt : attempts_) {
        // A clip that starts before the shoot-out still picks the board up at
        // the attempt's mark. The result itself waits for the outcome, which
        // may be later in this clip or not in it at all.
        if (!showFromClipStart
            && attempt.markMs > clip.startMs
            && attempt.markMs <= clip.endMs
            && attempt.markMs != attempt.outcomeMs) {
            boundaries.append(attempt.markMs);
        }
        if (attempt.outcomeMs > clip.startMs && attempt.outcomeMs <= clip.endMs) {
            boundaries.append(attempt.outcomeMs);
        }
    }
    std::sort(boundaries.begin(), boundaries.end());
    if (boundaries.isEmpty()) return phases;

    bool haveBoundary = false;
    qint64 previousBoundary = 0;
    for (const qint64 boundary : boundaries) {
        if (haveBoundary && boundary == previousBoundary) continue;
        haveBoundary = true;
        previousBoundary = boundary;

        TimedSnapshot phase;
        const qint64 offsetMs = boundary - clip.startMs;
        phase.activationOffsetSeconds = offsetMs <= 0 ? 0.0 : offsetMs / 1000.0;
        phase.snapshot = snapshotRevealedThrough(
            boundary, language, clip.tagId, clip.endMs);
        if (!phase.snapshot.visible) continue;
        phases.append(phase);
    }
    return phases;
}
