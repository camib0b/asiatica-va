#include "AppLocale.h"
#include "CompilationSidecar.h"
#include "EventCatalog.h"
#include "EventDefaults.h"
#include "ExportClipBuilder.h"
#include "TagEvent.h"
#include "TagSession.h"
#include "XmlEventMappingDialog.h"
#include "XmlExporter.h"
#include "XmlImporter.h"

#include <QApplication>
#include <QDate>
#include <QDialog>
#include <QEvent>
#include <QFile>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

namespace {

class WarningCapture : public QObject {
public:
    QString text;
    int seen = 0;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::Show) {
            if (auto* box = qobject_cast<QMessageBox*>(watched)) {
                text = box->text();
                ++seen;
                QTimer::singleShot(0, box, &QDialog::accept);
            }
        }
        return QObject::eventFilter(watched, event);
    }
};

class InstalledWarningCapture {
public:
    explicit InstalledWarningCapture(WarningCapture* capture) : capture_(capture) {
        qApp->installEventFilter(capture_);
    }
    ~InstalledWarningCapture() { qApp->removeEventFilter(capture_); }

private:
    WarningCapture* capture_;
};

void expectChain(const TagEvent::FollowUpChain& actual, const TagEvent::FollowUpChain& expected) {
    QCOMPARE(actual.size(), expected.size());
    for (int index = 0; index < expected.size(); ++index) {
        QCOMPARE(static_cast<int>(actual.at(index)), static_cast<int>(expected.at(index)));
    }
}

void expectPayloadRoundTrip(EventCatalog::EventId mainEventId, const TagEvent::FollowUpChain& chain,
                            const QString& teamLabel, const QString& homeTeamName,
                            const QString& awayTeamName, const QString& historicalPayload) {
    const QString payload = TagEvent::legacyFollowUpPayload(mainEventId, chain, teamLabel);
    QCOMPARE(payload, historicalPayload);
    const std::optional<TagEvent::FollowUpChain> parsed = TagEvent::parseLegacyFollowUpPayload(
        mainEventId, payload, homeTeamName, awayTeamName);
    QVERIFY(parsed.has_value());
    expectChain(*parsed, chain);
}

TagSession::GameTag manualTag(EventCatalog::EventId id, const QString& team, qint64 markMs,
                              const TagEvent::FollowUpChain& chain = {}) {
    TagSession::GameTag tag;
    tag.setMainEventKey(TagEvent::catalogKey(id));
    tag.setFollowUpChain(chain);
    tag.team = team;
    tag.markMs = markMs;
    tag.startMs = markMs > 1000 ? markMs - 1000 : 0;
    tag.endMs = markMs + 1000;
    tag.intervalManuallyEdited = true;
    tag.period = QStringLiteral("Q1");
    return tag;
}

TagSession::GameTag timeControlTag(const char* code, qint64 startMs, qint64 endMs) {
    TagSession::GameTag tag;
    tag.setMainEventKey(TagEvent::unlistedKey(QString::fromLatin1(code)));
    tag.markMs = startMs;
    tag.startMs = startMs;
    tag.endMs = endMs;
    tag.intervalManuallyEdited = true;
    return tag;
}

void configureMatch(TagSession& session) {
    session.setGameTeams(QStringLiteral("UC"), QStringLiteral("OR"), QStringLiteral("#3c5ac8"),
                         QStringLiteral("#c83c3c"));
    session.setGameMetadata(QDate(2026, 9, 27), QStringLiteral("UC"), QStringLiteral("OR"));
}

const TagSession::GameTag* tagWithId(const QVector<TagSession::GameTag>& tags,
                                     EventCatalog::EventId id) {
    for (const TagSession::GameTag& tag : tags) {
        if (tag.mainEventId == id) return &tag;
    }
    return nullptr;
}

const TagSession::GameTag* tagWithUnlistedName(const QVector<TagSession::GameTag>& tags,
                                               const QString& name) {
    for (const TagSession::GameTag& tag : tags) {
        if (tag.mainEventId == EventCatalog::EventId::None && tag.unlistedName == name) return &tag;
    }
    return nullptr;
}

QString exportedXml(TagSession& session) {
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("match.xml"));
    QString error;
    if (!XmlExporter::writeAllInstances(&session, path, &error)) return {};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(file.readAll());
}

}  // namespace

