#include "ExportClipBuilder.h"
#include "ShootoutOverlay.h"
#include "ShootoutState.h"
#include "TagSession.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QTest>

namespace {

using Marker = ShootoutState::MarkerKind;

const QString kConverted = QStringLiteral("Converted");
const QString kMissed = QStringLiteral("Missed");

QString outcomeChain(const QString& outcome) {
    if (outcome.isEmpty()) return {};
    return QStringLiteral("Side \u2192 ") + outcome;
}

ShootoutState::SourceEvent attempt(int order,
                                   qint64 markMs,
                                   const char* team,
                                   const QString& outcome) {
    ShootoutState::SourceEvent event;
    event.tagId = static_cast<quint64>(order + 1);
    event.markMs = markMs;
    event.teamKey = QString::fromLatin1(team);
    event.followUpEvent = outcomeChain(outcome);
    event.sourceOrder = order;
    return event;
}

QVector<ShootoutState::SourceEvent> alternatingFromHome(const QStringList& outcomes) {
    QVector<ShootoutState::SourceEvent> events;
    events.reserve(outcomes.size());
    for (int index = 0; index < outcomes.size(); ++index) {
        events.append(attempt(index,
                              (index + 1) * 1000,
                              index % 2 == 0 ? "Home" : "Away",
                              outcomes.at(index)));
    }
    return events;
}

ShootoutState::Snapshot revealAt(const ShootoutState& state,
                                 qint64 markMs,
                                 AppLocale::Language language = AppLocale::Language::English) {
    return state.snapshotRevealedThrough(markMs, language);
}

QString markerList(const QVector<Marker>& markers) {
    QStringList parts;
    parts.reserve(markers.size());
    for (const Marker marker : markers) {
        parts.append(QString::number(static_cast<int>(marker)));
    }
    return parts.join(QLatin1Char(','));
}

void expectMarkers(const QVector<Marker>& actual, const QVector<Marker>& expected) {
    if (actual == expected) return;
    QFAIL(qPrintable(QStringLiteral("markers [%1], expected [%2]")
                         .arg(markerList(actual), markerList(expected))));
}

QVector<Marker> regulation(std::initializer_list<Marker> prefix) {
    QVector<Marker> markers(ShootoutState::kRegulationRounds, Marker::NotTaken);
    int index = 0;
    for (const Marker marker : prefix) {
        markers[index] = marker;
        ++index;
    }
    return markers;
}

TagSession::GameTag storedTag(TagSession& session, TagSession::GameTag tag) {
    session.addTag(tag);
    return session.tags().constLast();
}

PresentationQueue::Clip presentationClip(const TagSession::GameTag& tag) {
    PresentationQueue::Clip clip;
    clip.tagId = tag.id;
    clip.markMs = tag.markMs;
    clip.startMs = tag.startMs;
    clip.endMs = tag.endMs;
    clip.mainEvent = tag.mainEvent;
    clip.followUpEvent = tag.followUpEvent;
    clip.team = tag.team;
    return clip;
}

bool imageContainsColor(const QImage& image, const QColor& expected) {
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor pixel = image.pixelColor(x, y);
            if (pixel.alpha() < 200) continue;
            if (qAbs(pixel.red() - expected.red()) < 28
                && qAbs(pixel.green() - expected.green()) < 28
                && qAbs(pixel.blue() - expected.blue()) < 28) {
                return true;
            }
        }
    }
    return false;
}

ShootoutOverlayFrame demoFrame() {
    ShootoutOverlayFrame frame;
    frame.homeName = QStringLiteral("UC");
    frame.awayName = QStringLiteral("OR");
    frame.homeColorHex = QStringLiteral("#3c5ac8");
    frame.awayColorHex = QStringLiteral("#c83c3c");
    frame.roundLabel = QStringLiteral("Ronda 3");
    frame.homeConverted = 2;
    frame.awayConverted = 1;
    frame.homeMarkers = regulation(
        {Marker::Converted, Marker::Converted, Marker::InProgress});
    frame.awayMarkers = regulation({Marker::Converted, Marker::Missed});
    return frame;
}

