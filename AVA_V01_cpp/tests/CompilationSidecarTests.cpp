#include "AvaVersion.h"
#include "CompilationSidecar.h"
#include "ExportClipBuilder.h"
#include "ExportJobManager.h"
#include "FfmpegLocator.h"
#include "TagSession.h"
#include "XmlExporter.h"

#include <QApplication>
#include <QCoreApplication>
#include <QTest>
#include <QCryptographicHash>
#include <QDate>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QProcess>
#include <QTemporaryDir>
#include <QXmlStreamReader>

#include <algorithm>
#include <cmath>
#include <optional>

namespace {

class DefaultLocaleGuard {
public:
    explicit DefaultLocaleGuard(const QLocale& locale) : previous_(QLocale()) {
        QLocale::setDefault(locale);
    }
    ~DefaultLocaleGuard() { QLocale::setDefault(previous_); }

private:
    QLocale previous_;
};

TagSession::GameTag makeTag(const QString& mainEvent,
                            const QString& team,
                            qint64 startMs,
                            qint64 endMs,
                            qint64 markMs,
                            const QString& period) {
    TagSession::GameTag tag;
    tag.mainEvent = mainEvent;
    tag.team = team;
    tag.startMs = startMs;
    tag.endMs = endMs;
    tag.markMs = markMs;
    tag.period = period;
    tag.intervalManuallyEdited = true;
    return tag;
}

TagSession::GameTag appendTag(TagSession& session, const TagSession::GameTag& tag) {
    session.addTag(tag);
    return session.tags().constLast();
}

PresentationQueue::Clip presentationClipFrom(const TagSession::GameTag& tag, int tagSessionIndex) {
    PresentationQueue::Clip clip;
    clip.tagSessionIndex = tagSessionIndex;
    clip.tagId = tag.id;
    clip.markMs = tag.markMs;
    clip.startMs = tag.startMs;
    clip.endMs = tag.endMs;
    clip.mainEvent = tag.mainEvent;
    clip.followUpEvent = tag.followUpEvent;
    clip.team = tag.team;
    clip.note = tag.note;
    return clip;
}

void configureMatch(TagSession& session) {
    session.setGameTeams(QStringLiteral("UC"), QStringLiteral("OR"),
                         QStringLiteral("#3c5ac8"), QStringLiteral("#c83c3c"));
    session.setGameMetadata(QStringLiteral("Copa área"), QDate(2026, 9, 27),
                            QStringLiteral("UC"), QStringLiteral("OR"));
}

ClipCompilationRecord recordWithSource(qint64 sourceStartMs,
                                       const QString& team,
                                       const QString& code,
                                       const QString& overlayLabel) {
    ClipCompilationRecord record;
    record.teamAbbrev = team;
    record.code = code;
    record.overlayLabel = overlayLabel;
    record.sourceStartMs = sourceStartMs;
    record.sourceEndMs = sourceStartMs + 1000;
    record.xmlInstanceId = 3;
    record.periodLabel = QStringLiteral("Q1");
    ClipSidecarLabel quarter;
    quarter.group = QStringLiteral("QUARTOS");
    quarter.text = QStringLiteral("Q1");
    record.labels.append(quarter);
    return record;
}

qint64 jsonSecondsToMilliseconds(double seconds) {
    return std::llround(seconds * 1000.0);
}

bool runProgram(const QString& program,
                const QStringList& arguments,
                int timeoutMilliseconds,
                QString* errorMessage,
                QByteArray* standardOutput = nullptr) {
    QProcess process;
    process.start(program, arguments);
    if (!process.waitForStarted(5000)) {
        if (errorMessage) {
            *errorMessage = process.errorString();
        }
        return false;
    }
    if (!process.waitForFinished(timeoutMilliseconds)) {
        process.kill();
        process.waitForFinished(1000);
        if (errorMessage) {
            *errorMessage = QStringLiteral("timed out");
        }
        return false;
    }
    if (standardOutput) {
        *standardOutput = process.readAllStandardOutput();
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (errorMessage) {
            *errorMessage = QString::fromUtf8(process.readAllStandardError()).right(800);
        }
        return false;
    }
    return true;
}

bool waitUntilJobFinishes(ExportJobManager& manager, int timeoutMilliseconds) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMilliseconds) {
        const QVector<ExportJobSnapshot> snapshots = manager.snapshots();
        if (!snapshots.isEmpty() && !snapshots.constFirst().running) {
            return true;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    }
    return false;
}

}  // namespace

class CompilationSidecarOffsetTest : public QObject {
    Q_OBJECT

private slots:
    void accumulatesMeasuredDurationsIncludingMinimumClip();
    void durationCheckUsesTwoFrames();
    void rendersSchemaWithUtf8AndCLocale();
    void writesJsonAtomically();
    void chapterMetadataUsesMillisecondTimebase();
    void mixedTeamsStayInConcatOrder();
    void xmlInstanceIdMatchesCatalogNotCompilationIndex();
    void quartosLabelUsesQuarterSpanContainingMark();
    void shortClipKeepsSourceIntervalAndHalfSecondEncode();
};