class TagEventRoundTripTest : public QObject {
    Q_OBJECT

private slots:
    void followUpPayloadRoundTripsForEachLayout();
    void xmlExportUsesEnglishCodesAndImportRestoresIds();
    void scoringFollowUpStillExportsAnEnglishGoalCode();
    void clipsJsonUsesEnglishNamesAndCodesThatMapBackToIds();
};

void TagEventRoundTripTest::followUpPayloadRoundTripsForEachLayout() {
    const QString arrow = AppLocale::kCompoundPathSeparator;
    const QString home = QStringLiteral("UC");
    const QString away = QStringLiteral("OR");

    expectPayloadRoundTrip(EventCatalog::EventId::Shot,
                           TagEvent::chainOf(EventCatalog::EventId::OnTarget,
                                             EventCatalog::EventId::FollowUpGoal),
                           home, home, away,
                           QStringLiteral("On target") + arrow + QStringLiteral("Goal"));
    expectPayloadRoundTrip(EventCatalog::EventId::Shootout,
                           TagEvent::chainOf(EventCatalog::EventId::Converted), home, home, away,
                           home + arrow + QStringLiteral("Converted"));
    expectPayloadRoundTrip(EventCatalog::EventId::Card, TagEvent::chainOf(EventCatalog::EventId::Green),
                           home, home, away, QStringLiteral("Green") + arrow + home);
    expectPayloadRoundTrip(EventCatalog::EventId::Goal, {}, home, home, away, home);
    expectPayloadRoundTrip(EventCatalog::EventId::Pass,
                           TagEvent::chainOf(EventCatalog::EventId::Flick,
                                             EventCatalog::EventId::Completed),
                           home, home, away,
                           QStringLiteral("Flick") + arrow + QStringLiteral("Completed"));
    expectPayloadRoundTrip(EventCatalog::EventId::CircleEntry,
                           TagEvent::chainOf(EventCatalog::EventId::Dribbling,
                                             EventCatalog::EventId::Left),
                           home, home, away,
                           home + arrow + QStringLiteral("Dribbling") + arrow + QStringLiteral("Left"));
    expectPayloadRoundTrip(EventCatalog::EventId::SeventyFiveYard,
                           TagEvent::chainOf(EventCatalog::EventId::Forward), home, home, away,
                           QStringLiteral("Forward") + arrow + home);

    const TagEvent::FollowUpChain shotGoal = TagEvent::chainOf(
        EventCatalog::EventId::OnTarget, EventCatalog::EventId::FollowUpGoal);
    const QString plainShot = TagEvent::legacyFollowUpPayload(EventCatalog::EventId::Shot, shotGoal,
                                                             QStringLiteral("On target"));
    QCOMPARE(plainShot, QStringLiteral("On target") + arrow + QStringLiteral("Goal"));
    const std::optional<TagEvent::FollowUpChain> plainParsed =
        TagEvent::parseLegacyFollowUpPayload(EventCatalog::EventId::Shot, plainShot,
                                             QStringLiteral("On target"), away);
    QVERIFY(plainParsed.has_value());
    expectChain(*plainParsed, shotGoal);

    const QString collidingCircle = QStringLiteral("Left") + arrow + QStringLiteral("Dribbling") +
                                    arrow + QStringLiteral("Left");
    const std::optional<TagEvent::FollowUpChain> circleParsed =
        TagEvent::parseLegacyFollowUpPayload(EventCatalog::EventId::CircleEntry, collidingCircle,
                                             QStringLiteral("Left"), away);
    QVERIFY(circleParsed.has_value());
    expectChain(*circleParsed, TagEvent::chainOf(EventCatalog::EventId::Dribbling,
                                                 EventCatalog::EventId::Left));

    const QString collidingCard = QStringLiteral("Green") + arrow + QStringLiteral("Green");
    const std::optional<TagEvent::FollowUpChain> cardParsed = TagEvent::parseLegacyFollowUpPayload(
        EventCatalog::EventId::Card, collidingCard, QStringLiteral("Green"), away);
    QVERIFY(cardParsed.has_value());
    expectChain(*cardParsed, TagEvent::chainOf(EventCatalog::EventId::Green));

    const std::optional<TagEvent::FollowUpChain> spanishShot =
        TagEvent::parseLegacyFollowUpPayload(
            EventCatalog::EventId::Shot, QStringLiteral("Al arco") + arrow + QStringLiteral("Gol"),
            home, away);
    QVERIFY(spanishShot.has_value());
    expectChain(*spanishShot, shotGoal);

    const std::optional<TagEvent::FollowUpChain> spanishShootout =
        TagEvent::parseLegacyFollowUpPayload(
            EventCatalog::EventId::Shootout,
            QStringLiteral("Universidad Católica") + arrow + QStringLiteral("Convertido"),
            QStringLiteral("Universidad Católica"), away);
    QVERIFY(spanishShootout.has_value());
    expectChain(*spanishShootout, TagEvent::chainOf(EventCatalog::EventId::Converted));

    const std::optional<TagEvent::FollowUpChain> missed = TagEvent::parseLegacyFollowUpPayload(
        EventCatalog::EventId::Shootout, QStringLiteral("Side") + arrow + QStringLiteral("No convertido"),
        QStringLiteral("Side"), away);
    QVERIFY(missed.has_value());
    expectChain(*missed, TagEvent::chainOf(EventCatalog::EventId::Missed));

    const std::optional<TagEvent::FollowUpChain> replay = TagEvent::parseLegacyFollowUpPayload(
        EventCatalog::EventId::Shootout, QStringLiteral("Repite"), home, away);
    QVERIFY(replay.has_value());
    expectChain(*replay, TagEvent::chainOf(EventCatalog::EventId::Replay));

    QVERIFY(!TagEvent::parseLegacyFollowUpPayload(
                 EventCatalog::EventId::Shootout,
                 QStringLiteral("Side") + arrow + QStringLiteral("No Goal"), QStringLiteral("Side"),
                 away)
                 .has_value());
    QVERIFY(!TagEvent::parseLegacyFollowUpPayload(
                 EventCatalog::EventId::Shot,
                 QStringLiteral("On target") + arrow + QStringLiteral("Saved") + arrow +
                     QStringLiteral("Extra"),
                 home, away)
                 .has_value());
    QVERIFY(!TagEvent::parseLegacyFollowUpPayload(EventCatalog::EventId::Shot,
                                                  QStringLiteral("Not a thing"), home, away)
                 .has_value());
    const std::optional<TagEvent::FollowUpChain> emptyShot = TagEvent::parseLegacyFollowUpPayload(
        EventCatalog::EventId::Shot, QString(), home, away);
    QVERIFY(emptyShot.has_value());
    QVERIFY(emptyShot->empty());

    QCOMPARE(TagEvent::displayLabel(EventCatalog::EventId::SixteenYard, AppLocale::Language::English),
             QStringLiteral("16-yd"));
    QCOMPARE(TagEvent::displayLabel(EventCatalog::EventId::Special, AppLocale::Language::English),
             QString(QChar(0x2606)));
    QCOMPARE(TagEvent::displayLabel(EventCatalog::EventId::Special, AppLocale::Language::Spanish),
             QString(QChar(0x2606)));
    QCOMPARE(TagEvent::displayLabel(EventCatalog::EventId::Shot, AppLocale::Language::Spanish),
             QStringLiteral("Tiro"));
    QCOMPARE(TagEvent::displayLabel(EventCatalog::EventId::FollowUpGoal, AppLocale::Language::Spanish),
             QStringLiteral("Gol"));
    QCOMPARE(TagEvent::displayLabel(EventCatalog::EventId::Goal, AppLocale::Language::Spanish),
             QStringLiteral("Gol"));
}

