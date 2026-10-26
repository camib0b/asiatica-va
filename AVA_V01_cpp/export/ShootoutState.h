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
/// Attempts are tags whose catalog event has isShootout set, ordered by mark
/// time (tag order breaks ties). Each side's round number is how many of that
/// side's slot-taking attempts this one is. A missing opponent attempt is not
/// invented, so two Home attempts
/// in a row are Ronda 1 then Ronda 2 for Home while Away is still on Ronda 1.
///
/// The model reads follow-up ids. Spanish words are parsed into those ids before
/// a tag is stored. The last follow-up whose catalog shootoutAttemptResult is
/// set wins:
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
/// Spoiler rule: an attempt's result joins the score and the dot history only
/// when playback reaches that attempt's outcome time, not when its clip starts.
///
/// The match scoreboard flips at a Goal tag's mark. Lead and lag do not move
/// that instant; they only place the clip around it. Factory Goal is a long
/// lead and a short lag, so on a goal clip the new score appears near the end
/// and stays up for the goal's lag. A penalty-corner clip flips at that same
/// Goal mark (captured when the goal is registered), which falls inside the
/// corner clip when the ball crosses, not at the corner clip's first frame.
///
/// An S.O. attempt has no second timestamp. The stored mark is the S.O. press.
/// Factory S.O. lead is short and the lag is the attempt itself, so the mark
/// sits near the start of the clip. Revealing there would show the finish for
/// the whole attempt. The outcome time is therefore:
/// - the mark, when it already lies in the final hold of the attempt's own
///   clip (the scoring goal's lag — the tail a goal score stays on screen).
///   A long lead puts the tag there.
/// - otherwise one hold before the attempt clip ends. That uses the clip end
///   the tag already stores (mark + lag, unless trimmed) and keeps the result
///   on screen for the same tail as a goal. No extra field is required, so
///   older sessions and XML imports keep working.
///
/// Before the outcome, if this clip is the attempt or the attempt falls inside
/// the clip, the round label names it and its slot is in progress — empty of a
/// result. Attempts whose outcome is already at or before the clip start are
/// shown in full. On a later non-shoot-out clip the board shows the revealed
/// score and does not light a future attempt that this clip does not contain.
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
        /// Attempt clip from the tag. When \p intervalEndMs is not after
        /// \p intervalStartMs, or the mark lies outside that interval, the
        /// outcome time is \p markMs. Callers that only know the mark leave
        /// the end before the start.
        qint64 intervalStartMs = 0;
        qint64 intervalEndMs = -1;
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

    /// Results whose outcome time is <= \p revealThroughMs are included.
    /// The next not-yet-revealed attempt is in progress when its tag id is
    /// \p highlightTagId (non-zero), or its mark or outcome time is
    /// <= \p highlightThroughMs.
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
        /// Absolute time at which this attempt's result becomes visible.
        qint64 outcomeMs = 0;
    };

    static QVector<AnnotatedAttempt> annotate(QVector<SourceEvent> events);

    QVector<AnnotatedAttempt> attempts_;
};
