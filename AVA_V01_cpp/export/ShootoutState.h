#pragma once

#include "AppLocale.h"
#include "TagSession.h"

#include <QString>
#include <QVector>
#include <QtGlobal>

/// Field-hockey shoot-out (S.O.) derived from tagged attempts.
///
/// Converted S.O. attempts are not match goals. This model never reads Goal tags
/// and never writes the match score. The main scoreboard stays on Goal tags only.
///
/// Attempts are the session's "S.O." tags, ordered by mark time (tag order breaks
/// ties). Each side's round number is how many of that side's slot-taking attempts
/// this one is. A missing opponent attempt is not invented, so two Home attempts
/// in a row are Ronda 1 then Ronda 2 for Home while Away is still on Ronda 1.
///
/// The model reads follow-up ids. Spanish words are parsed into those ids before
/// a tag is stored. The last matching id wins:
/// - Converted — goal, takes a slot
/// - Missed — miss, takes a slot
/// - Replay — void, does not take a slot and does not score; the retake is the
///   next slot-taking attempt by that side
/// - anything else, including an empty chain — Unknown: takes a slot, does not
///   score, drawn as a neutral dash once revealed
///
/// Tags whose team is not Home or Away are ignored.
///
/// Regulation is best of 5. It ends early when one side's confirmed goals exceed
/// the other's maximum, counting that side's unknown slots and untaken regulation
/// shots as goals they might still score. Sudden death starts only once both
/// sides have five known results and those five are level. In sudden death the
/// contest is decided only after both sides have taken the same number of extra
/// attempts and the scores differ. A side that has shot in the current sudden-death
/// round does not win before the other side shoots.
///
/// Spoiler rule for a clip: an attempt's result joins the score and the dot
/// history only when playback reaches its mark. Before that, if this clip is the
/// attempt (or the mark falls inside the clip), the round label names it and its
/// slot is in progress — empty of a result. Attempts already marked before the
/// clip starts are shown in full. On a later non-shoot-out clip the board shows
/// the revealed score and does not light a future attempt that this clip does not
/// contain.
class ShootoutState {
public:
    static constexpr int kRegulationRounds = 5;

    enum class Outcome {
        Converted,
        Missed,
        Replay,
        Unknown,
    };

    enum class RoundKind {
        Regulation,
        SuddenDeath,
    };

    enum class MarkerKind {
        NotTaken,
        InProgress,
        Converted,
        Missed,
        Unknown,
    };

    struct SourceEvent {
        quint64 tagId = 0;
        qint64 markMs = 0;
        QString teamKey;
        TagEvent::FollowUpChain followUps;
        int sourceOrder = 0;
    };

    struct Snapshot {
        bool visible = false;
        RoundKind roundKind = RoundKind::Regulation;
        int roundNumber = 0;
        QString roundLabel;
        int homeConverted = 0;
        int awayConverted = 0;
        QVector<MarkerKind> homeMarkers;
        QVector<MarkerKind> awayMarkers;
        bool decided = false;
        TagSession::TeamSide winner = TagSession::TeamSide::Unspecified;
        TagSession::TeamSide attemptTeam = TagSession::TeamSide::Unspecified;
    };

    struct ClipWindow {
        qint64 startMs = 0;
        qint64 endMs = 0;
        quint64 tagId = 0;
        bool isShootoutClip = false;
    };

    struct TimedSnapshot {
        double activationOffsetSeconds = 0.0;
        Snapshot snapshot;
    };

    ShootoutState() = default;
    explicit ShootoutState(QVector<SourceEvent> events);

    static ShootoutState fromGameTags(const QVector<TagSession::GameTag>& tags);

    bool hasAttempts() const { return !attempts_.isEmpty(); }
    qint64 firstAttemptMarkMs() const;

    /// Results with markMs <= \p revealThroughMs are included.
    /// The next not-yet-revealed attempt is in progress when its tag id is
    /// \p highlightTagId (non-zero) or its mark is <= \p highlightThroughMs.
    Snapshot snapshotRevealedThrough(qint64 revealThroughMs,
                                     AppLocale::Language language,
                                     quint64 highlightTagId = 0,
                                     qint64 highlightThroughMs = -1) const;

    /// Empty when the clip ends before the first attempt and is not itself an
    /// S.O. clip. Shoot-out clips, and any clip that starts once shoot-outs have
    /// begun, carry the board for the whole clip. A clip that only runs into the
    /// shoot-out shows the board from the first attempt inside it.
    QVector<TimedSnapshot> phasesForClip(const ClipWindow& clip,
                                         AppLocale::Language language) const;

    static QString roundLabel(RoundKind kind, int roundNumber, AppLocale::Language language);

private:
    struct AnnotatedAttempt {
        quint64 tagId = 0;
        qint64 markMs = 0;
        TagSession::TeamSide team = TagSession::TeamSide::Unspecified;
        Outcome outcome = Outcome::Unknown;
        bool occupiesSlot = false;
        int displaySlotIndex = 0;
        RoundKind roundKind = RoundKind::Regulation;
        int roundNumber = 0;
    };

    static QVector<AnnotatedAttempt> annotate(QVector<SourceEvent> events);

    QVector<AnnotatedAttempt> attempts_;
};