void TagEventRoundTripTest::xmlExportUsesEnglishCodesAndImportRestoresIds() {
    WarningCapture warnings;
    InstalledWarningCapture capture(&warnings);

    TagSession session;
    configureMatch(session);
    session.addTag(timeControlTag(EventDefaults::TimeCodes::kStartAnchor, 0, 2000));
    session.addTag(timeControlTag(EventDefaults::TimeCodes::kQuarter1, 0, 60000));
    session.addTag(manualTag(EventCatalog::EventId::SixteenYard, QStringLiteral("Away"), 10000,
                             TagEvent::chainOf(EventCatalog::EventId::ThreeMan)));
    session.addTag(manualTag(EventCatalog::EventId::Card, QStringLiteral("Home"), 20000,
                             TagEvent::chainOf(EventCatalog::EventId::Green)));
    session.addTag(manualTag(EventCatalog::EventId::Shot, QStringLiteral("Home"), 30000,
                             TagEvent::chainOf(EventCatalog::EventId::OnTarget,
                                               EventCatalog::EventId::Saved)));
    session.addTag(manualTag(EventCatalog::EventId::Goal, QStringLiteral("Home"), 40000));

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString xmlPath = directory.filePath(QStringLiteral("match.xml"));
    QString error;
    QVERIFY2(XmlExporter::writeAllInstances(&session, xmlPath, &error), qPrintable(error));

    QFile xmlFile(xmlPath);
    QVERIFY(xmlFile.open(QIODevice::ReadOnly));
    const QString xml = QString::fromUtf8(xmlFile.readAll());
    QVERIFY(xml.contains(QStringLiteral("OR 16YD+")));
    QVERIFY(xml.contains(QStringLiteral("UC SHOT+")));
    QVERIFY(xml.contains(QStringLiteral("UC CARD+")));
    QVERIFY(xml.contains(QStringLiteral("UC GOAL+")));
    QVERIFY(xml.contains(QStringLiteral(">Q1<")));
    QVERIFY(xml.contains(QStringLiteral(">Inicio<")));
    QVERIFY(!xml.contains(QStringLiteral("On target")));
    QVERIFY(!xml.contains(QStringLiteral("Saved")));
    QVERIFY(!xml.contains(QStringLiteral("Direct shot")));
    QVERIFY(!xml.contains(QStringLiteral("\u2192")));

    QVector<XmlImporter::ParsedInstance> instances;
    QVERIFY2(XmlImporter::parse(xmlPath, &instances, &error), qPrintable(error));
    XmlEventMappingDialog dialog(instances, 0, &session);
    auto* table = dialog.findChild<QTableWidget*>(QStringLiteral("XmlEventMappingTable"));
    QVERIFY(table);
    // The exporter writes a "+" instance for the tagged team and a "-" instance for
    // the other team. The mapping dialog imports a "-" row only when that same
    // abbreviation has no "+" row, and a "-" row has no team until the analyst
    // picks one. Leave those opposing rows out, which is how a round-trip keeps
    // one tag per event.
    for (int row = 0; row < table->rowCount(); ++row) {
        const QTableWidgetItem* codeItem = table->item(row, 0);
        QTableWidgetItem* importItem = table->item(row, 4);
        QVERIFY(codeItem);
        QVERIFY(importItem);
        if (codeItem->text().trimmed().endsWith(QLatin1Char('-'))) {
            importItem->setCheckState(Qt::Unchecked);
        }
    }
    auto* importButton = dialog.findChild<QPushButton*>(QStringLiteral("XmlMappingImportButton"));
    QVERIFY(importButton);
    QVERIFY(importButton->isEnabled());
    importButton->click();
    QVERIFY2(warnings.seen == 0, qPrintable(warnings.text));
    QCOMPARE(dialog.result(), static_cast<int>(QDialog::Accepted));

    const QVector<TagSession::GameTag> imported = dialog.importResult().tags;
    const TagSession::GameTag* sixteen = tagWithId(imported, EventCatalog::EventId::SixteenYard);
    const TagSession::GameTag* card = tagWithId(imported, EventCatalog::EventId::Card);
    const TagSession::GameTag* shot = tagWithId(imported, EventCatalog::EventId::Shot);
    const TagSession::GameTag* goal = tagWithId(imported, EventCatalog::EventId::Goal);
    const TagSession::GameTag* start =
        tagWithUnlistedName(imported, QString::fromLatin1(EventDefaults::TimeCodes::kStartAnchor));
    const TagSession::GameTag* quarter =
        tagWithUnlistedName(imported, QString::fromLatin1(EventDefaults::TimeCodes::kQuarter1));
    QVERIFY(sixteen);
    QVERIFY(card);
    QVERIFY(shot);
    QVERIFY(goal);
    QVERIFY(start);
    QVERIFY(quarter);
    QCOMPARE(sixteen->team, QStringLiteral("Away"));
    QCOMPARE(shot->team, QStringLiteral("Home"));
    QCOMPARE(goal->team, QStringLiteral("Home"));
    QVERIFY(shot->followUpChain().empty());
    QVERIFY(sixteen->followUpChain().empty());
    QCOMPARE(start->mainEventId, EventCatalog::EventId::None);
    QCOMPARE(quarter->unlistedName, QStringLiteral("Q1"));
}

