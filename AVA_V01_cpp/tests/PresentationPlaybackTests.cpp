#include "PresentationPlayback.h"

#include <QTest>

#include <cstddef>
#include <vector>

namespace {

using PresentationPlayback::ClipIntervalUpdated;
using PresentationPlayback::ClipSpan;
using PresentationPlayback::Effect;
using PresentationPlayback::ModeExit;
using PresentationPlayback::OpenClip;
using PresentationPlayback::Pause;
using PresentationPlayback::Play;
using PresentationPlayback::PlayheadTick;
using PresentationPlayback::Previous;
using PresentationPlayback::QueueEmptied;
using PresentationPlayback::QueueSnapshot;
using PresentationPlayback::Reset;
using PresentationPlayback::Result;
using PresentationPlayback::Seek;
using PresentationPlayback::State;
using PresentationPlayback::Next;

QueueSnapshot makeQueue(const std::vector<ClipSpan>& clips, int currentIndex) {
    QueueSnapshot snapshot;
    snapshot.currentIndex = currentIndex;
    snapshot.clips = clips;
    return snapshot;
}

State armedClip(qint64 startMs, qint64 endMs) {
    State state;
    state.playbackStarted = true;
    state.autoPauseArmed = true;
    state.autoPauseAtMs = endMs;
    state.lastPlayheadMs = startMs;
    return state;
}

void expectState(const State& actual, const State& expected) {
    QCOMPARE(actual.playbackStarted, expected.playbackStarted);
    QCOMPARE(actual.autoPauseArmed, expected.autoPauseArmed);
    QCOMPARE(actual.autoPauseAtMs, expected.autoPauseAtMs);
    QCOMPARE(actual.lastPlayheadMs, expected.lastPlayheadMs);
}

void expectNoTransition(const Result& result, const State& state) {
    expectState(result.state, state);
    QVERIFY(result.effects.empty());
    QVERIFY(!result.queueIndex.has_value());
}

void expectSeek(const Effect& effect, qint64 positionMs) {
    const Seek* seek = std::get_if<Seek>(&effect);
    QVERIFY(seek != nullptr);
    if (!seek) return;
    QCOMPARE(seek->positionMs, positionMs);
}

void expectPlay(const Effect& effect) {
    QVERIFY(std::holds_alternative<Play>(effect));
}

void expectPause(const Effect& effect) {
    QVERIFY(std::holds_alternative<Pause>(effect));
}

void expectShowClip(const Result& result, int queueIndex, qint64 startMs, qint64 endMs) {
    QVERIFY(result.queueIndex.has_value());
    QCOMPARE(*result.queueIndex, queueIndex);
    QCOMPARE(result.effects.size(), std::size_t{2});
    expectSeek(result.effects.at(0), startMs);
    expectPlay(result.effects.at(1));
    QCOMPARE(result.state.playbackStarted, true);
    QCOMPARE(result.state.autoPauseArmed, true);
    QCOMPARE(result.state.autoPauseAtMs, endMs);
    QCOMPARE(result.state.lastPlayheadMs, startMs);
}

const std::vector<ClipSpan> kThreeClips{
    ClipSpan{1000, 2000},
    ClipSpan{4000, 5500},
    ClipSpan{8000, 9000},
};

}  // namespace

class PresentationPlaybackTest : public QObject {
    Q_OBJECT

private slots:
    void firstNextStartsCurrentClipInsteadOfSkipping();
    void firstPreviousStartsCurrentClipInsteadOfRewinding();
    void negativeCurrentIndexStartsAtTheFirstClip();
    void nextAdvancesUntilTheLastClip();
    void previousRewindsUntilTheFirstClip();
    void emptyQueueIgnoresNavigation();
    void unstartedNavigationIgnoresAnIndexPastTheEnd();
    void startedNavigationIgnoresAMissingCurrentClip();
    void openClipSeeksPlaysAndArmsAutoPause();
    void autoPauseFiresOnceWhenPlaybackCrossesTheEnd();
    void autoPauseAllowsAnAdvanceOfTheMaximumContinuousStep();
    void scrubbingPastTheEndDisarmsWithoutPausing();
    void reachingTheEndWhilePausedDisarmsWithoutAPauseEffect();
    void playheadOutsidePresentationDoesNotDisarmOrPause();
    void playheadBeforeTheEndStaysArmed();
    void initialPlayheadDoesNotCountAsAPlaybackCross();
    void modeExitDisarmsWithoutClearingTheStartedLatch();
    void nextAfterModeExitContinuesTheQueue();
    void resetClearsStartedArmAndPlayhead();
    void nextAfterResetStartsTheCurrentClip();
    void queueEmptiedClearsStartedAndArmButKeepsThePlayhead();
    void intervalEditRearmsWhenThePlayheadIsBeforeTheNewEnd();
    void intervalEditDoesNotRearmWhenThePlayheadIsAtTheNewEnd();
    void intervalEditKeepsTheOldPausePointWhenThePlayheadIsAlreadyPastTheNewEnd();
};