ShootoutOverlayFrame worstCaseFrame() {
    ShootoutOverlayFrame frame;
    frame.homeName = QStringLiteral(" Aleksandra Wiśniewska-Kowalczyk\nUniversidad Católica ");
    frame.awayName = QStringLiteral("J");
    frame.homeColorHex = QStringLiteral("#000000");
    frame.awayColorHex = QStringLiteral("#fef9c3");
    frame.roundLabel = QStringLiteral("Muerte súbita 12");
    frame.homeConverted = 13;
    frame.awayConverted = 1;
    frame.homeMarkers = QVector<Marker>(13, Marker::Converted);
    frame.awayMarkers = regulation(
        {Marker::Missed, Marker::Unknown, Marker::Converted, Marker::Missed, Marker::InProgress});
    frame.awayMarkers += QVector<Marker>(8, Marker::Missed);
    return frame;
}

ShootoutOverlayFrame oneAttemptFrame() {
    ShootoutOverlayFrame frame;
    frame.homeName = QStringLiteral("UC");
    frame.awayName = QStringLiteral("OR");
    frame.homeColorHex = QStringLiteral("#3c5ac8");
    frame.awayColorHex = QStringLiteral("#c83c3c");
    frame.roundLabel = QStringLiteral("Ronda 1");
    frame.homeMarkers = regulation({Marker::InProgress});
    frame.awayMarkers = regulation({});
    return frame;
}

ShootoutOverlayFrame scriptFrame() {
    ShootoutOverlayFrame frame;
    frame.homeName = QStringLiteral("Đặng Thị Ngọc Hân");
    frame.awayName = QStringLiteral("🦊 Fox");
    frame.homeColorHex = QStringLiteral("#facc15");
    frame.awayColorHex = QStringLiteral("#14532d");
    frame.roundLabel = QStringLiteral("Ronda 1");
    frame.homeConverted = 1;
    frame.awayConverted = 0;
    frame.homeMarkers = regulation({Marker::Converted});
    frame.awayMarkers = regulation({Marker::Missed});
    return frame;
}

QImage stageOnPitch(const QImage& plate, const QSize& frameSize) {
    QImage canvas(frameSize, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(QColor(31, 107, 58));
    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);

    const int left = qMax(8, frameSize.width() / 80);
    const int scoreboardTop = qMax(8, qRound(frameSize.height() * 0.065));
    const int scoreboardWidth = qMin(frameSize.width() - (left * 2), qRound(420.0 * frameSize.height() / 720.0));
    const int scoreboardHeight = qMax(28, qRound(40.0 * frameSize.height() / 720.0));
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(15, 23, 42, 140));
    painter.drawRoundedRect(left, scoreboardTop, scoreboardWidth, scoreboardHeight,
                            6, 6);
    painter.setPen(QColor(255, 255, 255));
    QFont scoreFont(QStringLiteral("Helvetica"));
    scoreFont.setPixelSize(qMax(12, scoreboardHeight / 2));
    scoreFont.setWeight(QFont::DemiBold);
    painter.setFont(scoreFont);
    painter.drawText(QRect(left, scoreboardTop, scoreboardWidth, scoreboardHeight),
                     Qt::AlignCenter,
                     QStringLiteral("UC   1  —  0   OR"));

    const int plateTop = scoreboardTop + scoreboardHeight + qMax(4, frameSize.height() / 90);
    painter.drawImage(left, plateTop, plate);
    painter.end();
    return canvas;
}