void CompilationSidecarOffsetTest::accumulatesMeasuredDurationsIncludingMinimumClip() {
    const QVector<qint64> durations{
        CompilationSidecar::roundSecondsToMilliseconds(0.5),
        CompilationSidecar::roundSecondsToMilliseconds(1.25),
        CompilationSidecar::roundSecondsToMilliseconds(2.0),
    };
    QCOMPARE(durations.at(0), qint64{500});
    QCOMPARE(durations.at(1), qint64{1250});
    QCOMPARE(durations.at(2), qint64{2000});

    const CompilationSidecar::OffsetAccumulation offsets =
        CompilationSidecar::accumulateOffsets(durations);
    const QVector<qint64> expectedOffsets{0, 500, 1750};
    QCOMPARE(offsets.offsetMilliseconds, expectedOffsets);
    QCOMPARE(offsets.summedEndMilliseconds, qint64{3750});

    CompilationSidecar::SidecarBuildInput input;
    input.avaVersion = QStringLiteral("0.1.0+test");
    input.videoFileName = QStringLiteral("clips.mp4");
    input.sourceVideoFileName = QStringLiteral("match.mp4");
    input.finalDurationSeconds = 3.75;
    input.framesPerSecond = 30.0;
    input.measuredDurationSeconds = {0.5, 1.25, 2.0};
    input.clipsInConcatOrder = {
        recordWithSource(9000, QStringLiteral("OR"), QStringLiteral("OR 16YD+"),
                         QStringLiteral("OR - 16-yd")),
        recordWithSource(1000, QStringLiteral("OR"), QStringLiteral("OR 16YD+"),
                         QStringLiteral("OR - 16-yd")),
        recordWithSource(4000, QStringLiteral("OR"), QStringLiteral("OR 16YD+"),
                         QStringLiteral("OR - 16-yd")),
    };
    const CompilationSidecar::SidecarDocument document = CompilationSidecar::buildDocument(input);
    QVERIFY(document.verified);
    QCOMPARE(document.clips.size(), 3);
    QCOMPARE(document.clips.at(0).ordinal, 1);
    QCOMPARE(document.clips.at(0).offsetMilliseconds, qint64{0});
    QCOMPARE(document.clips.at(0).durationMilliseconds, qint64{500});
    QCOMPARE(document.clips.at(0).sourceStartMilliseconds, qint64{9000});
    QCOMPARE(document.clips.at(1).offsetMilliseconds, qint64{500});
    QCOMPARE(document.clips.at(1).sourceStartMilliseconds, qint64{1000});
    QCOMPARE(document.clips.at(2).offsetMilliseconds, qint64{1750});
    QCOMPARE(document.clips.at(2).sourceStartMilliseconds, qint64{4000});

    const QString json = QString::fromUtf8(CompilationSidecar::renderJson(document));
    const int firstSource = json.indexOf(QStringLiteral("\"sourceStartS\": 9.000"));
    const int secondSource = json.indexOf(QStringLiteral("\"sourceStartS\": 1.000"));
    const int thirdSource = json.indexOf(QStringLiteral("\"sourceStartS\": 4.000"));
    QVERIFY(firstSource >= 0);
    QVERIFY(firstSource < secondSource);
    QVERIFY(secondSource < thirdSource);
    QVERIFY(json.contains(QStringLiteral("\"offsetS\": 0.000")));
    QVERIFY(json.contains(QStringLiteral("\"offsetS\": 0.500")));
    QVERIFY(json.contains(QStringLiteral("\"offsetS\": 1.750")));
    QVERIFY(json.contains(QStringLiteral("\"durationS\": 0.500")));
}

void CompilationSidecarOffsetTest::durationCheckUsesTwoFrames() {
    QVERIFY(CompilationSidecar::finalDurationWithinTolerance(1000, 1080, 25.0, 2));
    QVERIFY(!CompilationSidecar::finalDurationWithinTolerance(1000, 1081, 25.0, 2));
    QVERIFY(!CompilationSidecar::finalDurationWithinTolerance(1000, 1000, 0.0, 2));

    CompilationSidecar::SidecarBuildInput input;
    input.avaVersion = QStringLiteral("0.1.0+test");
    input.videoFileName = QStringLiteral("clips.mp4");
    input.sourceVideoFileName = QStringLiteral("match.mp4");
    input.measuredDurationSeconds = {1.0};
    input.finalDurationSeconds = 1.2;
    input.framesPerSecond = 25.0;
    input.clipsInConcatOrder = {
        recordWithSource(0, QStringLiteral("OR"), QStringLiteral("OR 16YD+"),
                         QStringLiteral("OR - 16-yd")),
    };
    QVERIFY(!CompilationSidecar::buildDocument(input).verified);
}

void CompilationSidecarOffsetTest::rendersSchemaWithUtf8AndCLocale() {
    DefaultLocaleGuard german(QLocale(QLocale::German, QLocale::Germany));

    CompilationSidecar::SidecarBuildInput input;
    input.avaVersion = QStringLiteral("0.1.0+test");
    input.videoFileName = QStringLiteral("UC B vs OR - 16-yd OR.mp4");
    input.sourceVideoFileName = QStringLiteral("match.mp4");
    input.finalDurationSeconds = 0.5;
    input.framesPerSecond = 23.976;
    input.measuredDurationSeconds = {0.5};
    ClipCompilationRecord record = recordWithSource(
        57840, QStringLiteral("OR"), QStringLiteral("OR 16YD+"), QStringLiteral("OR - área"));
    record.sourceEndMs = 58040;
    record.xmlInstanceId = 3;
    ClipSidecarLabel resultado;
    resultado.group = QStringLiteral("RESULTADO");
    resultado.text = QStringLiteral("UC 0 - 0 OR");
    record.labels.append(resultado);
    input.clipsInConcatOrder = {record};

    const QByteArray jsonBytes = CompilationSidecar::renderJson(CompilationSidecar::buildDocument(input));
    const QString json = QString::fromUtf8(jsonBytes);
    const QString expected = QStringLiteral(
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"avaVersion\": \"0.1.0+test\",\n"
        "  \"video\": \"UC B vs OR - 16-yd OR.mp4\",\n"
        "  \"sourceVideo\": \"match.mp4\",\n"
        "  \"matchXml\": null,\n"
        "  \"fps\": 23.976,\n"
        "  \"durationS\": 0.500,\n"
        "  \"verified\": true,\n"
        "  \"team\": \"OR\",\n"
        "  \"code\": \"OR 16YD+\",\n"
        "  \"overlayLabel\": \"OR - área\",\n"
        "  \"clips\": [\n"
        "    {\n"
        "      \"ord\": 1,\n"
        "      \"offsetS\": 0.000,\n"
        "      \"durationS\": 0.500,\n"
        "      \"sourceStartS\": 57.840,\n"
        "      \"sourceEndS\": 58.040,\n"
        "      \"xmlInstanceId\": 3,\n"
        "      \"labels\": {\n"
        "        \"QUARTOS\": \"Q1\",\n"
        "        \"RESULTADO\": \"UC 0 - 0 OR\"\n"
        "      }\n"
        "    }\n"
        "  ]\n"
        "}\n");
    QCOMPARE(json, expected);
    QVERIFY(jsonBytes.contains(QStringLiteral("área").toUtf8()));
    QVERIFY(!jsonBytes.contains("\\u00e1"));
    QVERIFY(!json.contains(QStringLiteral("0,500")));
}