void PresentationPlaybackTest::firstNextStartsCurrentClipInsteadOfSkipping() {
    const State idle;
    const Result result =
        PresentationPlayback::reduce(idle, Next{makeQueue(kThreeClips, 1)});

    expectShowClip(result, 1, 4000, 5500);
}

void PresentationPlaybackTest::firstPreviousStartsCurrentClipInsteadOfRewinding() {
    const State idle;
    const Result result =
        PresentationPlayback::reduce(idle, Previous{makeQueue(kThreeClips, 1)});

    expectShowClip(result, 1, 4000, 5500);
}

void PresentationPlaybackTest::negativeCurrentIndexStartsAtTheFirstClip() {
    const Result result =
        PresentationPlayback::reduce(State{}, Next{makeQueue(kThreeClips, -1)});

    expectShowClip(result, 0, 1000, 2000);
}

void PresentationPlaybackTest::nextAdvancesUntilTheLastClip() {
    State state = armedClip(1000, 2000);
    const Result second =
        PresentationPlayback::reduce(state, Next{makeQueue(kThreeClips, 0)});
    expectShowClip(second, 1, 4000, 5500);

    const Result third =
        PresentationPlayback::reduce(second.state, Next{makeQueue(kThreeClips, 1)});
    expectShowClip(third, 2, 8000, 9000);

    const Result stopped =
        PresentationPlayback::reduce(third.state, Next{makeQueue(kThreeClips, 2)});
    expectNoTransition(stopped, third.state);
}

void PresentationPlaybackTest::previousRewindsUntilTheFirstClip() {
    State state = armedClip(8000, 9000);
    const Result second =
        PresentationPlayback::reduce(state, Previous{makeQueue(kThreeClips, 2)});
    expectShowClip(second, 1, 4000, 5500);

    const Result first =
        PresentationPlayback::reduce(second.state, Previous{makeQueue(kThreeClips, 1)});
    expectShowClip(first, 0, 1000, 2000);

    const Result stopped =
        PresentationPlayback::reduce(first.state, Previous{makeQueue(kThreeClips, 0)});
    expectNoTransition(stopped, first.state);
}

void PresentationPlaybackTest::emptyQueueIgnoresNavigation() {
    const State idle;
    const QueueSnapshot empty = makeQueue({}, -1);
    expectNoTransition(PresentationPlayback::reduce(idle, Next{empty}), idle);
    expectNoTransition(PresentationPlayback::reduce(idle, Previous{empty}), idle);

    const State started = armedClip(1000, 2000);
    expectNoTransition(PresentationPlayback::reduce(started, Next{empty}), started);
    expectNoTransition(PresentationPlayback::reduce(started, Previous{empty}), started);
}

void PresentationPlaybackTest::unstartedNavigationIgnoresAnIndexPastTheEnd() {
    const State idle;
    const Result result =
        PresentationPlayback::reduce(idle, Next{makeQueue(kThreeClips, 5)});
    expectNoTransition(result, idle);
}

void PresentationPlaybackTest::startedNavigationIgnoresAMissingCurrentClip() {
    const State started = armedClip(1000, 2000);
    const QueueSnapshot queue = makeQueue(kThreeClips, -1);
    expectNoTransition(PresentationPlayback::reduce(started, Next{queue}), started);
    expectNoTransition(PresentationPlayback::reduce(started, Previous{queue}), started);
}

void PresentationPlaybackTest::openClipSeeksPlaysAndArmsAutoPause() {
    const Result result = PresentationPlayback::reduce(State{}, OpenClip{4000, 5500});

    QVERIFY(!result.queueIndex.has_value());
    QCOMPARE(result.effects.size(), std::size_t{2});
    expectSeek(result.effects.at(0), 4000);
    expectPlay(result.effects.at(1));
    QCOMPARE(result.state.playbackStarted, true);
    QCOMPARE(result.state.autoPauseArmed, true);
    QCOMPARE(result.state.autoPauseAtMs, qint64{5500});
    QCOMPARE(result.state.lastPlayheadMs, qint64{4000});
}