void writeBreakUiToggle(const QImage& demo,
                        const QImage& worst,
                        const QImage& emptyNames,
                        const QImage& oneAttempt,
                        const QImage& script) {
    const QString directory = qEnvironmentVariable("AVA_SHOOTOUT_BREAK_UI_DIR");
    if (directory.isEmpty()) return;
    QDir().mkpath(directory);
    const QSize pitch(1280, 720);
    stageOnPitch(demo, pitch).save(directory + QStringLiteral("/demo.png"));
    stageOnPitch(worst, pitch).save(directory + QStringLiteral("/worst.png"));
    stageOnPitch(emptyNames, pitch).save(directory + QStringLiteral("/empty.png"));
    stageOnPitch(oneAttempt, pitch).save(directory + QStringLiteral("/one.png"));
    stageOnPitch(script, pitch).save(directory + QStringLiteral("/script.png"));
    demo.save(directory + QStringLiteral("/demo-plate.png"));
    worst.save(directory + QStringLiteral("/worst-plate.png"));
    emptyNames.save(directory + QStringLiteral("/empty-plate.png"));
    oneAttempt.save(directory + QStringLiteral("/one-plate.png"));
    script.save(directory + QStringLiteral("/script-plate.png"));

    QFile page(directory + QStringLiteral("/index.html"));
    if (!page.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    page.write(R"html(<!DOCTYPE html>
<meta charset="utf-8">
<title>Shoot-out overlay break-ui</title>
<style>
  body { margin: 0; background: #1f6b3a; font-family: sans-serif; color: #111; }
  .toggle { position: fixed; bottom: 16px; left: 50%; transform: translateX(-50%);
            display: flex; gap: 4px; background: #d4d4d8; padding: 4px; border-radius: 999px; }
  .toggle button { border: 0; background: transparent; border-radius: 999px; padding: 6px 12px; cursor: pointer; }
  .toggle button.active { background: white; }
  figure { margin: 24px; }
  img { display: none; max-width: calc(100vw - 48px); height: auto; }
  img.active { display: block; }
</style>
<figure>
  <img id="demo" class="active" src="demo.png" alt="Demo shoot-out overlay">
  <img id="worst" src="worst.png" alt="Worst-case shoot-out overlay">
  <img id="empty" src="empty.png" alt="Empty shoot-out overlay">
  <img id="one" src="one.png" alt="Single-attempt shoot-out overlay">
  <img id="script" src="script.png" alt="Script shoot-out overlay">
</figure>
<div class="toggle">
  <button class="active" data-state="demo">Demo data</button>
  <button data-state="worst">Worst case</button>
  <button data-state="empty">Empty</button>
  <button data-state="one">One</button>
  <button data-state="script">Script</button>
</div>
<script>
  const buttons = document.querySelectorAll(".toggle button");
  const images = document.querySelectorAll("img");
  const params = new URLSearchParams(location.search);
  const initial = params.get("data") || "demo";
  function show(state) {
    buttons.forEach((item) => item.classList.toggle("active", item.dataset.state === state));
    images.forEach((item) => item.classList.toggle("active", item.id === state));
    params.set("data", state);
    history.replaceState(null, "", "?" + params.toString());
  }
  buttons.forEach((button) => button.addEventListener("click", () => show(button.dataset.state)));
  if (document.getElementById(initial)) show(initial);
</script>
)html");
}

}  // namespace

class ShootoutStateTest : public QObject {
    Q_OBJECT

private slots:
    void bestOfFiveEndsWhenTheTrailerCannotCatchUp();
    void bestOfFiveContinuesWhileASideCanStillLevel();
    void suddenDeathStartsOnlyAfterALevelFiveAndNeedsBothAttempts();
    void missingResultsReplaysAndBrokenAlternationStayHonest();
    void currentAttemptIsHiddenUntilItsMark();
    void laterClipsKeepShootoutGoalsOffTheMatchScoreboard();
    void overlayFitsWorstCaseNamesAndSuddenDeath();
};

void ShootoutStateTest::bestOfFiveEndsWhenTheTrailerCannotCatchUp() {
    const ShootoutState state(alternatingFromHome({
        kConverted, kMissed,
        kConverted, kMissed,
        kConverted, kMissed,
    }));

    const ShootoutState::Snapshot beforeTheClincher = revealAt(state, 5000);
    QCOMPARE(beforeTheClincher.homeConverted, 3);
    QCOMPARE(beforeTheClincher.awayConverted, 0);
    QVERIFY(!beforeTheClincher.decided);

    const ShootoutState::Snapshot clinched = revealAt(state, 6000, AppLocale::Language::Spanish);
    QCOMPARE(clinched.homeConverted, 3);
    QCOMPARE(clinched.awayConverted, 0);
    QVERIFY(clinched.decided);
    QCOMPARE(clinched.winner, TagSession::TeamSide::Home);
    QCOMPARE(clinched.roundLabel, QStringLiteral("Ronda 3"));
    QCOMPARE(clinched.roundKind, ShootoutState::RoundKind::Regulation);
    expectMarkers(clinched.homeMarkers, regulation(
        {Marker::Converted, Marker::Converted, Marker::Converted}));
    expectMarkers(clinched.awayMarkers, regulation(
        {Marker::Missed, Marker::Missed, Marker::Missed}));
}

void ShootoutStateTest::bestOfFiveContinuesWhileASideCanStillLevel() {
    const ShootoutState levelable(alternatingFromHome({
        kConverted, kMissed,
        kConverted, kMissed,
    }));
    const ShootoutState::Snapshot twoNil = revealAt(levelable, 4000);
    QCOMPARE(twoNil.homeConverted, 2);
    QCOMPARE(twoNil.awayConverted, 0);
    QVERIFY(!twoNil.decided);

    QVector<ShootoutState::SourceEvent> homeOnlyEvents;
    homeOnlyEvents.append(attempt(0, 1000, "Home", kConverted));
    homeOnlyEvents.append(attempt(1, 2000, "Home", kConverted));
    homeOnlyEvents.append(attempt(2, 3000, "Home", kConverted));
    const ShootoutState::Snapshot homeOnly = revealAt(ShootoutState(homeOnlyEvents), 3000);
    QCOMPARE(homeOnly.homeConverted, 3);
    QCOMPARE(homeOnly.awayConverted, 0);
    QVERIFY(!homeOnly.decided);
    expectMarkers(homeOnly.awayMarkers, regulation({}));
}

void ShootoutStateTest::suddenDeathStartsOnlyAfterALevelFiveAndNeedsBothAttempts() {
    QStringList allConverted;
    for (int index = 0; index < 10; ++index) allConverted.append(kConverted);
    const ShootoutState level(alternatingFromHome(allConverted));

    const ShootoutState::Snapshot afterFive = revealAt(level, 10000, AppLocale::Language::Spanish);
    QCOMPARE(afterFive.homeConverted, 5);
    QCOMPARE(afterFive.awayConverted, 5);
    QVERIFY(!afterFive.decided);
    QCOMPARE(afterFive.roundLabel, QStringLiteral("Ronda 5"));
    QCOMPARE(afterFive.homeMarkers.size(), 5);
    QCOMPARE(afterFive.awayMarkers.size(), 5);

    QVector<ShootoutState::SourceEvent> suddenDeathEvents = alternatingFromHome(allConverted);
    suddenDeathEvents.append(attempt(10, 11000, "Home", kConverted));
    const ShootoutState oneSuddenDeathShot(suddenDeathEvents);
    const ShootoutState::Snapshot homeScoredFirst =
        revealAt(oneSuddenDeathShot, 11000, AppLocale::Language::Spanish);
    QCOMPARE(homeScoredFirst.homeConverted, 6);
    QCOMPARE(homeScoredFirst.awayConverted, 5);
    QVERIFY(!homeScoredFirst.decided);
    QCOMPARE(homeScoredFirst.roundLabel, QStringLiteral("Muerte súbita 1"));
    QCOMPARE(homeScoredFirst.roundKind, ShootoutState::RoundKind::SuddenDeath);
    QCOMPARE(homeScoredFirst.homeMarkers.size(), 6);
    QCOMPARE(homeScoredFirst.homeMarkers.at(5), Marker::Converted);
    QCOMPARE(homeScoredFirst.awayMarkers.size(), 5);

    suddenDeathEvents.append(attempt(11, 12000, "Away", kMissed));
    const ShootoutState answered(suddenDeathEvents);
    const ShootoutState::Snapshot awayMissed = revealAt(answered, 12000);
    QCOMPARE(awayMissed.homeConverted, 6);
    QCOMPARE(awayMissed.awayConverted, 5);
    QVERIFY(awayMissed.decided);
    QCOMPARE(awayMissed.winner, TagSession::TeamSide::Home);
    QCOMPARE(awayMissed.roundLabel, QStringLiteral("Sudden death 1"));

    QVector<ShootoutState::SourceEvent> bothScored = alternatingFromHome(allConverted);
    bothScored.append(attempt(10, 11000, "Home", kConverted));
    bothScored.append(attempt(11, 12000, "Away", kConverted));
    const ShootoutState::Snapshot stillLevel = revealAt(ShootoutState(bothScored), 12000);
    QCOMPARE(stillLevel.homeConverted, 6);
    QCOMPARE(stillLevel.awayConverted, 6);
    QVERIFY(!stillLevel.decided);
    QCOMPARE(stillLevel.roundLabel, QStringLiteral("Sudden death 1"));

    QVector<ShootoutState::SourceEvent> bothMissed = alternatingFromHome(allConverted);
    bothMissed.append(attempt(10, 11000, "Home", kMissed));
    bothMissed.append(attempt(11, 12000, "Away", kMissed));
    const ShootoutState::Snapshot missesContinue = revealAt(ShootoutState(bothMissed), 12000);
    QCOMPARE(missesContinue.homeConverted, 5);
    QCOMPARE(missesContinue.awayConverted, 5);
    QVERIFY(!missesContinue.decided);
}

void ShootoutStateTest::missingResultsReplaysAndBrokenAlternationStayHonest() {
    QVector<ShootoutState::SourceEvent> outOfOrder;
    outOfOrder.append(attempt(0, 500, "Away", kMissed));
    outOfOrder.append(attempt(1, 100, "Home", kConverted));
    const ShootoutState::Snapshot ordered = revealAt(ShootoutState(outOfOrder), 500);
    QCOMPARE(ordered.homeConverted, 1);
    QCOMPARE(ordered.awayConverted, 0);
    expectMarkers(ordered.homeMarkers, regulation({Marker::Converted}));
    expectMarkers(ordered.awayMarkers, regulation({Marker::Missed}));

    QVector<ShootoutState::SourceEvent> sameInstant;
    sameInstant.append(attempt(1, 1000, "Home", kConverted));
    sameInstant.append(attempt(0, 1000, "Home", kMissed));
    const ShootoutState::Snapshot tieBrokenBySourceOrder = revealAt(ShootoutState(sameInstant), 1000);
    QCOMPARE(tieBrokenBySourceOrder.homeConverted, 1);
    expectMarkers(tieBrokenBySourceOrder.homeMarkers,
                  regulation({Marker::Missed, Marker::Converted}));
    QCOMPARE(tieBrokenBySourceOrder.roundNumber, 2);

    QVector<ShootoutState::SourceEvent> homeTwice;
    homeTwice.append(attempt(0, 1000, "Home", kConverted));
    homeTwice.append(attempt(1, 2000, "Home", kConverted));
    homeTwice.append(attempt(2, 3000, "Away", kMissed));
    const ShootoutState::Snapshot brokenAlternation = revealAt(ShootoutState(homeTwice), 3000);
    QCOMPARE(brokenAlternation.homeConverted, 2);
    QCOMPARE(brokenAlternation.awayConverted, 0);
    QVERIFY(!brokenAlternation.decided);
    QCOMPARE(brokenAlternation.roundLabel, QStringLiteral("Round 1"));
    expectMarkers(brokenAlternation.homeMarkers,
                  regulation({Marker::Converted, Marker::Converted}));
    expectMarkers(brokenAlternation.awayMarkers, regulation({Marker::Missed}));

    QVector<ShootoutState::SourceEvent> unknownThenGoal;
    unknownThenGoal.append(attempt(0, 1000, "Home", QString()));
    unknownThenGoal.append(attempt(1, 2000, "Home", QStringLiteral("No Goal")));
    unknownThenGoal.append(attempt(2, 3000, "Home", QStringLiteral("Convertido")));
    const ShootoutState::Snapshot unknowns =
        revealAt(ShootoutState(unknownThenGoal), 3000, AppLocale::Language::Spanish);
    QCOMPARE(unknowns.homeConverted, 1);
    QCOMPARE(unknowns.roundLabel, QStringLiteral("Ronda 3"));
    expectMarkers(unknowns.homeMarkers, regulation(
        {Marker::Unknown, Marker::Unknown, Marker::Converted}));

    QVector<ShootoutState::SourceEvent> replayThenRetake;
    replayThenRetake.append(attempt(0, 1000, "Home", QStringLiteral("Repite")));
    replayThenRetake.append(attempt(1, 2000, "Home", kConverted));
    replayThenRetake.append(attempt(2, 3000, "Away", QStringLiteral("No convertido")));
    const ShootoutState::Snapshot replay = revealAt(ShootoutState(replayThenRetake), 3000);
    QCOMPARE(replay.homeConverted, 1);
    QCOMPARE(replay.awayConverted, 0);
    expectMarkers(replay.homeMarkers, regulation({Marker::Converted}));
    expectMarkers(replay.awayMarkers, regulation({Marker::Missed}));
    QCOMPARE(replay.roundLabel, QStringLiteral("Round 1"));

    QVector<ShootoutState::SourceEvent> ignored;
    ignored.append(attempt(0, 1000, "Neutral", kConverted));
    ignored.append(attempt(1, 2000, "Home", kMissed));
    TagSession::GameTag goal;
    goal.mainEvent = QStringLiteral("Goal");
    goal.team = QStringLiteral("Home");
    goal.markMs = 500;
    goal.followUpEvent = QStringLiteral("Converted");
    TagSession session;
    session.addTag(goal);
    const ShootoutState fromTags = ShootoutState::fromGameTags(session.tags());
    QVERIFY(!fromTags.hasAttempts());

    const ShootoutState::Snapshot onlyRealAttempt = revealAt(ShootoutState(ignored), 2000);
    QCOMPARE(onlyRealAttempt.homeConverted, 0);
    expectMarkers(onlyRealAttempt.homeMarkers, regulation({Marker::Missed}));
    QCOMPARE(onlyRealAttempt.awayMarkers.size(), 5);
    QCOMPARE(onlyRealAttempt.awayConverted, 0);
}

void ShootoutStateTest::currentAttemptIsHiddenUntilItsMark() {
    QVector<ShootoutState::SourceEvent> events;
    events.append(attempt(0, 1000, "Home", kConverted));
    events.append(attempt(1, 2000, "Away", kMissed));
    events.append(attempt(2, 5000, "Home", kConverted));
    const ShootoutState state(events);

    ShootoutState::ClipWindow clip;
    clip.startMs = 3000;
    clip.endMs = 9000;
    clip.tagId = 3;
    clip.isShootoutClip = true;
    const QVector<ShootoutState::TimedSnapshot> phases =
        state.phasesForClip(clip, AppLocale::Language::English);
    QCOMPARE(phases.size(), 2);
    QVERIFY(phases.at(0).activationOffsetSeconds == 0.0);
    QCOMPARE(phases.at(1).activationOffsetSeconds, 2.0);

    const ShootoutState::Snapshot& hidden = phases.at(0).snapshot;
    QCOMPARE(hidden.homeConverted, 1);
    QCOMPARE(hidden.awayConverted, 0);
    QCOMPARE(hidden.roundLabel, QStringLiteral("Round 2"));
    expectMarkers(hidden.homeMarkers, regulation({Marker::Converted, Marker::InProgress}));
    expectMarkers(hidden.awayMarkers, regulation({Marker::Missed}));

    const ShootoutState::Snapshot& revealed = phases.at(1).snapshot;
    QCOMPARE(revealed.homeConverted, 2);
    QCOMPARE(revealed.roundLabel, QStringLiteral("Round 2"));
    expectMarkers(revealed.homeMarkers, regulation({Marker::Converted, Marker::Converted}));

    ShootoutState::ClipWindow remainder;
    remainder.startMs = 6000;
    remainder.endMs = 7000;
    remainder.isShootoutClip = false;
    const QVector<ShootoutState::TimedSnapshot> remainderPhases =
        state.phasesForClip(remainder, AppLocale::Language::Spanish);
    QCOMPARE(remainderPhases.size(), 1);
    QVERIFY(remainderPhases.at(0).activationOffsetSeconds == 0.0);
    QCOMPARE(remainderPhases.at(0).snapshot.homeConverted, 2);
    QCOMPARE(remainderPhases.at(0).snapshot.roundLabel, QStringLiteral("Ronda 2"));
    expectMarkers(remainderPhases.at(0).snapshot.homeMarkers,
                  regulation({Marker::Converted, Marker::Converted}));

    ShootoutState::ClipWindow before;
    before.startMs = 0;
    before.endMs = 500;
    const QVector<ShootoutState::TimedSnapshot> beforePhases =
        state.phasesForClip(before, AppLocale::Language::English);
    QVERIFY(beforePhases.isEmpty());

    ShootoutState::ClipWindow runsIntoShootout;
    runsIntoShootout.startMs = 0;
    runsIntoShootout.endMs = 1500;
    const QVector<ShootoutState::TimedSnapshot> overlapping =
        state.phasesForClip(runsIntoShootout, AppLocale::Language::English);
    QCOMPARE(overlapping.size(), 1);
    QCOMPARE(overlapping.at(0).activationOffsetSeconds, 1.0);
    QCOMPARE(overlapping.at(0).snapshot.homeConverted, 1);
    expectMarkers(overlapping.at(0).snapshot.homeMarkers, regulation({Marker::Converted}));
}

void ShootoutStateTest::laterClipsKeepShootoutGoalsOffTheMatchScoreboard() {
    TagSession session;
    session.setGameTeams(QStringLiteral("Universidad Católica"),
                         QStringLiteral("Old Georgians"),
                         QStringLiteral("#3c5ac8"),
                         QStringLiteral("#c83c3c"));

    TagSession::GameTag goal;
    goal.mainEvent = QStringLiteral("Goal");
    goal.team = QStringLiteral("Home");
    goal.markMs = 8000;
    goal.startMs = 6000;
    goal.endMs = 12000;
    goal.intervalManuallyEdited = true;
    const TagSession::GameTag storedGoal = storedTag(session, goal);

    TagSession::GameTag shootout;
    shootout.mainEvent = QStringLiteral("S.O.");
    shootout.team = QStringLiteral("Home");
    shootout.followUpEvent = QStringLiteral("Universidad Católica \u2192 Converted");
    shootout.markMs = 50000;
    shootout.startMs = 48000;
    shootout.endMs = 59000;
    shootout.intervalManuallyEdited = true;
    const TagSession::GameTag storedShootout = storedTag(session, shootout);

    TagSession::GameTag laterShot;
    laterShot.mainEvent = QStringLiteral("Shot");
    laterShot.team = QStringLiteral("Away");
    laterShot.markMs = 80000;
    laterShot.startMs = 78000;
    laterShot.endMs = 90000;
    laterShot.intervalManuallyEdited = true;
    const TagSession::GameTag storedShot = storedTag(session, laterShot);

    ExportClipBuilder::OverlayOptions options;
    options.language = AppLocale::Language::Spanish;
    options.includeScoreboardOverlay = true;
    const QVector<ClipSegment> segments = ExportClipBuilder::buildClipSegments(
        &session,
        {presentationClip(storedGoal), presentationClip(storedShootout), presentationClip(storedShot)},
        options);
    QCOMPARE(segments.size(), 3);

    QVERIFY(segments.at(0).shootouts.isEmpty());
    QCOMPARE(segments.at(0).scoreboards.size(), 2);
    QCOMPARE(segments.at(0).scoreboards.at(1).scoreboard.homeGoals, 1);

    QCOMPARE(segments.at(1).scoreboards.size(), 1);
    QCOMPARE(segments.at(1).scoreboards.at(0).scoreboard.homeGoals, 1);
    QCOMPARE(segments.at(1).scoreboards.at(0).scoreboard.awayGoals, 0);
    QCOMPARE(segments.at(1).shootouts.size(), 2);
    QCOMPARE(segments.at(1).shootouts.at(0).frame.homeConverted, 0);
    QCOMPARE(segments.at(1).shootouts.at(0).frame.roundLabel, QStringLiteral("Ronda 1"));
    QCOMPARE(segments.at(1).shootouts.at(1).frame.homeConverted, 1);
    QCOMPARE(segments.at(1).shootouts.at(1).frame.homeName, QStringLiteral("Universidad Católica"));
    QCOMPARE(segments.at(1).shootouts.at(1).frame.awayName, QStringLiteral("Old Georgians"));

    QCOMPARE(segments.at(2).scoreboards.at(0).scoreboard.homeGoals, 1);
    QCOMPARE(segments.at(2).shootouts.size(), 1);
    QCOMPARE(segments.at(2).shootouts.at(0).frame.homeConverted, 1);
    QCOMPARE(segments.at(2).shootouts.at(0).frame.awayConverted, 0);

    options.includeScoreboardOverlay = false;
    const QVector<ClipSegment> withoutBoard = ExportClipBuilder::buildClipSegments(
        &session, {presentationClip(storedShootout)}, options);
    QVERIFY(withoutBoard.at(0).scoreboards.isEmpty());
    QVERIFY(withoutBoard.at(0).shootouts.isEmpty());
}

void ShootoutStateTest::overlayFitsWorstCaseNamesAndSuddenDeath() {
    const ShootoutOverlayFrame demo = demoFrame();
    const ShootoutOverlayFrame worst = worstCaseFrame();
    const ShootoutOverlayFrame oneAttempt = oneAttemptFrame();
    const ShootoutOverlayFrame script = scriptFrame();
    ShootoutOverlayFrame emptyNames;
    emptyNames.homeName = QStringLiteral("   ");
    emptyNames.awayName = QString();
    emptyNames.awayColorHex = QStringLiteral("not-a-color");
    emptyNames.homeMarkers = regulation({});
    emptyNames.awayMarkers = regulation({});

    const int landscapeCap = 1152;
    const QImage demoImage = ShootoutOverlay::render(demo, 1.0, landscapeCap);
    const QImage demoLarge = ShootoutOverlay::render(demo, 2.0, 2400);
    const QImage demoSmall = ShootoutOverlay::render(demo, 0.5, 640);
    const QImage worstWide = ShootoutOverlay::render(worst, 1.0, landscapeCap);
    const QImage worstNarrow = ShootoutOverlay::render(worst, 1.0, 320);
    const QImage worstPortrait = ShootoutOverlay::render(worst, 1.5, 547);
    const QImage worstZoom = ShootoutOverlay::render(worst, 2.0, landscapeCap);
    const QImage emptyImage = ShootoutOverlay::render(emptyNames, 1.0, landscapeCap);
    const QImage oneImage = ShootoutOverlay::render(oneAttempt, 1.0, landscapeCap);
    const QImage scriptImage = ShootoutOverlay::render(script, 1.0, landscapeCap);

    QVERIFY(!demoImage.isNull());
    QVERIFY(!worstWide.isNull());
    QVERIFY(!worstNarrow.isNull());
    QVERIFY(!emptyImage.isNull());
    QVERIFY(demoImage.width() <= 1280);
    QVERIFY(worstWide.width() <= 1280);
    QVERIFY(worstNarrow.width() <= 320);
    QVERIFY(worstNarrow.width() > 0);
    QVERIFY(worstPortrait.width() <= 547);
    QVERIFY(worstZoom.width() <= landscapeCap);
    QVERIFY(demoLarge.width() > demoImage.width());
    QVERIFY(demoSmall.width() < demoImage.width());
    QVERIFY(worstWide.height() < demoImage.height() * 3);
    QVERIFY(emptyImage.height() < demoImage.height() * 2);
    QVERIFY(demoImage.pixelColor(0, 0).alpha() < 30);
    QVERIFY(imageContainsColor(demoImage, QColor(QStringLiteral("#3c5ac8"))));
    QVERIFY(imageContainsColor(demoImage, QColor(QStringLiteral("#c83c3c"))));
    QVERIFY(imageContainsColor(worstNarrow, QColor(QStringLiteral("#fef9c3"))));
    QVERIFY(imageContainsColor(emptyImage, QColor(248, 113, 113)));
    bool blackSwatchHasLightRim = false;
    const int rimBottom = qMin(worstWide.height(), (worstWide.height() * 2) / 3);
    for (int y = worstWide.height() / 3; y < rimBottom; ++y) {
        for (int x = 6; x < 22; ++x) {
            const QColor pixel = worstWide.pixelColor(x, y);
            if (pixel.alpha() > 180 && pixel.red() > 190 && pixel.green() > 190 && pixel.blue() > 190) {
                blackSwatchHasLightRim = true;
            }
        }
    }
    QVERIFY(blackSwatchHasLightRim);

    writeBreakUiToggle(demoImage, worstWide, emptyImage, oneImage, scriptImage);
    const QString directory = qEnvironmentVariable("AVA_SHOOTOUT_BREAK_UI_DIR");
    if (!directory.isEmpty()) {
        worstNarrow.save(directory + QStringLiteral("/worst-320.png"));
        worstPortrait.save(directory + QStringLiteral("/worst-portrait.png"));
        worstZoom.save(directory + QStringLiteral("/worst-zoom.png"));
        stageOnPitch(worstNarrow, QSize(320, 568)).save(directory + QStringLiteral("/worst-320-pitch.png"));
        stageOnPitch(worstPortrait, QSize(608, 1080)).save(directory + QStringLiteral("/worst-portrait-pitch.png"));
    }
}

int runShootoutStateTests(int argc, char** argv) {
    ShootoutStateTest tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "ShootoutStateTests.moc"