void TagEventRoundTripTest::scoringFollowUpStillExportsAnEnglishGoalCode() {
    TagSession session;
    configureMatch(session);
    session.addTag(manualTag(EventCatalog::EventId::Shot, QStringLiteral("Home"), 30000,
                             TagEvent::chainOf(EventCatalog::EventId::OnTarget,
                                               EventCatalog::EventId::FollowUpGoal)));

    const QString xml = exportedXml(session);
    QVERIFY(xml.contains(QStringLiteral("UC SHOT+")));
    QVERIFY(xml.contains(QStringLiteral("UC GOAL+")));
    QVERIFY(!xml.contains(QStringLiteral("On target")));
    QVERIFY(!xml.contains(QStringLiteral("\u2192")));
}

void TagEventRoundTripTest::clipsJsonUsesEnglishNamesAndCodesThatMapBackToIds() {
    TagSession session;
    configureMatch(session);
    session.addTag(manualTag(EventCatalog::EventId::SixteenYard, QStringLiteral("Away"), 2000));
    const TagSession::GameTag stored = session.tags().constLast();

    PresentationQueue::Clip clip;
    clip.tagSessionIndex = 0;
    clip.tagId = stored.id;
    clip.markMs = stored.markMs;
    clip.startMs = stored.startMs;
    clip.endMs = stored.endMs;
    clip.mainEvent = stored.mainEventKey();
    clip.followUps = stored.followUpChain();
    clip.team = stored.team;

    ExportClipBuilder::OverlayOptions options;
    options.includeBottomOverlay = true;
    options.includeScoreboardOverlay = false;
    options.includeNotesOverlay = false;
    const QVector<ClipSegment> segments =
        ExportClipBuilder::buildClipSegments(&session, {clip}, options);
    QCOMPARE(segments.size(), 1);
    QCOMPARE(segments.at(0).overlayText, QStringLiteral("OR - 16-yd  1 / 1"));
    QCOMPARE(segments.at(0).compilation.code, QStringLiteral("OR 16YD+"));
    QCOMPARE(segments.at(0).compilation.overlayLabel, QStringLiteral("OR - 16-yd"));

    CompilationSidecar::SidecarBuildInput input;
    input.avaVersion = QStringLiteral("test");
    input.videoFileName = QStringLiteral("match.mp4");
    input.sourceVideoFileName = QStringLiteral("source.mp4");
    input.measuredDurationSeconds = {1.0};
    input.clipsInConcatOrder = {segments.at(0).compilation};
    const QString json =
        QString::fromUtf8(CompilationSidecar::renderJson(CompilationSidecar::buildDocument(input)));
    QVERIFY(json.contains(QStringLiteral("\"code\": \"OR 16YD+\"")));
    QVERIFY(json.contains(QStringLiteral("\"overlayLabel\": \"OR - 16-yd\"")));

    const TagEvent::MainEventKey sixteen =
        TagEvent::mainEventKeyFromExportedCode(QStringLiteral("OR 16YD+"));
    QCOMPARE(sixteen.id, EventCatalog::EventId::SixteenYard);
    QVERIFY(sixteen.unlistedName.isEmpty());

    const TagEvent::MainEventKey goal =
        TagEvent::mainEventKeyFromExportedCode(QStringLiteral("uc goal-"));
    QCOMPARE(goal.id, EventCatalog::EventId::Goal);

    const TagEvent::MainEventKey shot =
        TagEvent::mainEventKeyFromExportedCode(QStringLiteral("  home shot+  "));
    QCOMPARE(shot.id, EventCatalog::EventId::Shot);

    const TagEvent::MainEventKey bareShot =
        TagEvent::mainEventKeyFromExportedCode(QStringLiteral("Shot"));
    QCOMPARE(bareShot.id, EventCatalog::EventId::Shot);

    const TagEvent::MainEventKey quarter =
        TagEvent::mainEventKeyFromExportedCode(QStringLiteral("Q1"));
    QCOMPARE(quarter.id, EventCatalog::EventId::None);
    QCOMPARE(quarter.unlistedName, QStringLiteral("Q1"));

    const TagEvent::MainEventKey start =
        TagEvent::mainEventKeyFromExportedCode(QStringLiteral("Inicio"));
    QCOMPARE(start.id, EventCatalog::EventId::None);
    QCOMPARE(start.unlistedName, QStringLiteral("Inicio"));
}

int runTagEventRoundTripTests(int argc, char** argv) {
    TagEventRoundTripTest tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "TagEventRoundTripTests.moc"