void CompilationSidecarOffsetTest::writesJsonAtomically() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString jsonPath = directory.filePath(QStringLiteral("plays.clips.json"));
    const QByteArray payload = QStringLiteral("{\"overlayLabel\": \"área\"}\n").toUtf8();
    QString error;
    QVERIFY2(CompilationSidecar::writeUtf8FileAtomically(jsonPath, payload, &error),
             qPrintable(error));

    QFile written(jsonPath);
    QVERIFY(written.open(QIODevice::ReadOnly));
    QCOMPARE(written.readAll(), payload);
    written.close();

    const QStringList leftovers = QDir(directory.path()).entryList(QDir::Files);
    QCOMPARE(leftovers, QStringList{QStringLiteral("plays.clips.json")});

    const QString videoPath = directory.filePath(QStringLiteral("UC B vs OR - 16-yd OR.mp4"));
    const QString sidecarPath = CompilationSidecar::clipsJsonPathForVideo(videoPath);
    QCOMPARE(QFileInfo(sidecarPath).fileName(), QStringLiteral("UC B vs OR - 16-yd OR.clips.json"));
    QCOMPARE(QFileInfo(sidecarPath).absolutePath(), QFileInfo(videoPath).absolutePath());
}

void CompilationSidecarOffsetTest::chapterMetadataUsesMillisecondTimebase() {
    const QString metadata = CompilationSidecar::renderChapterMetadata(
        {0, 500},
        {500, 1000},
        {QStringLiteral("1/2 · Q1"), QStringLiteral("2/2 · Q=2")});
    QVERIFY(metadata.startsWith(QStringLiteral(";FFMETADATA1\n")));
    QVERIFY(metadata.contains(QStringLiteral("TIMEBASE=1/1000\n")));
    QVERIFY(metadata.contains(QStringLiteral("START=0\n")));
    QVERIFY(metadata.contains(QStringLiteral("END=500\n")));
    QVERIFY(metadata.contains(QStringLiteral("title=1/2 · Q1\n")));
    QVERIFY(metadata.contains(QStringLiteral("START=500\n")));
    QVERIFY(metadata.contains(QStringLiteral("END=1500\n")));
    QVERIFY(metadata.contains(QStringLiteral("title=2/2 · Q\\=2\n")));
    QCOMPARE(CompilationSidecar::chapterTitle(2, 14, QStringLiteral("Q3")),
             QStringLiteral("2/14 · Q3"));
    QCOMPARE(CompilationSidecar::chapterTitle(2, 14, QString()), QStringLiteral("2/14"));
}

void CompilationSidecarOffsetTest::mixedTeamsStayInConcatOrder() {
    CompilationSidecar::SidecarBuildInput input;
    input.avaVersion = QStringLiteral("0.1.0+test");
    input.videoFileName = QStringLiteral("mixed.mp4");
    input.sourceVideoFileName = QStringLiteral("match.mp4");
    input.finalDurationSeconds = 2.0;
    input.framesPerSecond = 30.0;
    input.measuredDurationSeconds = {1.0, 1.0};
    ClipCompilationRecord away = recordWithSource(
        8000, QStringLiteral("OR"), QStringLiteral("OR 16YD+"), QStringLiteral("OR - 16-yd"));
    away.xmlInstanceId = 8;
    ClipCompilationRecord home = recordWithSource(
        1000, QStringLiteral("UC"), QStringLiteral("UC 16YD+"), QStringLiteral("UC - 16-yd"));
    home.xmlInstanceId = -1;
    home.labels.clear();
    input.clipsInConcatOrder = {away, home};

    const CompilationSidecar::SidecarDocument document = CompilationSidecar::buildDocument(input);
    QCOMPARE(document.clips.at(0).ordinal, 1);
    QCOMPARE(document.clips.at(0).sourceStartMilliseconds, qint64{8000});
    QCOMPARE(document.clips.at(1).ordinal, 2);
    QCOMPARE(document.clips.at(1).sourceStartMilliseconds, qint64{1000});
    QVERIFY(document.team.isEmpty());
    QVERIFY(document.code.isEmpty());
    QVERIFY(document.overlayLabel.isEmpty());

    const QString json = QString::fromUtf8(CompilationSidecar::renderJson(document));
    QVERIFY(json.contains(QStringLiteral("\"team\": null")));
    QVERIFY(json.contains(QStringLiteral("\"code\": null")));
    QVERIFY(json.contains(QStringLiteral("\"overlayLabel\": null")));
    QVERIFY(json.contains(QStringLiteral("\"xmlInstanceId\": null")));
    const int awayStart = json.indexOf(QStringLiteral("\"sourceStartS\": 8.000"));
    const int homeStart = json.indexOf(QStringLiteral("\"sourceStartS\": 1.000"));
    QVERIFY(awayStart >= 0);
    QVERIFY(awayStart < homeStart);
}

