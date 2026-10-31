#include "AppLocale.h"
#include "ClipExporter.h"
#include "ExportJobManager.h"
#include "ExportJobTransition.h"
#include "GameControls.h"
#include "TagSession.h"
#include "VideoOpenStage.h"
#include "WorkWindow.h"

#include <QAction>
#include <QApplication>
#include <QFile>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QVBoxLayout>

namespace {

QString writePlaceholderVideo(QTemporaryDir& directory) {
    const QString path = directory.filePath(QStringLiteral("game.mp4"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return {};
    if (file.write("not a real video") < 0) return {};
    return path;
}

QLabel* periodTitleLabel(QWidget* root, const QString& fullLabel) {
    const QList<QLabel*> labels = root->findChildren<QLabel*>();
    for (QLabel* label : labels) {
        if (label->property("fullLabel").toString() == fullLabel) return label;
    }
    return nullptr;
}

#define EXPECT_STAGE(fromStage, actionTaken, isAccepted, nextStage)                               \
    do {                                                                                           \
        const VideoOpenStage::Result reduced =                                                    \
            VideoOpenStage::reduce((fromStage), (actionTaken));                                   \
        QCOMPARE(reduced.accepted, (isAccepted));                                                 \
        QCOMPARE(reduced.stage, (isAccepted) ? (nextStage) : (fromStage));                        \
    } while (false)

}  // namespace

class ExplicitStateMachineTest : public QObject {
    Q_OBJECT

private slots:
    void videoStageAllowsTheLegalTransitions();
    void videoStageRefusesReplaceWhenNoVideoIsOpen();
    void replacingBlocksImportAndExportUntilAbandon();
    void failedPreparationAbandonsBackToNoVideo();
    void closeClearsTheOpenVideo();
    void quarterSnapshotRefusesAnIllegalIndex();
    void panelStaysWithTheSessionWhenAdvanceIsRefused();
    void periodClickDoesNothingWithoutATagSession();
    void modeAllowsEveryRealModeAndRefusesAnUnknownValue();
    void failedExportMessageContainingCancelledStaysFailed();
    void clipExporterEmptySourceFinishesFailed();
    void cancellingAFinishedXmlJobLeavesItSucceeded();
};

void ExplicitStateMachineTest::videoStageAllowsTheLegalTransitions() {
    EXPECT_STAGE(VideoOpenStage::Stage::NoVideo, VideoOpenStage::Action::BeginPreparation,
                 true, VideoOpenStage::Stage::Preparing);
    EXPECT_STAGE(VideoOpenStage::Stage::NoVideo, VideoOpenStage::Action::Close,
                 true, VideoOpenStage::Stage::NoVideo);
    EXPECT_STAGE(VideoOpenStage::Stage::Preparing, VideoOpenStage::Action::BeginPreparation,
                 true, VideoOpenStage::Stage::Preparing);
    EXPECT_STAGE(VideoOpenStage::Stage::Preparing, VideoOpenStage::Action::CommitOpened,
                 true, VideoOpenStage::Stage::VideoOpen);
    EXPECT_STAGE(VideoOpenStage::Stage::Preparing, VideoOpenStage::Action::Close,
                 true, VideoOpenStage::Stage::NoVideo);
    EXPECT_STAGE(VideoOpenStage::Stage::Preparing, VideoOpenStage::Action::AbandonFailedOpen,
                 true, VideoOpenStage::Stage::NoVideo);
    EXPECT_STAGE(VideoOpenStage::Stage::VideoOpen, VideoOpenStage::Action::BeginReplace,
                 true, VideoOpenStage::Stage::Replacing);
    EXPECT_STAGE(VideoOpenStage::Stage::VideoOpen, VideoOpenStage::Action::Close,
                 true, VideoOpenStage::Stage::NoVideo);
    EXPECT_STAGE(VideoOpenStage::Stage::Replacing, VideoOpenStage::Action::BeginReplace,
                 true, VideoOpenStage::Stage::Replacing);
    EXPECT_STAGE(VideoOpenStage::Stage::Replacing, VideoOpenStage::Action::CommitOpened,
                 true, VideoOpenStage::Stage::VideoOpen);
    EXPECT_STAGE(VideoOpenStage::Stage::Replacing, VideoOpenStage::Action::AbandonFailedOpen,
                 true, VideoOpenStage::Stage::VideoOpen);
    EXPECT_STAGE(VideoOpenStage::Stage::Replacing, VideoOpenStage::Action::Close,
                 true, VideoOpenStage::Stage::NoVideo);
}

void ExplicitStateMachineTest::videoStageRefusesReplaceWhenNoVideoIsOpen() {
    EXPECT_STAGE(VideoOpenStage::Stage::NoVideo, VideoOpenStage::Action::BeginReplace,
                 false, VideoOpenStage::Stage::NoVideo);
    EXPECT_STAGE(VideoOpenStage::Stage::NoVideo, VideoOpenStage::Action::CommitOpened,
                 false, VideoOpenStage::Stage::NoVideo);
    EXPECT_STAGE(VideoOpenStage::Stage::NoVideo, VideoOpenStage::Action::AbandonFailedOpen,
                 false, VideoOpenStage::Stage::NoVideo);
    EXPECT_STAGE(VideoOpenStage::Stage::Preparing, VideoOpenStage::Action::BeginReplace,
                 false, VideoOpenStage::Stage::Preparing);
    EXPECT_STAGE(VideoOpenStage::Stage::VideoOpen, VideoOpenStage::Action::BeginPreparation,
                 false, VideoOpenStage::Stage::VideoOpen);
    EXPECT_STAGE(VideoOpenStage::Stage::VideoOpen, VideoOpenStage::Action::CommitOpened,
                 false, VideoOpenStage::Stage::VideoOpen);
    EXPECT_STAGE(VideoOpenStage::Stage::VideoOpen, VideoOpenStage::Action::AbandonFailedOpen,
                 false, VideoOpenStage::Stage::VideoOpen);
    EXPECT_STAGE(VideoOpenStage::Stage::Replacing, VideoOpenStage::Action::BeginPreparation,
                 false, VideoOpenStage::Stage::Replacing);
}

void ExplicitStateMachineTest::replacingBlocksImportAndExportUntilAbandon() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString videoPath = writePlaceholderVideo(directory);
    QVERIFY(!videoPath.isEmpty());

    TagSession session;
    WorkWindow window;
    window.setTagSession(&session);
    window.setExportDefaultDirectoryFromVideoPath(videoPath);
    window.loadVideoFromFile(videoPath);
    QCOMPARE(window.workStage(), VideoOpenStage::Stage::VideoOpen);
    QVERIFY(window.hasOpenVideo());
    QVERIFY(window.videoImportAndExportAllowed());

    const QString sourceBeforeReplace = window.sourceVideoPath_;
    const QString exportDirectoryBeforeReplace = window.exportDefaultDirectoryPath_;
    QCOMPARE(sourceBeforeReplace, videoPath);
    QVERIFY(!exportDirectoryBeforeReplace.isEmpty());

    auto* importAction = window.findChild<QAction*>(QStringLiteral("ImportXmlAction"));
    auto* exportAction = window.findChild<QAction*>(QStringLiteral("ExportXmlAction"));
    auto* exportButton = window.findChild<QPushButton*>(QStringLiteral("PresentationExportButton"));
    QVERIFY(importAction != nullptr);
    QVERIFY(exportAction != nullptr);
    QVERIFY(exportButton != nullptr);
    QVERIFY(importAction->isEnabled());
    QVERIFY(exportAction->isEnabled());
    QVERIFY(exportButton->isEnabled());
    QCOMPARE(exportButton->cursor().shape(), Qt::PointingHandCursor);

    WorkWindow::VideoStageInput input;
    input.pendingMatchSourcePaths = QStringList{QStringLiteral("/tmp/replacement-source.mp4")};
    input.pendingMetadataConfirmed = false;
    input.updateExportDirectory = true;
    input.exportDirectory = QStringLiteral("/tmp/replacement-export");
    const WorkWindow::VideoStageCommitResult begun =
        window.commitVideoStage(VideoOpenStage::Action::BeginReplace, input);
    QVERIFY(begun.accepted);
    QCOMPARE(window.workStage(), VideoOpenStage::Stage::Replacing);
    QVERIFY(window.hasOpenVideo());
    QVERIFY(!window.videoImportAndExportAllowed());
    QCOMPARE(window.exportDefaultDirectoryPath_, QStringLiteral("/tmp/replacement-export"));
    QCOMPARE(window.sourceVideoPath_, sourceBeforeReplace);
    QVERIFY(!importAction->isEnabled());
    QVERIFY(!exportAction->isEnabled());
    QVERIFY(!exportButton->isEnabled());
    QCOMPARE(exportButton->cursor().shape(), Qt::ArrowCursor);

    QVERIFY(QMetaObject::invokeMethod(&window, "onImportXml", Qt::DirectConnection));
    QVERIFY(QMetaObject::invokeMethod(&window, "onExportXml", Qt::DirectConnection));
    QVERIFY(QMetaObject::invokeMethod(&window, "onPresentationExportRequested", Qt::DirectConnection));
    QVERIFY(QApplication::activeModalWidget() == nullptr);

    const WorkWindow::VideoStageCommitResult abandoned =
        window.commitVideoStage(VideoOpenStage::Action::AbandonFailedOpen, {});
    QVERIFY(abandoned.accepted);
    QCOMPARE(window.workStage(), VideoOpenStage::Stage::VideoOpen);
    QCOMPARE(window.sourceVideoPath_, sourceBeforeReplace);
    QCOMPARE(window.exportDefaultDirectoryPath_, exportDirectoryBeforeReplace);
    QVERIFY(window.pendingMatchSourcePaths_.isEmpty());
    QVERIFY(window.videoImportAndExportAllowed());
    QVERIFY(importAction->isEnabled());
    QVERIFY(exportAction->isEnabled());
    QVERIFY(exportButton->isEnabled());
    QCOMPARE(exportButton->cursor().shape(), Qt::PointingHandCursor);
}

void ExplicitStateMachineTest::failedPreparationAbandonsBackToNoVideo() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString videoPath = writePlaceholderVideo(directory);
    QVERIFY(!videoPath.isEmpty());