void PresentationPlaybackTest::autoPauseFiresOnceWhenPlaybackCrossesTheEnd() {
    State state =
        PresentationPlayback::reduce(State{}, OpenClip{0, 1000}).state;

    const Result beforeEnd = PresentationPlayback::reduce(
        state, PlayheadTick{900, true, true});
    QCOMPARE(beforeEnd.effects.size(), std::size_t{0});
    QCOMPARE(beforeEnd.state.autoPauseArmed, true);
    QCOMPARE(beforeEnd.state.lastPlayheadMs, qint64{900});

    const Result atEnd = PresentationPlayback::reduce(
        beforeEnd.state, PlayheadTick{1000, true, true});
    QCOMPARE(atEnd.effects.size(), std::size_t{1});
    expectPause(atEnd.effects.at(0));
    QCOMPARE(atEnd.state.autoPauseArmed, false);
    QCOMPARE(atEnd.state.playbackStarted, true);
    QCOMPARE(atEnd.state.autoPauseAtMs, qint64{1000});

    const Result afterEnd = PresentationPlayback::reduce(
        atEnd.state, PlayheadTick{1100, true, true});
    QVERIFY(afterEnd.effects.empty());
    QCOMPARE(afterEnd.state.autoPauseArmed, false);

    const Result secondClip =
        PresentationPlayback::reduce(afterEnd.state, OpenClip{2000, 3000});
    QCOMPARE(secondClip.state.autoPauseArmed, true);
    QCOMPARE(secondClip.state.autoPauseAtMs, qint64{3000});

    const Result stillInside = PresentationPlayback::reduce(
        secondClip.state, PlayheadTick{2900, true, true});
    QVERIFY(stillInside.effects.empty());
    QCOMPARE(stillInside.state.autoPauseArmed, true);

    const Result secondEnd = PresentationPlayback::reduce(
        stillInside.state, PlayheadTick{3000, true, true});
    QCOMPARE(secondEnd.effects.size(), std::size_t{1});
    expectPause(secondEnd.effects.at(0));
    QCOMPARE(secondEnd.state.autoPauseArmed, false);

    const Result afterSecondEnd = PresentationPlayback::reduce(
        secondEnd.state, PlayheadTick{3100, true, true});
    QVERIFY(afterSecondEnd.effects.empty());
}

void PresentationPlaybackTest::autoPauseAllowsAnAdvanceOfTheMaximumContinuousStep() {
    const qint64 endMs = 8000;
    const qint64 previousMs = endMs - PresentationPlayback::kMaxContinuousPlaybackAdvanceMs;
    State state = armedClip(previousMs, endMs);

    const Result onTheLimit = PresentationPlayback::reduce(
        state, PlayheadTick{endMs, true, true});
    QCOMPARE(onTheLimit.effects.size(), std::size_t{1});
    expectPause(onTheLimit.effects.at(0));
    QCOMPARE(onTheLimit.state.autoPauseArmed, false);

    State pastTheLimit = armedClip(previousMs - 1, endMs);
    const Result tooFar = PresentationPlayback::reduce(
        pastTheLimit, PlayheadTick{endMs, true, true});
    QVERIFY(tooFar.effects.empty());
    QCOMPARE(tooFar.state.autoPauseArmed, false);
    QCOMPARE(tooFar.state.lastPlayheadMs, endMs);
}

void PresentationPlaybackTest::scrubbingPastTheEndDisarmsWithoutPausing() {
    const State state = armedClip(1000, 4000);
    const Result scrub = PresentationPlayback::reduce(
        state, PlayheadTick{10000, true, true});

    QVERIFY(scrub.effects.empty());
    QCOMPARE(scrub.state.autoPauseArmed, false);
    QCOMPARE(scrub.state.playbackStarted, true);
    QCOMPARE(scrub.state.lastPlayheadMs, qint64{10000});

    const Result later = PresentationPlayback::reduce(
        scrub.state, PlayheadTick{10100, true, true});
    QVERIFY(later.effects.empty());
    QCOMPARE(later.state.autoPauseArmed, false);
}

void PresentationPlaybackTest::reachingTheEndWhilePausedDisarmsWithoutAPauseEffect() {
    const State state = armedClip(1000, 4000);
    const Result result = PresentationPlayback::reduce(
        state, PlayheadTick{4500, true, false});

    QVERIFY(result.effects.empty());
    QCOMPARE(result.state.autoPauseArmed, false);
    QCOMPARE(result.state.lastPlayheadMs, qint64{4500});
}

void PresentationPlaybackTest::playheadOutsidePresentationDoesNotDisarmOrPause() {
    const State state = armedClip(1000, 4000);
    const Result result = PresentationPlayback::reduce(
        state, PlayheadTick{9000, false, true});

    QVERIFY(result.effects.empty());
    QCOMPARE(result.state.autoPauseArmed, true);
    QCOMPARE(result.state.autoPauseAtMs, qint64{4000});
    QCOMPARE(result.state.playbackStarted, true);
    QCOMPARE(result.state.lastPlayheadMs, qint64{9000});
}