void CompilationSidecarOffsetTest::xmlInstanceIdMatchesCatalogNotCompilationIndex() {
    TagSession session;
    configureMatch(session);
    const TagSession::GameTag goal = appendTag(
        session, makeTag(QStringLiteral("Goal"), QStringLiteral("Home"), 200, 1200, 800,
                         QStringLiteral("Q1")));
    Q_UNUSED(goal);
    const TagSession::GameTag early = appendTag(
        session, makeTag(QStringLiteral("16-yd"), QStringLiteral("Away"), 1500, 2800, 2000,
                         QStringLiteral("Q1")));
    const TagSession::GameTag shortClip = appendTag(
        session, makeTag(QStringLiteral("16-yd"), QStringLiteral("Away"), 4000, 4200, 4100,
                         QStringLiteral("Q1")));
    const TagSession::GameTag late = appendTag(
        session, makeTag(QStringLiteral("16-yd"), QStringLiteral("Away"), 5000, 6800, 6000,
                         QStringLiteral("Q2")));

    QVector<PresentationQueue::Clip> ordered;
    ordered.append(presentationClipFrom(late, 3));
    ordered.append(presentationClipFrom(early, 1));
    ordered.append(presentationClipFrom(shortClip, 2));

    ExportClipBuilder::OverlayOptions options;
    options.includeBottomOverlay = true;
    options.includeScoreboardOverlay = false;
    options.includeNotesOverlay = false;
    const QVector<ClipSegment> segments =
        ExportClipBuilder::buildClipSegments(&session, ordered, options);

    QCOMPARE(segments.size(), 3);
    QCOMPARE(segments.at(0).overlayText, QStringLiteral("OR - 16-yd  1 / 3"));
    QCOMPARE(segments.at(1).overlayText, QStringLiteral("OR - 16-yd  2 / 3"));
    QCOMPARE(segments.at(2).overlayText, QStringLiteral("OR - 16-yd  3 / 3"));
    QCOMPARE(segments.at(0).compilation.sourceStartMs, late.startMs);
    QCOMPARE(segments.at(1).compilation.sourceStartMs, early.startMs);

    const QVector<XmlExporter::ExportedXmlInstance> catalog =
        XmlExporter::buildExportedInstances(&session);
    const std::optional<XmlExporter::ExportedXmlInstance> lateInstance =
        XmlExporter::taggedTeamInstanceFor(catalog, late.id, QStringLiteral("16-yd"));
    QVERIFY(lateInstance.has_value());
    QCOMPARE(lateInstance->code, QStringLiteral("OR 16YD+"));
    QCOMPARE(segments.at(0).compilation.xmlInstanceId, lateInstance->id);
    QVERIFY(lateInstance->id != 1);
    QCOMPARE(catalog.at(lateInstance->id).code, QStringLiteral("UC 16YD-"));
    QCOMPARE(catalog.at(lateInstance->id).id, lateInstance->id + 1);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString xmlPath = directory.filePath(QStringLiteral("match.xml"));
    QString error;
    QVERIFY2(XmlExporter::writeAllInstances(&session, xmlPath, &error), qPrintable(error));

    QFile xmlFile(xmlPath);
    QVERIFY(xmlFile.open(QIODevice::ReadOnly));
    QVector<int> writtenIds;
    QVector<QString> writtenCodes;
    QXmlStreamReader reader(&xmlFile);
    bool insideInstance = false;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement() && reader.name() == QLatin1String("instance")) {
            insideInstance = true;
            continue;
        }
        if (reader.isEndElement() && reader.name() == QLatin1String("instance")) {
            insideInstance = false;
            continue;
        }
        if (!insideInstance || !reader.isStartElement()) {
            continue;
        }
        if (reader.name() == QLatin1String("ID")) {
            writtenIds.append(reader.readElementText().toInt());
        } else if (reader.name() == QLatin1String("code")) {
            writtenCodes.append(reader.readElementText());
        }
    }
    QVERIFY(!reader.hasError());
    QCOMPARE(writtenIds.size(), catalog.size());
    QCOMPARE(writtenCodes.size(), catalog.size());
    for (int index = 0; index < catalog.size(); ++index) {
        QCOMPARE(writtenIds.at(index), catalog.at(index).id);
        QCOMPARE(writtenCodes.at(index), catalog.at(index).code);
    }
}

namespace {

QString quartosLabel(const XmlExporter::ExportedXmlInstance& instance) {
    for (const XmlExporter::ExportedXmlLabel& label : instance.labels) {
        if (label.group == QStringLiteral("QUARTOS")) return label.text;
    }
    return QString();
}

}  // namespace