    WorkWindow window;
    window.showTeamSetupForVideo(videoPath, QStringList{videoPath});
    QCOMPARE(window.workStage(), VideoOpenStage::Stage::Preparing);
    QVERIFY(!window.hasOpenVideo());
    QVERIFY(!window.videoImportAndExportAllowed());

    const WorkWindow::VideoStageCommitResult abandoned =
        window.commitVideoStage(VideoOpenStage::Action::AbandonFailedOpen, {});
    QVERIFY(abandoned.accepted);
    QCOMPARE(window.workStage(), VideoOpenStage::Stage::NoVideo);
    QVERIFY(window.sourceVideoPath_.isEmpty());
    QVERIFY(!window.videoImportAndExportAllowed());
}

void ExplicitStateMachineTest::closeClearsTheOpenVideo() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString videoPath = writePlaceholderVideo(directory);
    QVERIFY(!videoPath.isEmpty());

    TagSession session;
    WorkWindow window;
    window.setTagSession(&session);
    window.loadVideoFromFile(videoPath);
    QCOMPARE(window.workStage(), VideoOpenStage::Stage::VideoOpen);

    QVERIFY(QMetaObject::invokeMethod(&window, "onCloseVideo", Qt::DirectConnection));
    QCOMPARE(window.workStage(), VideoOpenStage::Stage::NoVideo);
    QVERIFY(!window.hasOpenVideo());
    QVERIFY(window.sourceVideoPath_.isEmpty());
    QVERIFY(window.matchSourcePaths_.isEmpty());
    QVERIFY(!window.videoImportAndExportAllowed());
}