void PresentationPlaybackTest::playheadBeforeTheEndStaysArmed() {
    const State state = armedClip(1000, 4000);
    const Result result = PresentationPlayback::reduce(
        state, PlayheadTick{2500, true, true});

    QVERIFY(result.effects.empty());
    QCOMPARE(result.state.autoPauseArmed, true);
    QCOMPARE(result.state.autoPauseAtMs, qint64{4000});
    QCOMPARE(result.state.lastPlayheadMs, qint64{2500});
}

void PresentationPlaybackTest::initialPlayheadDoesNotCountAsAPlaybackCross() {
    State state;
    state.autoPauseArmed = true;
    state.autoPauseAtMs = 4000;
    state.lastPlayheadMs = -1;

    const Result result = PresentationPlayback::reduce(
        state, PlayheadTick{5000, true, true});

    QVERIFY(result.effects.empty());
    QCOMPARE(result.state.autoPauseArmed, false);
    QCOMPARE(result.state.lastPlayheadMs, qint64{5000});
}

void PresentationPlaybackTest::modeExitDisarmsWithoutClearingTheStartedLatch() {
    const State state = armedClip(4000, 5500);
    const Result result = PresentationPlayback::reduce(state, ModeExit{});

    QVERIFY(result.effects.empty());
    QVERIFY(!result.queueIndex.has_value());
    QCOMPARE(result.state.autoPauseArmed, false);
    QCOMPARE(result.state.playbackStarted, true);
    QCOMPARE(result.state.autoPauseAtMs, qint64{5500});
    QCOMPARE(result.state.lastPlayheadMs, qint64{4000});
}

void PresentationPlaybackTest::nextAfterModeExitContinuesTheQueue() {
    const State left = PresentationPlayback::reduce(armedClip(1000, 2000), ModeExit{}).state;
    const Result result =
        PresentationPlayback::reduce(left, Next{makeQueue(kThreeClips, 0)});

    expectShowClip(result, 1, 4000, 5500);
}

void PresentationPlaybackTest::resetClearsStartedArmAndPlayhead() {
    const State state = armedClip(4000, 5500);
    const Result result = PresentationPlayback::reduce(state, Reset{});

    QVERIFY(result.effects.empty());
    QCOMPARE(result.state.playbackStarted, false);
    QCOMPARE(result.state.autoPauseArmed, false);
    QCOMPARE(result.state.lastPlayheadMs, qint64{-1});
    QCOMPARE(result.state.autoPauseAtMs, qint64{5500});
}

void PresentationPlaybackTest::nextAfterResetStartsTheCurrentClip() {
    const State reset =
        PresentationPlayback::reduce(armedClip(1000, 2000), Reset{}).state;
    const Result result =
        PresentationPlayback::reduce(reset, Next{makeQueue(kThreeClips, 0)});

    expectShowClip(result, 0, 1000, 2000);
}

void PresentationPlaybackTest::queueEmptiedClearsStartedAndArmButKeepsThePlayhead() {
    const State state = armedClip(4000, 5500);
    const Result result = PresentationPlayback::reduce(state, QueueEmptied{});

    QVERIFY(result.effects.empty());
    QCOMPARE(result.state.playbackStarted, false);
    QCOMPARE(result.state.autoPauseArmed, false);
    QCOMPARE(result.state.lastPlayheadMs, qint64{4000});
    QCOMPARE(result.state.autoPauseAtMs, qint64{5500});
}

void PresentationPlaybackTest::intervalEditRearmsWhenThePlayheadIsBeforeTheNewEnd() {
    State state = armedClip(1000, 4000);
    state.autoPauseArmed = false;
    state.lastPlayheadMs = 3000;

    const Result result =
        PresentationPlayback::reduce(state, ClipIntervalUpdated{3000, 5000});

    QVERIFY(result.effects.empty());
    QCOMPARE(result.state.autoPauseArmed, true);
    QCOMPARE(result.state.autoPauseAtMs, qint64{5000});
    QCOMPARE(result.state.lastPlayheadMs, qint64{3000});
    QCOMPARE(result.state.playbackStarted, true);
}

void PresentationPlaybackTest::intervalEditDoesNotRearmWhenThePlayheadIsAtTheNewEnd() {
    State state = armedClip(1000, 4000);
    state.autoPauseArmed = false;

    const Result result =
        PresentationPlayback::reduce(state, ClipIntervalUpdated{4000, 4000});

    expectState(result.state, state);
    QVERIFY(result.effects.empty());
}

void PresentationPlaybackTest::intervalEditKeepsTheOldPausePointWhenThePlayheadIsAlreadyPastTheNewEnd() {
    const State state = armedClip(1000, 5000);
    const Result result =
        PresentationPlayback::reduce(state, ClipIntervalUpdated{4500, 4000});

    expectState(result.state, state);
    QVERIFY(result.effects.empty());
}

int runPresentationPlaybackTests(int argc, char** argv) {
    PresentationPlaybackTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "PresentationPlaybackTests.moc"