void CompilationSidecarOffsetTest::quartosLabelUsesQuarterSpanContainingMark() {
    TagSession session;
    configureMatch(session);

    appendTag(session, makeTag(QStringLiteral("Q1"), QString(), 28592, 1124879, 28592,
                               QStringLiteral("Q1")));
    appendTag(session, makeTag(QStringLiteral("Q2"), QString(), 1124879, 2200000, 1124879,
                               QStringLiteral("Q2")));
    appendTag(session, makeTag(QStringLiteral("Q3"), QString(), 2200000, 3300000, 2200000,
                               QStringLiteral("Q3")));
    appendTag(session, makeTag(QStringLiteral("Q4"), QString(), 3300000, 4400000, 3300000,
                               QStringLiteral("Q4")));

    // Tagged after the match had already reached Q4; the play itself is inside Q1.
    const TagSession::GameTag retrospectiveGoal = appendTag(
        session, makeTag(QStringLiteral("Goal"), QStringLiteral("Away"),
                         290000, 304510, 296510, QStringLiteral("Q4")));
    // Stamped with an earlier quarter; the play is inside the closed Q4 span.
    const TagSession::GameTag fourthQuarterShot = appendTag(
        session, makeTag(QStringLiteral("Shot"), QStringLiteral("Home"),
                         3400000, 3410000, 3405000, QStringLiteral("Q1")));

    const QVector<XmlExporter::ExportedXmlInstance> catalog =
        XmlExporter::buildExportedInstances(&session);
    const std::optional<XmlExporter::ExportedXmlInstance> goalInstance =
        XmlExporter::taggedTeamInstanceFor(catalog, retrospectiveGoal.id, QStringLiteral("Goal"));
    const std::optional<XmlExporter::ExportedXmlInstance> shotInstance =
        XmlExporter::taggedTeamInstanceFor(catalog, fourthQuarterShot.id, QStringLiteral("Shot"));
    QVERIFY(goalInstance.has_value());
    QVERIFY(shotInstance.has_value());
    QCOMPARE(quartosLabel(*goalInstance), QStringLiteral("Q1"));
    QCOMPARE(quartosLabel(*shotInstance), QStringLiteral("Q4"));

    bool opposingGoalLabeled = false;
    for (const XmlExporter::ExportedXmlInstance& instance : catalog) {
        if (instance.sourceTagId != retrospectiveGoal.id) continue;
        if (instance.code != QStringLiteral("UC GOAL-")) continue;
        opposingGoalLabeled = true;
        QCOMPARE(quartosLabel(instance), QStringLiteral("Q1"));
    }
    QVERIFY(opposingGoalLabeled);
}

void CompilationSidecarOffsetTest::shortClipKeepsSourceIntervalAndHalfSecondEncode() {
    TagSession session;
    configureMatch(session);
    const TagSession::GameTag shortClip = appendTag(
        session, makeTag(QStringLiteral("16-yd"), QStringLiteral("Away"), 4000, 4200, 4100,
                         QStringLiteral("Q1")));
    ExportClipBuilder::OverlayOptions options;
    options.includeScoreboardOverlay = false;
    const QVector<ClipSegment> segments = ExportClipBuilder::buildClipSegments(
        &session, {presentationClipFrom(shortClip, 0)}, options);
    QCOMPARE(segments.size(), 1);
    QCOMPARE(segments.at(0).durationMs, qint64{500});
    QCOMPARE(segments.at(0).compilation.sourceEndMs - segments.at(0).compilation.sourceStartMs,
             qint64{200});
    QCOMPARE(segments.at(0).startMs, qint64{4000});
}

class CompilationSidecarExportTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void exportsSidecarBesideCompilation();
    void cancelBeforeOutputReplaceDoesNotWriteSidecar();
};

void CompilationSidecarExportTest::initTestCase() {
    if (FfmpegLocator::findFfmpeg().isEmpty() || FfmpegLocator::findFfprobe().isEmpty()) {
        QSKIP("ffmpeg and ffprobe are required for the compilation sidecar export test");
    }
}