void ExplicitStateMachineTest::quarterSnapshotRefusesAnIllegalIndex() {
    QVERIFY(TagSession::isLegalQuarterSnapshot(TagSession::QuarterPhase::NotStarted, -1));
    QVERIFY(TagSession::isLegalQuarterSnapshot(TagSession::QuarterPhase::GameEnded, -1));
    QVERIFY(TagSession::isLegalQuarterSnapshot(TagSession::QuarterPhase::QuarterInProgress, 0));
    QVERIFY(TagSession::isLegalQuarterSnapshot(TagSession::QuarterPhase::QuarterInProgress, 3));
    QVERIFY(!TagSession::isLegalQuarterSnapshot(TagSession::QuarterPhase::QuarterInProgress, -1));
    QVERIFY(!TagSession::isLegalQuarterSnapshot(TagSession::QuarterPhase::QuarterInProgress, 4));
    QVERIFY(!TagSession::isLegalQuarterSnapshot(TagSession::QuarterPhase::GameEnded, 3));
    QVERIFY(!TagSession::isLegalQuarterSnapshot(TagSession::QuarterPhase::NotStarted, 0));
}

void ExplicitStateMachineTest::panelStaysWithTheSessionWhenAdvanceIsRefused() {
    QWidget window;
    auto* layout = new QVBoxLayout(&window);
    auto* controls = new GameControls(&window);
    layout->addWidget(controls);
    TagSession session;
    controls->setTagSession(&session);
    window.resize(900, 500);
    window.show();
    window.activateWindow();
    QApplication::setActiveWindow(&window);
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    controls->setFocus(Qt::OtherFocusReason);
    QVERIFY(QTest::qWaitFor([&] { return controls->hasFocus(); }));

    connect(controls, &GameControls::gameStartRequested, &session, [&session]() {
        session.startGame(0);
    });
    QKeyEvent startOnce(QEvent::KeyPress, Qt::Key_G, Qt::NoModifier, QStringLiteral("g"), false);
    QApplication::sendEvent(controls, &startOnce);
    QCOMPARE(session.quarterPhase(), TagSession::QuarterPhase::QuarterInProgress);
    QCOMPARE(session.currentQuarterIndex(), 0);
    QCOMPARE(controls->currentPeriodName(), QStringLiteral("Q1"));
    const QString startSecondQuarter = AppLocale::trUi("gamecontrols.start_q2");
    QLabel* periodTitle = periodTitleLabel(controls, startSecondQuarter);
    QVERIFY(periodTitle != nullptr);
    QCOMPARE(periodTitle->property("fullLabel").toString(), startSecondQuarter);

    connect(controls, &GameControls::nextQuarterRequested, &session, []() {});
    QKeyEvent refusedAdvance(QEvent::KeyPress, Qt::Key_H, Qt::NoModifier, QStringLiteral("h"), false);
    QApplication::sendEvent(controls, &refusedAdvance);
    QCOMPARE(session.quarterPhase(), TagSession::QuarterPhase::QuarterInProgress);
    QCOMPARE(session.currentQuarterIndex(), 0);
    QCOMPARE(controls->currentPeriodName(), QStringLiteral("Q1"));
    QCOMPARE(periodTitle->property("fullLabel").toString(), startSecondQuarter);

    disconnect(controls, &GameControls::nextQuarterRequested, nullptr, nullptr);
    connect(controls, &GameControls::nextQuarterRequested, &session, [&session]() {
        session.advanceQuarter(1000);
    });
    QKeyEvent acceptedAdvance(QEvent::KeyPress, Qt::Key_H, Qt::NoModifier, QStringLiteral("h"), false);
    QApplication::sendEvent(controls, &acceptedAdvance);
    QCOMPARE(session.currentQuarterIndex(), 1);
    QCOMPARE(controls->currentPeriodName(), QStringLiteral("Q2"));
    const QString startThirdQuarter = AppLocale::trUi("gamecontrols.start_q3");
    QCOMPARE(periodTitle->property("fullLabel").toString(), startThirdQuarter);
}

