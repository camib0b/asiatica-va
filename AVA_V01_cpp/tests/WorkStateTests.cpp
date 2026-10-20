#include "EventDefaults.h"
#include "PendingClipNote.h"
#include "TagFilter.h"
#include "TagSession.h"

#include <QTest>

namespace {

TagSession::GameTag playTag(const QString& mainEvent, qint64 markMs) {
    TagSession::GameTag tag;
    tag.mainEvent = mainEvent;
    tag.markMs = markMs;
    tag.team = TagSession::teamSideKey(TagSession::TeamSide::Home);
    return tag;
}

}  // namespace

class WorkStateTest : public QObject {
    Q_OBJECT

private slots:
    void pendingNoteAppliesOnlyForTheCapturedEpoch();
    void clearRestartsIdsAndBumpsEpochBeforeListenersReturn();
    void replaceImportBumpsEpochAfterTheFlushSignal();
    void mergeImportKeepsTheEpoch();
    void currentPeriodFollowsQuarterTags();
    void advanceQuarterDerivesTheNextQuarterFromTheTag();
    void advanceQuarterRejectsAGameThatIsNotInProgress();
    void eventPathTakesPrecedenceOverHiddenMains();
    void selectAllAndSelectNoneAreSymmetric();
};

void WorkStateTest::pendingNoteAppliesOnlyForTheCapturedEpoch() {
    PendingClipNote note;
    note.tagId = 1;
    note.idEpoch = 4;
    note.text = QStringLiteral("saved later");

    QVERIFY(pendingClipNoteApplies(note, 4, true));
    QVERIFY(!pendingClipNoteApplies(note, 5, true));
    QVERIFY(!pendingClipNoteApplies(note, 4, false));

    note.tagId = 0;
    QVERIFY(!pendingClipNoteApplies(note, 4, true));
}

void WorkStateTest::clearRestartsIdsAndBumpsEpochBeforeListenersReturn() {
    TagSession session;
    session.addTag(playTag(QStringLiteral("Goal"), 1000));
    QCOMPARE(session.tags().constFirst().id, quint64{1});
    const quint64 epochBeforeClear = session.tagIdEpoch();

    bool flushSawOriginalTag = false;
    connect(&session, &TagSession::aboutToClear, &session, [&]() {
        flushSawOriginalTag = session.indexOfTagId(1) >= 0;
        QCOMPARE(session.tagIdEpoch(), epochBeforeClear);
    }, Qt::DirectConnection);

    session.clear();
    QVERIFY(flushSawOriginalTag);
    QCOMPARE(session.tagIdEpoch(), epochBeforeClear + 1);
    QVERIFY(session.tags().isEmpty());

    session.addTag(playTag(QStringLiteral("Goal"), 2000));
    QCOMPARE(session.tags().constFirst().id, quint64{1});

    const PendingClipNote staleNote{1, epochBeforeClear, QStringLiteral("old game")};
    QVERIFY(!pendingClipNoteApplies(staleNote, session.tagIdEpoch(), true));
}

void WorkStateTest::replaceImportBumpsEpochAfterTheFlushSignal() {
    TagSession session;
    session.addTag(playTag(QStringLiteral("Goal"), 1000));
    const quint64 epochBeforeImport = session.tagIdEpoch();

    bool flushSawOriginalTag = false;
    connect(&session, &TagSession::aboutToReplaceTags, &session, [&]() {
        flushSawOriginalTag = session.indexOfTagId(1) >= 0;
        QCOMPARE(session.tagIdEpoch(), epochBeforeImport);
    }, Qt::DirectConnection);

    TagSession::GameTag replacement = playTag(QStringLiteral("Shot"), 3000);
    session.importTags({replacement}, TagSession::ImportMode::Replace);
    QVERIFY(flushSawOriginalTag);
    QCOMPARE(session.tagIdEpoch(), epochBeforeImport + 1);
    QCOMPARE(session.tags().size(), 1);
    QCOMPARE(session.tags().constFirst().mainEvent, QStringLiteral("Shot"));
}