void CompilationSidecarExportTest::exportsSidecarBesideCompilation() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString ffmpegPath = FfmpegLocator::findFfmpeg();
    const QString ffprobePath = FfmpegLocator::findFfprobe();
    const QString sourcePath = directory.filePath(QStringLiteral("full-match.mp4"));
    QString commandError;
    QVERIFY2(runProgram(ffmpegPath,
                        {QStringLiteral("-y"),
                         QStringLiteral("-f"), QStringLiteral("lavfi"),
                         QStringLiteral("-i"), QStringLiteral("testsrc=duration=8:size=640x360:rate=30"),
                         QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
                         QStringLiteral("-c:v"), QStringLiteral("libx264"),
                         QStringLiteral("-preset"), QStringLiteral("ultrafast"),
                         sourcePath},
                        60000,
                        &commandError),
             qPrintable(commandError));

    TagSession session;
    configureMatch(session);
    appendTag(session, makeTag(QStringLiteral("Q1"), QString(), 0, 4500, 0, QStringLiteral("Q1")));
    appendTag(session, makeTag(QStringLiteral("Q2"), QString(), 4500, 8000, 4500,
                               QStringLiteral("Q2")));
    appendTag(session, makeTag(QStringLiteral("Goal"), QStringLiteral("Home"), 200, 1200, 800,
                               QStringLiteral("Q1")));
    const TagSession::GameTag early = appendTag(
        session, makeTag(QStringLiteral("16-yd"), QStringLiteral("Away"), 1500, 2800, 2000,
                         QStringLiteral("Q1")));
    const TagSession::GameTag shortClip = appendTag(
        session, makeTag(QStringLiteral("16-yd"), QStringLiteral("Away"), 4000, 4200, 4100,
                         QStringLiteral("Q1")));
    const TagSession::GameTag late = appendTag(
        session, makeTag(QStringLiteral("16-yd"), QStringLiteral("Away"), 5000, 6800, 6000,
                         QStringLiteral("Q2")));

    ExportClipBuilder::OverlayOptions options;
    options.includeBottomOverlay = true;
    options.includeScoreboardOverlay = false;
    options.includeNotesOverlay = false;
    const QVector<ClipSegment> segments = ExportClipBuilder::buildClipSegments(
        &session,
        {presentationClipFrom(late, 3), presentationClipFrom(early, 1),
         presentationClipFrom(shortClip, 2)},
        options);
    QCOMPARE(segments.size(), 3);
    QCOMPARE(segments.at(0).overlayText, QStringLiteral("OR - 16-yd  1 / 3"));

    const QString outputPath =
        directory.filePath(QStringLiteral("UC B vs OR - 16-yd OR.mp4"));
    ExportJobRequest request;
    request.format = ExportOutputFormat::Both;
    request.sourceVideoPath = sourcePath;
    request.outputPath = outputPath;
    request.clips = segments;
    request.includeAudioTrack = false;
    request.includeBrandingOverlay = false;
    request.tagSession = &session;

    ExportJobManager manager;
    QString startError;
    QVERIFY2(manager.startJob(request, &startError), qPrintable(startError));
    QVERIFY2(waitUntilJobFinishes(manager, 180000), "export timed out");
    const ExportJobSnapshot snapshot = manager.snapshots().constFirst();
    QVERIFY2(!snapshot.failed, qPrintable(snapshot.statusText));

    const QString jsonPath = CompilationSidecar::clipsJsonPathForVideo(outputPath);
    QVERIFY(QFile::exists(outputPath));
    QVERIFY(QFile::exists(jsonPath));
    QCOMPARE(QFileInfo(jsonPath).fileName(), QStringLiteral("UC B vs OR - 16-yd OR.clips.json"));

    QFile jsonFile(jsonPath);
    QVERIFY(jsonFile.open(QIODevice::ReadOnly));
    const QByteArray jsonBytes = jsonFile.readAll();
    jsonFile.close();
    QVERIFY2(jsonBytes.contains(QStringLiteral("área").toUtf8()), jsonBytes.constData());
    QVERIFY(!jsonBytes.contains("\\u00e1"));

    const QJsonDocument parsed = QJsonDocument::fromJson(jsonBytes);
    QVERIFY2(parsed.isObject(), jsonBytes.constData());
    const QJsonObject root = parsed.object();
    QCOMPARE(root.value(QStringLiteral("schemaVersion")).toInt(), 1);
    QCOMPARE(root.value(QStringLiteral("video")).toString(),
             QStringLiteral("UC B vs OR - 16-yd OR.mp4"));
    QCOMPARE(root.value(QStringLiteral("sourceVideo")).toString(), QStringLiteral("full-match.mp4"));
    QCOMPARE(root.value(QStringLiteral("team")).toString(), QStringLiteral("OR"));
    QCOMPARE(root.value(QStringLiteral("code")).toString(), QStringLiteral("OR 16YD+"));
    QCOMPARE(root.value(QStringLiteral("overlayLabel")).toString(), QStringLiteral("OR - 16-yd"));
    QVERIFY(root.value(QStringLiteral("verified")).toBool());
    QVERIFY(root.value(QStringLiteral("avaVersion")).toString().contains(QStringLiteral("0.1.0")));

    const QString expectedXmlName =
        ExportClipBuilder::xmlReportBaseName(&session) + QStringLiteral(".xml");
    QCOMPARE(root.value(QStringLiteral("matchXml")).toString(), expectedXmlName);
    const QString xmlPath = directory.filePath(expectedXmlName);
    QVERIFY(QFile::exists(xmlPath));

    const double framesPerSecond = root.value(QStringLiteral("fps")).toDouble();
    QVERIFY(framesPerSecond > 29.0);
    QVERIFY(framesPerSecond < 31.0);

    const QJsonArray clips = root.value(QStringLiteral("clips")).toArray();
    QCOMPARE(clips.size(), segments.size());
    QCOMPARE(clips.size(), 3);

    QFile xmlFile(xmlPath);
    QVERIFY(xmlFile.open(QIODevice::ReadOnly));
    struct WrittenInstance {
        int id = 0;
        QString code;
        qint64 startMs = 0;
    };
    QVector<WrittenInstance> writtenInstances;
    QXmlStreamReader reader(&xmlFile);
    WrittenInstance current;
    bool inInstance = false;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement() && reader.name() == QLatin1String("instance")) {
            current = {};
            inInstance = true;
        } else if (reader.isEndElement() && reader.name() == QLatin1String("instance")) {
            if (inInstance) {
                writtenInstances.append(current);
            }
            inInstance = false;
        } else if (inInstance && reader.isStartElement()) {
            if (reader.name() == QLatin1String("ID")) {
                current.id = reader.readElementText().toInt();
            } else if (reader.name() == QLatin1String("code")) {
                current.code = reader.readElementText();
            } else if (reader.name() == QLatin1String("start")) {
                current.startMs = jsonSecondsToMilliseconds(reader.readElementText().toDouble());
            }
        }
    }
    QVERIFY(!reader.hasError());

    double runningEndSeconds = 0.0;
    bool foundShortClip = false;
    for (int index = 0; index < clips.size(); ++index) {
        const QJsonObject clip = clips.at(index).toObject();
        QCOMPARE(clip.value(QStringLiteral("ord")).toInt(), index + 1);
        const double offsetSeconds = clip.value(QStringLiteral("offsetS")).toDouble();
        const double durationSeconds = clip.value(QStringLiteral("durationS")).toDouble();
        QVERIFY(std::abs(offsetSeconds - runningEndSeconds) < 0.001);
        runningEndSeconds += durationSeconds;

        const qint64 sourceStartMs =
            jsonSecondsToMilliseconds(clip.value(QStringLiteral("sourceStartS")).toDouble());
        const qint64 sourceEndMs =
            jsonSecondsToMilliseconds(clip.value(QStringLiteral("sourceEndS")).toDouble());
        const double sourceSpanSeconds = (sourceEndMs - sourceStartMs) / 1000.0;
        if (std::abs(sourceSpanSeconds - 0.2) < 0.001) {
            foundShortClip = true;
            QVERIFY(durationSeconds >= 0.45);
            QVERIFY(durationSeconds < 0.9);
        }

        const int xmlInstanceId = clip.value(QStringLiteral("xmlInstanceId")).toInt();
        QVERIFY(xmlInstanceId > 0);
        bool matchedXml = false;
        for (const WrittenInstance& written : writtenInstances) {
            if (written.code == QStringLiteral("OR 16YD+") && written.startMs == sourceStartMs) {
                QCOMPARE(xmlInstanceId, written.id);
                matchedXml = true;
            }
        }
        QVERIFY(matchedXml);

        const QString jsonText = QString::fromUtf8(jsonBytes);
        const int competition = jsonText.indexOf(QStringLiteral("\"COMPETICION\""));
        const int resultado = jsonText.indexOf(QStringLiteral("\"RESULTADO\""));
        const int quartos = jsonText.indexOf(QStringLiteral("\"QUARTOS\""));
        const int year = jsonText.indexOf(QStringLiteral("\"ANO\""));
        QVERIFY(competition >= 0);
        QVERIFY(competition < resultado);
        QVERIFY(resultado < quartos);
        QVERIFY(quartos < year);
    }
    QVERIFY(foundShortClip);
    QCOMPARE(clips.at(0).toObject().value(QStringLiteral("ord")).toInt(), 1);
    QVERIFY(clips.at(0).toObject().value(QStringLiteral("xmlInstanceId")).toInt() != 1);

    const double finalDurationSeconds = root.value(QStringLiteral("durationS")).toDouble();
    QVERIFY(std::abs(finalDurationSeconds - runningEndSeconds) <= (2.0 / framesPerSecond));
    const std::optional<double> probedDuration =
        CompilationSidecar::probeMediaDurationSeconds(ffprobePath, outputPath);
    QVERIFY(probedDuration.has_value());
    const qint64 probedMilliseconds =
        CompilationSidecar::roundSecondsToMilliseconds(*probedDuration);
    const qint64 summedMilliseconds = jsonSecondsToMilliseconds(runningEndSeconds);
    QVERIFY(CompilationSidecar::finalDurationWithinTolerance(
        probedMilliseconds, summedMilliseconds, framesPerSecond,
        CompilationSidecar::kDurationToleranceFrames));

    QByteArray chaptersOutput;
    QVERIFY2(runProgram(ffprobePath,
                        {QStringLiteral("-v"), QStringLiteral("error"),
                         QStringLiteral("-show_chapters"),
                         QStringLiteral("-of"), QStringLiteral("json"),
                         outputPath},
                        15000,
                        &commandError,
                        &chaptersOutput),
             qPrintable(commandError));
    const QJsonArray chapters =
        QJsonDocument::fromJson(chaptersOutput).object().value(QStringLiteral("chapters")).toArray();
    QCOMPARE(chapters.size(), clips.size());
    const QStringList expectedTitles{
        QStringLiteral("1/3 · Q2"),
        QStringLiteral("2/3 · Q1"),
        QStringLiteral("3/3 · Q1"),
    };
    for (int index = 0; index < chapters.size(); ++index) {
        const QJsonObject chapter = chapters.at(index).toObject();
        QCOMPARE(chapter.value(QStringLiteral("tags")).toObject().value(QStringLiteral("title")).toString(),
                 expectedTitles.at(index));
        const double chapterStart = chapter.value(QStringLiteral("start_time")).toString().toDouble();
        const double clipOffset = clips.at(index).toObject().value(QStringLiteral("offsetS")).toDouble();
        QVERIFY(std::abs(chapterStart - clipOffset) < 0.02);
    }

    // Burned overlay spot-check. Chapter titles already lock k/N. These PNGs are
    // the frames at each offset so a reviewer can see "k / N" in the picture.
    // The test also checks that a frame just before clip 2 differs from a frame
    // inside clip 2, which is the seek landing on the next segment.
    const QString spotDirectory = QDir::temp().filePath(QStringLiteral("ava-sidecar-spotcheck"));
    QDir().mkpath(spotDirectory);
    QString previousFrameHash;
    QString frameBeforeBoundaryHash;
    QString frameInsideSecondClipHash;
    const double secondOffset = clips.at(1).toObject().value(QStringLiteral("offsetS")).toDouble();
    for (int index = 0; index < clips.size(); ++index) {
        const double offsetSeconds = clips.at(index).toObject().value(QStringLiteral("offsetS")).toDouble();
        const double durationSeconds =
            clips.at(index).toObject().value(QStringLiteral("durationS")).toDouble();
        const double seekSeconds = offsetSeconds + std::min(0.15, durationSeconds * 0.4);
        const QString framePath =
            QDir(spotDirectory).filePath(QStringLiteral("clip-%1.png").arg(index + 1));
        QVERIFY2(runProgram(ffmpegPath,
                            {QStringLiteral("-y"),
                             QStringLiteral("-ss"), QString::number(seekSeconds, 'f', 3),
                             QStringLiteral("-i"), outputPath,
                             QStringLiteral("-frames:v"), QStringLiteral("1"),
                             framePath},
                            20000,
                            &commandError),
                 qPrintable(commandError));
        QFile frameFile(framePath);
        QVERIFY(frameFile.open(QIODevice::ReadOnly));
        const QByteArray frameBytes = frameFile.readAll();
        frameFile.close();
        QVERIFY(frameBytes.size() > 1000);
        const QString frameHash = QString::fromLatin1(
            QCryptographicHash::hash(frameBytes, QCryptographicHash::Sha256).toHex());
        if (!previousFrameHash.isEmpty()) {
            QVERIFY(frameHash != previousFrameHash);
        }
        previousFrameHash = frameHash;
        if (index == 1) {
            frameInsideSecondClipHash = frameHash;
        }
    }
    const QString boundaryFramePath = QDir(spotDirectory).filePath(QStringLiteral("before-clip-2.png"));
    QVERIFY2(runProgram(ffmpegPath,
                        {QStringLiteral("-y"),
                         QStringLiteral("-ss"), QString::number(secondOffset - 0.08, 'f', 3),
                         QStringLiteral("-i"), outputPath,
                         QStringLiteral("-frames:v"), QStringLiteral("1"),
                         boundaryFramePath},
                        20000,
                        &commandError),
             qPrintable(commandError));
    QFile boundaryFile(boundaryFramePath);
    QVERIFY(boundaryFile.open(QIODevice::ReadOnly));
    frameBeforeBoundaryHash = QString::fromLatin1(
        QCryptographicHash::hash(boundaryFile.readAll(), QCryptographicHash::Sha256).toHex());
    boundaryFile.close();
    QVERIFY(frameBeforeBoundaryHash != frameInsideSecondClipHash);

    const QString examplePath = QDir::temp().filePath(QStringLiteral("ava-clips-sidecar-example.json"));
    QFile::remove(examplePath);
    QVERIFY(QFile::copy(jsonPath, examplePath));
    qInfo("sidecar example: %s", qPrintable(examplePath));
    qInfo("overlay frames: %s", qPrintable(spotDirectory));
    QCOMPARE(AvaVersion::current().isEmpty(), false);
}