void ExplicitStateMachineTest::periodClickDoesNothingWithoutATagSession() {
    WorkWindow window;
    window.setTagSession(nullptr);
    const QString startGame = AppLocale::trUi("gamecontrols.start_game");
    QLabel* periodTitle = periodTitleLabel(&window, startGame);
    QVERIFY(periodTitle != nullptr);
    auto* periodButton = qobject_cast<QPushButton*>(periodTitle->parentWidget());
    QVERIFY(periodButton != nullptr);
    QVERIFY(periodButton->isEnabled());
    QCOMPARE(periodButton->cursor().shape(), Qt::PointingHandCursor);

    periodButton->click();
    QCOMPARE(periodTitle->property("fullLabel").toString(), startGame);
    QCOMPARE(periodButton->cursor().shape(), Qt::PointingHandCursor);
}

void ExplicitStateMachineTest::modeAllowsEveryRealModeAndRefusesAnUnknownValue() {
    const WorkWindow::Mode modes[] = {
        WorkWindow::Mode::Tagging,
        WorkWindow::Mode::Analyzing,
        WorkWindow::Mode::Presenting,
    };
    for (WorkWindow::Mode from : modes) {
        for (WorkWindow::Mode to : modes) {
            QVERIFY(WorkWindow::isAllowedModeTransition(from, to));
        }
    }
    const auto unknown = static_cast<WorkWindow::Mode>(99);
    QVERIFY(!WorkWindow::isAllowedModeTransition(WorkWindow::Mode::Tagging, unknown));
    QVERIFY(!WorkWindow::isAllowedModeTransition(unknown, WorkWindow::Mode::Tagging));

    WorkWindow window;
    QCOMPARE(window.mode(), WorkWindow::Mode::Tagging);
    window.setMode(WorkWindow::Mode::Analyzing);
    QCOMPARE(window.mode(), WorkWindow::Mode::Analyzing);
    window.setMode(WorkWindow::Mode::Presenting);
    QCOMPARE(window.mode(), WorkWindow::Mode::Presenting);
    window.setMode(WorkWindow::Mode::Tagging);
    QCOMPARE(window.mode(), WorkWindow::Mode::Tagging);
}