void WorkStateTest::mergeImportKeepsTheEpoch() {
    TagSession session;
    session.addTag(playTag(QStringLiteral("Goal"), 1000));
    const quint64 epochBeforeImport = session.tagIdEpoch();

    bool signalFired = false;
    connect(&session, &TagSession::aboutToReplaceTags, &session, [&]() {
        signalFired = true;
        QCOMPARE(session.tags().size(), 1);
    }, Qt::DirectConnection);

    session.importTags({playTag(QStringLiteral("Shot"), 3000)}, TagSession::ImportMode::Merge);
    QVERIFY(signalFired);
    QCOMPARE(session.tagIdEpoch(), epochBeforeImport);
    QCOMPARE(session.tags().size(), 2);
}

void WorkStateTest::currentPeriodFollowsQuarterTags() {
    TagSession session;
    QVERIFY(session.currentPeriodLabel().isEmpty());
    QCOMPARE(session.quarterPhase(), TagSession::QuarterPhase::NotStarted);

    QVERIFY(session.startGame(1000));
    QCOMPARE(session.quarterPhase(), TagSession::QuarterPhase::QuarterInProgress);
    QCOMPARE(session.currentQuarterIndex(), 0);
    QCOMPARE(session.currentQuarterStartMs(), qint64{1000});
    QCOMPARE(session.currentPeriodLabel(), EventDefaults::quarterCode(0));
    QCOMPARE(session.tags().size(), 1);
    QCOMPARE(session.tags().constFirst().mainEvent,
             QString::fromLatin1(EventDefaults::TimeCodes::kStartAnchor));
    QCOMPARE(session.tags().constFirst().startMs, qint64{1000});
    QCOMPARE(session.tags().constFirst().endMs, qint64{3000});
    QCOMPARE(session.tags().constFirst().period, EventDefaults::quarterCode(0));

    QVERIFY(!session.startGame(4000));
    QCOMPARE(session.tags().size(), 1);

    TagSession clamped;
    QVERIFY(clamped.startGame(-50));
    QCOMPARE(clamped.tags().constFirst().markMs, qint64{0});
    QCOMPARE(clamped.tags().constFirst().startMs, qint64{0});
}

void WorkStateTest::advanceQuarterDerivesTheNextQuarterFromTheTag() {
    TagSession session;
    QVERIFY(session.startGame(0));

    QVERIFY(session.advanceQuarter(5000));
    QCOMPARE(session.currentQuarterIndex(), 1);
    QCOMPARE(session.currentQuarterStartMs(), qint64{5000});
    QCOMPARE(session.currentPeriodLabel(), EventDefaults::quarterCode(1));
    QCOMPARE(session.quarterPhase(), TagSession::QuarterPhase::QuarterInProgress);

    const TagSession::GameTag* closedQuarter = nullptr;
    for (const TagSession::GameTag& tag : session.tags()) {
        if (tag.mainEvent == EventDefaults::quarterCode(0)) closedQuarter = &tag;
    }
    QVERIFY(closedQuarter != nullptr);
    QCOMPARE(closedQuarter->startMs, qint64{0});
    QCOMPARE(closedQuarter->endMs, qint64{5000});
    QCOMPARE(closedQuarter->period, EventDefaults::quarterCode(0));

    QVERIFY(session.advanceQuarter(6000));
    QVERIFY(session.advanceQuarter(7000));
    QVERIFY(session.advanceQuarter(8000));
    QCOMPARE(session.quarterPhase(), TagSession::QuarterPhase::GameEnded);
    QCOMPARE(session.currentQuarterIndex(), -1);
    QCOMPARE(session.currentPeriodLabel(), EventDefaults::quarterCode(3));
    QCOMPARE(session.periodLabelAtTimestampMs(9000), EventDefaults::quarterCode(3));

    const int tagCount = session.tags().size();
    QVERIFY(!session.advanceQuarter(9000));
    QCOMPARE(session.tags().size(), tagCount);
    QCOMPARE(session.currentPeriodLabel(), EventDefaults::quarterCode(3));
}