void CompilationSidecarExportTest::cancelBeforeOutputReplaceDoesNotWriteSidecar() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString sourcePath = directory.filePath(QStringLiteral("full-match.mp4"));
    QString commandError;
    QVERIFY2(runProgram(FfmpegLocator::findFfmpeg(),
                        {QStringLiteral("-y"),
                         QStringLiteral("-f"), QStringLiteral("lavfi"),
                         QStringLiteral("-i"), QStringLiteral("testsrc=duration=6:size=320x180:rate=30"),
                         QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
                         QStringLiteral("-c:v"), QStringLiteral("libx264"),
                         QStringLiteral("-preset"), QStringLiteral("ultrafast"),
                         sourcePath},
                        60000,
                        &commandError),
             qPrintable(commandError));

    TagSession session;
    configureMatch(session);
    const TagSession::GameTag first = appendTag(
        session, makeTag(QStringLiteral("16-yd"), QStringLiteral("Away"), 0, 2000, 1000,
                         QStringLiteral("Q1")));
    const TagSession::GameTag second = appendTag(
        session, makeTag(QStringLiteral("16-yd"), QStringLiteral("Away"), 2000, 4000, 3000,
                         QStringLiteral("Q1")));
    const TagSession::GameTag third = appendTag(
        session, makeTag(QStringLiteral("16-yd"), QStringLiteral("Away"), 4000, 5500, 5000,
                         QStringLiteral("Q2")));
    ExportClipBuilder::OverlayOptions options;
    options.includeScoreboardOverlay = false;
    options.includeNotesOverlay = false;
    const QVector<ClipSegment> segments = ExportClipBuilder::buildClipSegments(
        &session,
        {presentationClipFrom(first, 0), presentationClipFrom(second, 1),
         presentationClipFrom(third, 2)},
        options);

    const QString outputPath = directory.filePath(QStringLiteral("cancelled.mp4"));
    const QString jsonPath = CompilationSidecar::clipsJsonPathForVideo(outputPath);
    QFile stale(jsonPath);
    QVERIFY(stale.open(QIODevice::WriteOnly));
    stale.write("{}\n");
    stale.close();

    ExportJobRequest request;
    request.format = ExportOutputFormat::Mp4;
    request.sourceVideoPath = sourcePath;
    request.outputPath = outputPath;
    request.clips = segments;
    request.includeAudioTrack = false;
    request.includeBrandingOverlay = false;
    request.tagSession = &session;

    ExportJobManager manager;
    QString startError;
    QVERIFY2(manager.startJob(request, &startError), qPrintable(startError));
    manager.cancelJob(manager.snapshots().constFirst().id);
    QVERIFY(waitUntilJobFinishes(manager, 30000));
    const ExportJobSnapshot snapshot = manager.snapshots().constFirst();
    QVERIFY(snapshot.statusText.contains(QStringLiteral("cancel"), Qt::CaseInsensitive));
    // Cancel happens during the first encode, before concat replaces the mp4, so a
    // pre-existing sidecar must still be there. A partial sidecar from this run must not.
    QVERIFY(QFile::exists(jsonPath));
    QFile leftover(jsonPath);
    QVERIFY(leftover.open(QIODevice::ReadOnly));
    QCOMPARE(leftover.readAll(), QByteArray("{}\n"));
}

#include "CompilationSidecarTests.moc"

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    int status = 0;
    {
        CompilationSidecarOffsetTest offsets;
        const int offsetStatus = QTest::qExec(&offsets, argc, argv);
        if (offsetStatus != 0) {
            return offsetStatus;
        }
        status |= offsetStatus;
    }
    {
        CompilationSidecarExportTest integration;
        status |= QTest::qExec(&integration, argc, argv);
    }
    return status;
}