void ExplicitStateMachineTest::failedExportMessageContainingCancelledStaysFailed() {
    const QString message = QStringLiteral("FFmpeg failed: filter cancelled by the encoder");
    const ExportJobTransition::Decision failed = ExportJobTransition::apply(
        ExportJobTransition::Phase::Exporting, ClipExporter::Outcome::Failed, message);
    QVERIFY(failed.accepted);
    QCOMPARE(failed.phase, ExportJobTransition::Phase::Failed);
    QCOMPARE(failed.statusText, message);

    const ExportJobTransition::Decision cancelled = ExportJobTransition::apply(
        ExportJobTransition::Phase::Exporting,
        ClipExporter::Outcome::Cancelled,
        QStringLiteral("Export cancelled."));
    QVERIFY(cancelled.accepted);
    QCOMPARE(cancelled.phase, ExportJobTransition::Phase::Cancelled);

    const ExportJobTransition::Decision refused = ExportJobTransition::apply(
        ExportJobTransition::Phase::Succeeded,
        ClipExporter::Outcome::Cancelled,
        QStringLiteral("Export cancelled."));
    QVERIFY(!refused.accepted);
    QCOMPARE(refused.phase, ExportJobTransition::Phase::Succeeded);
}

void ExplicitStateMachineTest::clipExporterEmptySourceFinishesFailed() {
    ClipExporter exporter;
    QSignalSpy finished(&exporter, &ClipExporter::exportFinished);
    exporter.setOutputPath(QStringLiteral("/tmp/unused-export.mp4"));
    ClipSegment clip;
    clip.startMs = 0;
    clip.durationMs = 1000;
    exporter.setClips({clip});
    exporter.startExport();
    QCOMPARE(finished.count(), 1);
    QCOMPARE(finished.at(0).at(0).value<ClipExporter::Outcome>(), ClipExporter::Outcome::Failed);
}

void ExplicitStateMachineTest::cancellingAFinishedXmlJobLeavesItSucceeded() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    TagSession session;
    ExportJobRequest request;
    request.format = ExportOutputFormat::Xml;
    request.outputPath = directory.filePath(QStringLiteral("match.xml"));
    request.session = captureTagSessionExportData(session);

    ExportJobManager manager;
    QString errorMessage;
    QVERIFY2(manager.startJob(request, &errorMessage), qPrintable(errorMessage));
    QCOMPARE(manager.snapshots().size(), 1);
    const ExportJobSnapshot started = manager.snapshots().constFirst();
    QVERIFY(started.succeeded);
    QVERIFY(!started.cancelled);
    QVERIFY(!started.failed);

    manager.cancelJob(started.id);
    QCOMPARE(manager.snapshots().size(), 1);
    const ExportJobSnapshot afterCancel = manager.snapshots().constFirst();
    QVERIFY(afterCancel.succeeded);
    QVERIFY(!afterCancel.cancelled);
    QVERIFY(!afterCancel.failed);
}

int runStateMachineTests(int argc, char** argv) {
    ExplicitStateMachineTest tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "StateMachineTests.moc"