void WorkStateTest::advanceQuarterRejectsAGameThatIsNotInProgress() {
    TagSession session;
    QVERIFY(!session.advanceQuarter(1000));
    QVERIFY(session.tags().isEmpty());
    QVERIFY(session.currentPeriodLabel().isEmpty());

    QVERIFY(session.startGame(100));
    QVERIFY(session.advanceQuarter(50));
    QCOMPARE(session.currentQuarterIndex(), 1);
    QCOMPARE(session.currentQuarterStartMs(), qint64{100});
}

void WorkStateTest::eventPathTakesPrecedenceOverHiddenMains() {
    TagFilter filter;
    filter.hiddenMainEvents.insert(QStringLiteral("Shot"));
    filter.eventPath = EventPathFilter{QStringLiteral("Circle Entry"), QStringLiteral("Goal")};

    QVERIFY(filter.allows(QStringLiteral("Circle Entry"), QStringLiteral("Goal")));
    QVERIFY(filter.allows(QStringLiteral("Circle Entry"), QStringLiteral("Goal → Far")));
    QVERIFY(!filter.allows(QStringLiteral("Circle Entry"), QStringLiteral("No Goal")));
    QVERIFY(!filter.allows(QStringLiteral("Circle Entry"), QStringLiteral("Goal Extra")));
    QVERIFY(!filter.allows(QStringLiteral("Shot"), QString()));
    QVERIFY(!filter.allows(QStringLiteral("PC"), QString()));

    filter.hiddenMainEvents.insert(QStringLiteral("Circle Entry"));
    QVERIFY(filter.allows(QStringLiteral("Circle Entry"), QStringLiteral("Goal")));
    QVERIFY(filter.isActive());

    filter.eventPath->followUpEvent.clear();
    QVERIFY(filter.allows(QStringLiteral("Circle Entry"), QStringLiteral("No Goal")));
    QVERIFY(!filter.allows(QStringLiteral("PC"), QStringLiteral("Goal")));

    filter.clearEventPath();
    QVERIFY(!filter.allows(QStringLiteral("Shot"), QString()));
    QVERIFY(!filter.allows(QStringLiteral("Circle Entry"), QStringLiteral("Goal")));
    QVERIFY(filter.allows(QStringLiteral("PC"), QString()));
}

void WorkStateTest::selectAllAndSelectNoneAreSymmetric() {
    const QStringList mainEvents{QStringLiteral("Shot"), QStringLiteral("Goal"), QStringLiteral("PC")};

    TagFilter showAll;
    showAll.hideAllMainEvents(mainEvents);
    showAll.eventPath = EventPathFilter{QStringLiteral("Shot"), QString()};
    showAll.showAllMainEvents();
    QVERIFY(showAll.hiddenMainEvents.isEmpty());
    QVERIFY(showAll.eventPath.has_value());
    QVERIFY(showAll.isActive());
    QVERIFY(showAll.allows(QStringLiteral("Shot"), QStringLiteral("Saved")));
    QVERIFY(!showAll.allows(QStringLiteral("Goal"), QString()));

    showAll.clearEventPath();
    QVERIFY(!showAll.isActive());
    QVERIFY(showAll.allows(QStringLiteral("Goal"), QString()));

    TagFilter showNone;
    showNone.hideAllMainEvents(mainEvents);
    QVERIFY(showNone.isActive());
    for (const QString& mainEvent : mainEvents) {
        QVERIFY(!showNone.allows(mainEvent, QString()));
    }
    QVERIFY(showNone.allows(QStringLiteral("Circle Entry"), QString()));

    showNone.showAllMainEvents();
    QVERIFY(!showNone.isActive());
    QVERIFY(showNone.hiddenMainEvents.isEmpty());
    for (const QString& mainEvent : mainEvents) {
        QVERIFY(showNone.allows(mainEvent, QString()));
    }
}

int runWorkStateTests(int argc, char** argv) {
    WorkStateTest tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "WorkStateTests.moc"
