#include "ExportClipBuilder.h"

#include "TagSession.h"

#include <algorithm>

namespace ExportClipBuilder {

QString teamDisplayName(const TagSession* session, const QString& teamKey) {
    if (teamKey == QStringLiteral("Home")) {
        return (session && !session->homeTeamName().isEmpty())
            ? session->homeTeamName()
            : AppLocale::trUi("export.team_home_default");
    }
    if (teamKey == QStringLiteral("Away")) {
        return (session && !session->awayTeamName().isEmpty())
            ? session->awayTeamName()
            : AppLocale::trUi("export.team_away_default");
    }
    return teamKey;
}

QString sanitizedFileNamePart(const QString& raw) {
    QString segment = raw.trimmed();
    const QString forbidden = QStringLiteral("\\/:*?\"<>|\r\n\t");
    for (QChar character : forbidden) {
        segment.replace(character, QLatin1Char('_'));
    }
    while (segment.contains(QStringLiteral("  "))) {
        segment.replace(QStringLiteral("  "), QStringLiteral(" "));
    }
    if (segment.isEmpty()) {
        return QStringLiteral("clip");
    }
    return segment;
}

QString eventLabelForExportFileName(const QString& canonicalEvent) {
    if (canonicalEvent == QStringLiteral("Special")) {
        return QStringLiteral("special");
    }
    return AppLocale::trEvent(canonicalEvent);
}

QString xmlReportBaseName(const TagSession* session) {
    const QString homeSegment =
        sanitizedFileNamePart(teamDisplayName(session, QStringLiteral("Home")));
    const QString awaySegment =
        sanitizedFileNamePart(teamDisplayName(session, QStringLiteral("Away")));
    const QString reportSegment =
        sanitizedFileNamePart(AppLocale::trUi("export.xml_report_filename_segment"));
    return QStringLiteral("%1 vs %2 - %3")
        .arg(homeSegment, awaySegment, reportSegment);
}

QString compilationBaseName(const TagSession* session,
                            const QVector<PresentationQueue::Clip>& clips) {
    const QString homeSegment =
        sanitizedFileNamePart(teamDisplayName(session, QStringLiteral("Home")));
    const QString awaySegment =
        sanitizedFileNamePart(teamDisplayName(session, QStringLiteral("Away")));

    QString sharedEvent;
    QString sharedTeam;
    bool mixedEvents = false;
    bool mixedTeams = false;
    for (int index = 0; index < clips.size(); ++index) {
        const PresentationQueue::Clip& clip = clips.at(index);
        if (index == 0) {
            sharedEvent = clip.mainEvent;
            sharedTeam = clip.team;
            continue;
        }
        if (clip.mainEvent != sharedEvent) mixedEvents = true;
        if (clip.team != sharedTeam) mixedTeams = true;
    }

    const QString eventSegment = (clips.isEmpty() || mixedEvents || sharedEvent.isEmpty())
        ? sanitizedFileNamePart(AppLocale::trUi("export.mixed_events_filename"))
        : sanitizedFileNamePart(eventLabelForExportFileName(sharedEvent));
    const QString teamSegment = (clips.isEmpty() || mixedTeams || sharedTeam.isEmpty())
        ? sanitizedFileNamePart(AppLocale::trUi("export.team_all"))
        : sanitizedFileNamePart(teamDisplayName(session, sharedTeam));

    return QStringLiteral("%1 vs %2 - %3 %4")
        .arg(homeSegment, awaySegment, eventSegment, teamSegment);
}

QVector<PresentationQueue::Clip> sortedClips(QVector<PresentationQueue::Clip> clips,
                                             SortOrder order) {
    if (order == SortOrder::ByTeamThenChronological) {
        std::sort(clips.begin(), clips.end(),
                  [](const PresentationQueue::Clip& first, const PresentationQueue::Clip& second) {
            if (first.team != second.team) {
                if (first.team == QStringLiteral("Home")) return true;
                if (second.team == QStringLiteral("Home")) return false;
                return first.team < second.team;
            }
            if (first.markMs != second.markMs) return first.markMs < second.markMs;
            return first.tagSessionIndex < second.tagSessionIndex;
        });
    } else {
        std::sort(clips.begin(), clips.end(),
                  [](const PresentationQueue::Clip& first, const PresentationQueue::Clip& second) {
            if (first.markMs != second.markMs) return first.markMs < second.markMs;
            return first.tagSessionIndex < second.tagSessionIndex;
        });
    }
    return clips;
}

QVector<ClipSegment> buildClipSegments(const TagSession* session,
                                       const QVector<PresentationQueue::Clip>& clips,
                                       const OverlayOptions& options) {
    QVector<ClipSegment> segments;
    segments.reserve(clips.size());

    const QString homeName = teamDisplayName(session, QStringLiteral("Home"));
    const QString awayName = teamDisplayName(session, QStringLiteral("Away"));
    const QString homeColorHex = session ? session->homeTeamColor() : QString();
    const QString awayColorHex = session ? session->awayTeamColor() : QString();
    const QVector<TagSession::GameTag> emptyTags;
    const auto& allTags = session ? session->tags() : emptyTags;
    const int totalClips = clips.size();

    for (int index = 0; index < totalClips; ++index) {
        const PresentationQueue::Clip& clip = clips.at(index);
        qint64 durationMs = clip.endMs - clip.startMs;
        if (durationMs < 500) durationMs = 500;

        QString primary;
        QString secondary;
        if (options.includeBottomOverlay) {
            const QString translatedEvent =
                AppLocale::trEventForLanguage(clip.mainEvent, options.language);
            primary = QStringLiteral("%1 - %2  %3 / %4")
                .arg(teamDisplayName(session, clip.team), translatedEvent)
                .arg(index + 1)
                .arg(totalClips);
            if (options.includeNotesOverlay && !clip.note.trimmed().isEmpty()) {
                secondary = clip.note.trimmed();
            }
        }

        QVector<TimedScoreboard> scoreboardPhases;
        if (options.includeScoreboardOverlay) {
            int initialHomeGoals = 0;
            int initialAwayGoals = 0;
            struct InClipGoal {
                qint64 positionMs;
                QString team;
            };
            QVector<InClipGoal> inClipGoals;

            for (const auto& tag : allTags) {
                if (tag.mainEvent != QStringLiteral("Goal")) continue;
                if (tag.positionMs <= clip.startMs) {
                    if (tag.team == QStringLiteral("Home")) ++initialHomeGoals;
                    else if (tag.team == QStringLiteral("Away")) ++initialAwayGoals;
                } else if (tag.positionMs <= clip.endMs) {
                    inClipGoals.append({tag.positionMs, tag.team});
                }
            }

            std::sort(inClipGoals.begin(), inClipGoals.end(),
                      [](const InClipGoal& first, const InClipGoal& second) {
                return first.positionMs < second.positionMs;
            });

            scoreboardPhases.append(
                {0.0,
                 {homeName, awayName, initialHomeGoals, initialAwayGoals, homeColorHex, awayColorHex,
                  session ? session->periodLabelAtTimestampMs(clip.startMs) : QString()}});

            int runningHome = initialHomeGoals;
            int runningAway = initialAwayGoals;
            for (const auto& goal : inClipGoals) {
                if (goal.team == QStringLiteral("Home")) ++runningHome;
                else if (goal.team == QStringLiteral("Away")) ++runningAway;

                const double offsetSeconds = (goal.positionMs - clip.startMs) / 1000.0;
                if (scoreboardPhases.last().activationOffsetSeconds == offsetSeconds) {
                    scoreboardPhases.last().scoreboard.homeGoals = runningHome;
                    scoreboardPhases.last().scoreboard.awayGoals = runningAway;
                    if (session) {
                        scoreboardPhases.last().scoreboard.periodLabel =
                            session->periodLabelAtTimestampMs(goal.positionMs);
                    }
                } else {
                    scoreboardPhases.append(
                        {offsetSeconds,
                         {homeName, awayName, runningHome, runningAway, homeColorHex, awayColorHex,
                          session ? session->periodLabelAtTimestampMs(goal.positionMs) : QString()}});
                }
            }
        }

        segments.append({clip.startMs, durationMs, primary, secondary, scoreboardPhases});
    }

    return segments;
}

} // namespace ExportClipBuilder
