#include "CompilationSidecar.h"
#include "EventCatalog.h"
#include "EventControlsSetup.h"
#include "EventControlsTemplateStore.h"
#include "EventDefaults.h"
#include "ExportClipBuilder.h"
#include "MatchSessionFile.h"
#include "PresentationQueue.h"
#include "TagSession.h"
#include "Telestration.h"
#include "XmlExporter.h"

#include <QCoreApplication>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

namespace {

class CurrentDirectoryGuard {
 public:
  explicit CurrentDirectoryGuard(const QString& path) : previous_(QDir::currentPath()) {
    QDir::setCurrent(path);
  }
  ~CurrentDirectoryGuard() { QDir::setCurrent(previous_); }

  CurrentDirectoryGuard(const CurrentDirectoryGuard&) = delete;
  CurrentDirectoryGuard& operator=(const CurrentDirectoryGuard&) = delete;

 private:
  QString previous_;
};

bool writeBytes(const QString& path, const QByteArray& bytes) {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
  return file.write(bytes) == bytes.size();
}

QByteArray readBytes(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return {};
  return file.readAll();
}

bool createEmptyFile(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly)) return false;
  file.close();
  return true;
}

void expectSameTag(const TagSession::GameTag& actual, const TagSession::GameTag& expected) {
  QCOMPARE(actual.id, expected.id);
  QCOMPARE(actual.mainEventId, expected.mainEventId);
  QCOMPARE(actual.unlistedName, expected.unlistedName);
  QCOMPARE(actual.followUpCount, expected.followUpCount);
  for (int index = 0; index < expected.followUpCount; ++index) {
    QCOMPARE(actual.followUpIds[static_cast<std::size_t>(index)],
             expected.followUpIds[static_cast<std::size_t>(index)]);
  }
  QCOMPARE(actual.markMs, expected.markMs);
  QCOMPARE(actual.startMs, expected.startMs);
  QCOMPARE(actual.endMs, expected.endMs);
  QCOMPARE(actual.note, expected.note);
  QCOMPARE(actual.period, expected.period);
  QCOMPARE(actual.team, expected.team);
  QCOMPARE(actual.intervalManuallyEdited, expected.intervalManuallyEdited);
}

void expectSamePoint(const Telestration::Point& actual, const Telestration::Point& expected) {
  QCOMPARE(actual.x, expected.x);
  QCOMPARE(actual.y, expected.y);
}

void expectSameDrawing(const Telestration::Drawing& actual, const Telestration::Drawing& expected) {
  QCOMPARE(actual.id, expected.id);
  QCOMPARE(actual.tagId.has_value(), expected.tagId.has_value());
  if (expected.tagId.has_value()) QCOMPARE(*actual.tagId, *expected.tagId);
  QCOMPARE(actual.anchorMs, expected.anchorMs);
  QCOMPARE(actual.holdMs, expected.holdMs);
  QCOMPARE(actual.shapes.size(), expected.shapes.size());
  for (int index = 0; index < expected.shapes.size(); ++index) {
    const Telestration::Shape& actualShape = actual.shapes.at(index);
    const Telestration::Shape& expectedShape = expected.shapes.at(index);
    QCOMPARE(actualShape.kind, expectedShape.kind);
    QCOMPARE(actualShape.color, expectedShape.color);
    QCOMPARE(actualShape.strokeWidth, expectedShape.strokeWidth);
    QCOMPARE(actualShape.points.size(), expectedShape.points.size());
    for (int pointIndex = 0; pointIndex < expectedShape.points.size(); ++pointIndex) {
      expectSamePoint(actualShape.points.at(pointIndex), expectedShape.points.at(pointIndex));
    }
  }
}

QByteArray xmlDocumentBytes(const TagSession& session) {
  QTemporaryDir directory;
  if (!directory.isValid()) return {};
  const QString path = directory.filePath(QStringLiteral("match.xml"));
  QString error;
  if (!XmlExporter::writeAllInstances(&session, path, &error)) return {};
  return readBytes(path);
}

QVector<PresentationQueue::Clip> clipsFromQueue(const PresentationQueue& queue) {
  QVector<PresentationQueue::Clip> clips;
  clips.reserve(queue.count());
  for (int index = 0; index < queue.count(); ++index) clips.append(queue.clipAt(index));
  return clips;
}

PresentationQueue::Clip clipForTag(const PresentationQueue& queue, quint64 tagId) {
  for (int index = 0; index < queue.count(); ++index) {
    if (queue.clipAt(index).tagId == tagId) return queue.clipAt(index);
  }
  return {};
}

QByteArray clipsJsonBytes(const TagSession& session, const QVector<PresentationQueue::Clip>& clips) {
  ExportClipBuilder::OverlayOptions options;
  options.includeBottomOverlay = true;
  options.includeScoreboardOverlay = false;
  options.includeNotesOverlay = false;
  options.includeDrawings = false;
  const QVector<ClipSegment> segments =
      ExportClipBuilder::buildClipSegments(&session, clips, options);

  CompilationSidecar::SidecarBuildInput input;
  input.avaVersion = QStringLiteral("test");
  input.videoFileName = QStringLiteral("match.mp4");
  input.sourceVideoFileName = QStringLiteral("source.mp4");
  input.matchXmlFileName = QStringLiteral("match.xml");
  input.framesPerSecond = 30.0;
  input.measuredDurationSeconds.reserve(segments.size());
  input.clipsInConcatOrder.reserve(segments.size());
  double totalSeconds = 0.0;
  for (const ClipSegment& segment : segments) {
    input.clipsInConcatOrder.append(segment.compilation);
    const double seconds = static_cast<double>(segment.durationMs) / 1000.0;
    input.measuredDurationSeconds.append(seconds);
    totalSeconds += seconds;
  }
  input.finalDurationSeconds = totalSeconds;
  return CompilationSidecar::renderJson(CompilationSidecar::buildDocument(input));
}

TagSession::GameTag shotTag() {
  TagSession::GameTag tag;
  tag.id = 4;
  tag.mainEventId = EventCatalog::EventId::Shot;
  tag.followUpCount = 2;
  tag.followUpIds[0] = EventCatalog::EventId::OnTarget;
  tag.followUpIds[1] = EventCatalog::EventId::FollowUpGoal;
  tag.markMs = 9000;
  tag.startMs = 4000;
  tag.endMs = 14000;
  tag.note = QStringLiteral("Near post");
  tag.period = QStringLiteral("Q2");
  tag.team = QStringLiteral("Home");
  tag.intervalManuallyEdited = true;
  return tag;
}

TagSession::GameTag quarterTag() {
  TagSession::GameTag tag;
  tag.id = 2;
  tag.mainEventId = EventCatalog::EventId::None;
  tag.unlistedName = QStringLiteral("Q1");
  tag.markMs = 0;
  tag.startMs = 0;
  tag.endMs = 9000;
  tag.intervalManuallyEdited = true;
  return tag;
}

Telestration::Drawing taggedArrow() {
  Telestration::Shape shape;
  shape.kind = Telestration::ShapeKind::Arrow;
  shape.color = Telestration::InkColor::Blue;
  shape.strokeWidth = 0.125;
  shape.points = {Telestration::Point{0.25, 0.5}, Telestration::Point{0.75, 0.5}};

  Telestration::Drawing drawing;
  drawing.id = 3;
  drawing.tagId = 4;
  drawing.anchorMs = 9000;
  drawing.holdMs = 2500;
  drawing.shapes = {shape};
  return drawing;
}

Telestration::Drawing freehandMark() {
  Telestration::Shape shape;
  shape.kind = Telestration::ShapeKind::Freehand;
  shape.color = Telestration::InkColor::White;
  shape.strokeWidth = Telestration::kDefaultStrokeWidth;
  shape.points = {Telestration::Point{0.125, 0.875}};

  Telestration::Drawing drawing;
  drawing.id = 8;
  drawing.anchorMs = 1000;
  drawing.holdMs = 3000;
  drawing.shapes = {shape};
  return drawing;
}

Telestration::Drawing circleMark() {
  Telestration::Shape shape;
  shape.kind = Telestration::ShapeKind::Circle;
  shape.color = Telestration::InkColor::Red;
  shape.strokeWidth = 0.25;
  shape.points = {Telestration::Point{0.5, 0.25}, Telestration::Point{0.5, 0.75}};

  Telestration::Drawing drawing;
  drawing.id = 9;
  drawing.anchorMs = 2000;
  drawing.holdMs = 1500;
  drawing.shapes = {shape};
  return drawing;
}

Telestration::Drawing dottedArrow() {
  Telestration::Shape shape;
  shape.kind = Telestration::ShapeKind::DottedArrow;
  shape.color = Telestration::InkColor::Black;
  shape.strokeWidth = 0.125;
  shape.points = {Telestration::Point{0.125, 0.125}, Telestration::Point{0.875, 0.875}};

  Telestration::Drawing drawing;
  drawing.id = 10;
  drawing.anchorMs = 0;
  drawing.holdMs = 4000;
  drawing.shapes = {shape};
  return drawing;
}

void writeDurationGroup(QSettings& settings, const QString& group, qint64 leadMs, qint64 lagMs) {
  settings.beginGroup(group);
  settings.setValue(QStringLiteral("preMs"), static_cast<qlonglong>(leadMs));
  settings.setValue(QStringLiteral("postMs"), static_cast<qlonglong>(lagMs));
  settings.endGroup();
}

QString legacySettingsGroup(const QString& canonicalName) {
  return QString::fromLatin1(canonicalName.toUtf8().toPercentEncoding("-_"));
}

const char* kMinimalSession =
    "{\n"
    "  \"format\": \"ava-match-session\",\n"
    "  \"version\": 1,\n"
    "  \"tags\": [],\n"
    "  \"coachNote\": \"ignored\"\n"
    "}\n";

}  // namespace

class MatchSessionFileTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase();
  void cleanup();
  void cleanupTestCase();

  void roundTripRestoresMatchAndKeepsExportBytes();
  void missingCorruptAndNewerFilesStaySafe();
  void templatesSaveApplyAndProtectTheBuiltinLayout();
  void qsettingsNameKeysMigrateToCatalogIds();

 private:
  QString previousOrganizationName_;
  QString previousApplicationName_;
};

void MatchSessionFileTest::initTestCase() {
  previousOrganizationName_ = QCoreApplication::organizationName();
  previousApplicationName_ = QCoreApplication::applicationName();
  QCoreApplication::setOrganizationName(QStringLiteral("AsiaticaVA"));
  QCoreApplication::setApplicationName(QStringLiteral("AVAMatchSessionTests"));
  EventDefaults::clearUserOverrides();
}

void MatchSessionFileTest::cleanup() { EventDefaults::clearUserOverrides(); }

void MatchSessionFileTest::cleanupTestCase() {
  EventDefaults::clearUserOverrides();
  QString settingsPath;
  {
    QSettings settings;
    settingsPath = settings.fileName();
    settings.clear();
    settings.sync();
  }
  QFile::remove(settingsPath);
  QCoreApplication::setOrganizationName(previousOrganizationName_);
  QCoreApplication::setApplicationName(previousApplicationName_);
  EventDefaults::loadFromSettings();
}

void MatchSessionFileTest::roundTripRestoresMatchAndKeepsExportBytes() {
  QTemporaryDir mediaDirectory;
  QVERIFY(mediaDirectory.isValid());
  const QString firstVideo = mediaDirectory.filePath(QStringLiteral("first.mp4"));
  const QString secondVideo = mediaDirectory.filePath(QStringLiteral("second.mp4"));
  QVERIFY(createEmptyFile(firstVideo));
  QVERIFY(createEmptyFile(secondVideo));
  const QStringList sourcePaths{firstVideo, secondVideo};

  QTemporaryDir storageDirectory;
  QVERIFY(storageDirectory.isValid());
  const QString sessionPath = MatchSessionFile::sessionFilePath(storageDirectory.path(), sourcePaths);
  QVERIFY(!sessionPath.isEmpty());
  QCOMPARE(MatchSessionFile::loadFile(sessionPath).status, MatchSessionFile::LoadStatus::Missing);

  {
    CurrentDirectoryGuard guard(mediaDirectory.path());
    const QString fromRelative = MatchSessionFile::sessionFilePath(
        storageDirectory.path(),
        {QStringLiteral("first.mp4"), QStringLiteral("second.mp4")});
    QCOMPARE(fromRelative, sessionPath);
  }
  const QString swapped = MatchSessionFile::sessionFilePath(
      storageDirectory.path(), {secondVideo, firstVideo});
  QVERIFY(swapped != sessionPath);
  QVERIFY(MatchSessionFile::sessionFilePath(QString(), sourcePaths).isEmpty());

  TagSession source;
  source.setGameTeams(QStringLiteral("Las Leonas"), QStringLiteral("Las Diablas"),
                      QStringLiteral("#112233"), QStringLiteral("#abcdef"));
  source.setGameMetadata(QDate(2026, 10, 23), QStringLiteral("LEO"), QStringLiteral("DIA"));
  source.setMatchNote(QStringLiteral("<p>Notas <b>del</b> partido</p>"));
  source.restoreTags({shotTag(), quarterTag()}, 11);
  QVERIFY(source.setEventLabel(EventCatalog::EventId::Shot, QStringLiteral("Tiro libre")).accepted);
  QVERIFY(source.setEventShortcutKey(EventCatalog::EventId::Shot, Qt::Key_K).accepted);
  QVERIFY(source.setEventHidden(EventCatalog::EventId::OnTarget, true).accepted);
  QVERIFY(source.setEventLabel(EventCatalog::EventId::OnTarget, QStringLiteral("Off target")).accepted);
  QVERIFY(source.setEventShortcutKey(EventCatalog::EventId::OnTarget, Qt::Key_1).accepted);

  EventDefaults::setUserOverride(QStringLiteral("Shot"), 1500, 2500);

  PresentationQueue sourceQueue;
  sourceQueue.setTagSession(&source);
  sourceQueue.restoreSelection({4, 2}, 4);

  Telestration::Session sourceDrawings;
  sourceDrawings.replaceDrawings({taggedArrow(), freehandMark(), circleMark(), dottedArrow()}, 20);

  const MatchSessionFile::Document captured =
      MatchSessionFile::capture(source, &sourceQueue, &sourceDrawings, sourcePaths);
  QCOMPARE(captured.clipLeadLagSpecified, true);
  const MatchSessionFile::SaveResult saved = MatchSessionFile::saveFile(sessionPath, captured);
  QCOMPARE(saved.status, MatchSessionFile::SaveStatus::Saved);

  const QByteArray sourceXml = xmlDocumentBytes(source);
  const QByteArray sourceSidecar = clipsJsonBytes(source, clipsFromQueue(sourceQueue));
  QVERIFY(!sourceXml.isEmpty());
  QVERIFY(!sourceSidecar.isEmpty());
  QVERIFY(sourceXml.contains("SHOT"));
  QVERIFY(!sourceXml.contains("Tiro libre"));

  const QByteArray renamedShotSidecar = clipsJsonBytes(source, {clipForTag(sourceQueue, 4)});
  TagSession plain;
  plain.setGameTeams(source.homeTeamName(), source.awayTeamName(), source.homeTeamColor(),
                     source.awayTeamColor());
  plain.setGameMetadata(source.gameDate(), source.homeAbbrev(), source.awayAbbrev());
  plain.setMatchNote(source.matchNote());
  plain.restoreTags(source.tags(), source.nextTagId());
  PresentationQueue plainQueue;
  plainQueue.setTagSession(&plain);
  plainQueue.restoreSelection(sourceQueue.selectedTagIds(), sourceQueue.currentClipTagId());
  QCOMPARE(xmlDocumentBytes(plain), sourceXml);
  const QByteArray plainShotSidecar = clipsJsonBytes(plain, {clipForTag(plainQueue, 4)});
  const QJsonObject renamedShot = QJsonDocument::fromJson(renamedShotSidecar).object();
  const QJsonObject plainShot = QJsonDocument::fromJson(plainShotSidecar).object();
  QCOMPARE(renamedShot.value(QStringLiteral("code")).toString(),
           plainShot.value(QStringLiteral("code")).toString());
  QVERIFY(renamedShot.value(QStringLiteral("code")).toString().contains(QStringLiteral("SHOT")));
  QVERIFY(!renamedShot.value(QStringLiteral("code")).toString().contains(QStringLiteral("Tiro libre")));
  QVERIFY(renamedShot.value(QStringLiteral("overlayLabel")).toString().contains(QStringLiteral("Tiro libre")));
  QVERIFY(plainShot.value(QStringLiteral("overlayLabel")).toString().contains(QStringLiteral("Shot")));
  QVERIFY(!plainShot.value(QStringLiteral("overlayLabel")).toString().contains(QStringLiteral("Tiro libre")));

  EventDefaults::setUserOverride(QStringLiteral("Goal"), 1000, 2000);

  const MatchSessionFile::LoadResult loaded = MatchSessionFile::loadFile(sessionPath);
  QCOMPARE(loaded.status, MatchSessionFile::LoadStatus::Loaded);
  QCOMPARE(loaded.document.tags.size(), 2);
  QCOMPARE(loaded.document.tags.at(0).id, static_cast<quint64>(4));
  QCOMPARE(loaded.document.tags.at(1).id, static_cast<quint64>(2));

  TagSession restored;
  PresentationQueue restoredQueue;
  restoredQueue.setTagSession(&restored);
  Telestration::Session restoredDrawings;
  MatchSessionFile::apply(loaded.document, restored, &restoredQueue, &restoredDrawings, false);

  QCOMPARE(restored.homeTeamName(), QStringLiteral("Las Leonas"));
  QCOMPARE(restored.awayTeamName(), QStringLiteral("Las Diablas"));
  QCOMPARE(restored.homeTeamColor(), QStringLiteral("#112233"));
  QCOMPARE(restored.awayTeamColor(), QStringLiteral("#abcdef"));
  QCOMPARE(restored.gameDate(), QDate(2026, 10, 23));
  QCOMPARE(restored.homeAbbrev(), QStringLiteral("LEO"));
  QCOMPARE(restored.awayAbbrev(), QStringLiteral("DIA"));
  QCOMPARE(restored.matchNote(), source.matchNote());
  QCOMPARE(restored.nextTagId(), static_cast<quint64>(11));
  QCOMPARE(restored.tags().size(), 2);
  expectSameTag(restored.tags().at(0), source.tags().at(0));
  expectSameTag(restored.tags().at(1), source.tags().at(1));
  QCOMPARE(restored.eventLabelOverrides(), source.eventLabelOverrides());
  QCOMPARE(restored.eventLabelOverrides().label(EventCatalog::EventId::Shot),
           QStringLiteral("Tiro libre"));
  QCOMPARE(restored.eventLabelOverrides().shortcutKey(EventCatalog::EventId::Shot), Qt::Key_K);
  QVERIFY(restored.eventLabelOverrides().hidden(EventCatalog::EventId::OnTarget));
  QCOMPARE(restored.eventLabelOverrides().label(EventCatalog::EventId::OnTarget),
           QStringLiteral("Off target"));
  QCOMPARE(restored.eventLabelOverrides().shortcutKey(EventCatalog::EventId::OnTarget), Qt::Key_1);
  QCOMPARE(restored.quarterPhase(), TagSession::QuarterPhase::QuarterInProgress);
  QCOMPARE(restored.currentQuarterIndex(), 1);
  QCOMPARE(restored.currentQuarterStartMs(), static_cast<qint64>(9000));

  QCOMPARE(restoredQueue.selectedTagIds(), QVector<quint64>({4, 2}));
  QCOMPARE(restoredQueue.currentClipTagId(), static_cast<quint64>(4));
  QCOMPARE(restoredDrawings.nextDrawingId(), static_cast<quint64>(20));
  QCOMPARE(restoredDrawings.drawings().size(), 4);
  expectSameDrawing(restoredDrawings.drawings().at(0), taggedArrow());
  expectSameDrawing(restoredDrawings.drawings().at(1), freehandMark());
  expectSameDrawing(restoredDrawings.drawings().at(2), circleMark());
  expectSameDrawing(restoredDrawings.drawings().at(3), dottedArrow());

  const EventDefaults::EventDuration shot = EventDefaults::defaultFor(QStringLiteral("Shot"));
  QCOMPARE(shot.leadMs, static_cast<qint64>(1500));
  QCOMPARE(shot.lagMs, static_cast<qint64>(2500));
  const EventDefaults::EventDuration goal = EventDefaults::defaultFor(QStringLiteral("Goal"));
  QCOMPARE(goal.leadMs, EventDefaults::factoryDefaultFor(QStringLiteral("Goal")).leadMs);
  QCOMPARE(goal.lagMs, EventDefaults::factoryDefaultFor(QStringLiteral("Goal")).lagMs);

  QCOMPARE(xmlDocumentBytes(restored), sourceXml);
  QCOMPARE(clipsJsonBytes(restored, clipsFromQueue(restoredQueue)), sourceSidecar);

  TagSession::GameTag added;
  added.mainEventId = EventCatalog::EventId::Pass;
  added.markMs = 1000;
  added.startMs = 0;
  added.endMs = 2000;
  restored.addTag(added);
  QCOMPARE(restored.tags().constLast().id, static_cast<quint64>(11));

  TagSession keptMetadata;
  keptMetadata.setGameTeams(QStringLiteral("Home Club"), QStringLiteral("Away Club"),
                            QStringLiteral("#000000"), QStringLiteral("#ffffff"));
  keptMetadata.setGameMetadata(QDate(2024, 1, 2), QStringLiteral("HC"), QStringLiteral("AC"));
  PresentationQueue keptQueue;
  keptQueue.setTagSession(&keptMetadata);
  MatchSessionFile::apply(loaded.document, keptMetadata, &keptQueue, nullptr, true);
  QCOMPARE(keptMetadata.homeTeamName(), QStringLiteral("Home Club"));
  QCOMPARE(keptMetadata.awayTeamName(), QStringLiteral("Away Club"));
  QCOMPARE(keptMetadata.homeAbbrev(), QStringLiteral("HC"));
  QCOMPARE(keptMetadata.gameDate(), QDate(2024, 1, 2));
  QCOMPARE(keptMetadata.tags().size(), 2);
  QCOMPARE(keptMetadata.eventLabelOverrides().label(EventCatalog::EventId::Shot),
           QStringLiteral("Tiro libre"));
}

void MatchSessionFileTest::missingCorruptAndNewerFilesStaySafe() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString missingPath = directory.filePath(QStringLiteral("missing.json"));
  const MatchSessionFile::LoadResult missing = MatchSessionFile::loadFile(missingPath);
  QCOMPARE(missing.status, MatchSessionFile::LoadStatus::Missing);

  const QString emptyPath = directory.filePath(QStringLiteral("empty.json"));
  QVERIFY(writeBytes(emptyPath, QByteArray()));
  QCOMPARE(MatchSessionFile::loadFile(emptyPath).status, MatchSessionFile::LoadStatus::Corrupt);

  const QString truncatedPath = directory.filePath(QStringLiteral("truncated.json"));
  const QByteArray truncatedBytes = QByteArrayLiteral("{");
  QVERIFY(writeBytes(truncatedPath, truncatedBytes));
  QCOMPARE(MatchSessionFile::loadFile(truncatedPath).status, MatchSessionFile::LoadStatus::Corrupt);

  const QString wrongFormatPath = directory.filePath(QStringLiteral("wrong-format.json"));
  const QByteArray wrongFormatBytes =
      QByteArrayLiteral("{\"format\":\"other\",\"version\":1,\"tags\":[]}\n");
  QVERIFY(writeBytes(wrongFormatPath, wrongFormatBytes));
  QCOMPARE(MatchSessionFile::loadFile(wrongFormatPath).status, MatchSessionFile::LoadStatus::Corrupt);

  const QString missingTagsPath = directory.filePath(QStringLiteral("missing-tags.json"));
  const QByteArray missingTagsBytes =
      QByteArrayLiteral("{\"format\":\"ava-match-session\",\"version\":1}\n");
  QVERIFY(writeBytes(missingTagsPath, missingTagsBytes));
  QCOMPARE(MatchSessionFile::loadFile(missingTagsPath).status, MatchSessionFile::LoadStatus::Corrupt);

  const QString duplicateTagsPath = directory.filePath(QStringLiteral("duplicate-tags.json"));
  const QByteArray duplicateTagsBytes = QByteArrayLiteral(
      "{\"format\":\"ava-match-session\",\"version\":1,\"tags\":["
      "{\"id\":1,\"mainEventId\":2,\"followUpIds\":[],\"markMs\":10,\"startMs\":0,\"endMs\":20},"
      "{\"id\":1,\"mainEventId\":2,\"followUpIds\":[],\"markMs\":30,\"startMs\":20,\"endMs\":40}"
      "]}\n");
  QVERIFY(writeBytes(duplicateTagsPath, duplicateTagsBytes));
  QCOMPARE(MatchSessionFile::loadFile(duplicateTagsPath).status, MatchSessionFile::LoadStatus::Corrupt);

  const QString newerPath = directory.filePath(QStringLiteral("newer.json"));
  const QByteArray newerBytes =
      QByteArrayLiteral("{\"format\":\"ava-match-session\",\"version\":2,\"tags\":[],\"future\":true}\n");
  QVERIFY(writeBytes(newerPath, newerBytes));
  QCOMPARE(MatchSessionFile::loadFile(newerPath).status, MatchSessionFile::LoadStatus::NewerVersion);

  TagSession session;
  const MatchSessionFile::Document replacement = MatchSessionFile::capture(session, nullptr, nullptr, {});
  QCOMPARE(MatchSessionFile::saveFile(truncatedPath, replacement).status,
           MatchSessionFile::SaveStatus::PreservedCorrupt);
  QCOMPARE(readBytes(truncatedPath), truncatedBytes);
  QCOMPARE(MatchSessionFile::saveFile(newerPath, replacement).status,
           MatchSessionFile::SaveStatus::PreservedNewerVersion);
  QCOMPARE(readBytes(newerPath), newerBytes);
  QCOMPARE(MatchSessionFile::saveFile(wrongFormatPath, replacement,
                                      MatchSessionFile::ExistingFileGuard::Replace)
               .status,
           MatchSessionFile::SaveStatus::Saved);
  QCOMPARE(MatchSessionFile::loadFile(wrongFormatPath).status, MatchSessionFile::LoadStatus::Loaded);

  const QString minimalPath = directory.filePath(QStringLiteral("minimal.json"));
  QVERIFY(writeBytes(minimalPath, QByteArray(kMinimalSession)));
  const MatchSessionFile::LoadResult minimal = MatchSessionFile::loadFile(minimalPath);
  QCOMPARE(minimal.status, MatchSessionFile::LoadStatus::Loaded);
  QVERIFY(minimal.document.tags.isEmpty());
  QCOMPARE(minimal.document.clipLeadLagSpecified, false);
  QCOMPARE(minimal.document.presentation.specified, false);

  EventDefaults::setUserOverride(QStringLiteral("Shot"), 1500, 2500);
  TagSession untouched;
  MatchSessionFile::apply(minimal.document, untouched, nullptr, nullptr, false);
  QCOMPARE(EventDefaults::defaultFor(QStringLiteral("Shot")).leadMs, static_cast<qint64>(1500));
  QCOMPARE(untouched.eventLabelOverrides().records().size(), 0);

  const QString clearedDurationsPath = directory.filePath(QStringLiteral("cleared-durations.json"));
  const QByteArray clearedDurationsBytes = QByteArrayLiteral(
      "{\"format\":\"ava-match-session\",\"version\":1,\"tags\":[],\"clipLeadLagOverrides\":[]}\n");
  QVERIFY(writeBytes(clearedDurationsPath, clearedDurationsBytes));
  const MatchSessionFile::LoadResult cleared = MatchSessionFile::loadFile(clearedDurationsPath);
  QCOMPARE(cleared.status, MatchSessionFile::LoadStatus::Loaded);
  QCOMPARE(cleared.document.clipLeadLagSpecified, true);
  QVERIFY(cleared.document.clipLeadLagOverrides.isEmpty());
  TagSession durationSession;
  MatchSessionFile::apply(cleared.document, durationSession, nullptr, nullptr, false);
  QCOMPARE(EventDefaults::defaultFor(QStringLiteral("Shot")).leadMs,
           EventDefaults::factoryDefaultFor(QStringLiteral("Shot")).leadMs);

  const MatchSessionFile::LoadResult noSources = MatchSessionFile::loadForSources({});
  QCOMPARE(noSources.status, MatchSessionFile::LoadStatus::Missing);
}

void MatchSessionFileTest::templatesSaveApplyAndProtectTheBuiltinLayout() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString root = directory.path();
  const QString templatePath = QDir(root).filePath(QStringLiteral("control-templates.json"));

  const EventControlsTemplateStore::Catalog absent = EventControlsTemplateStore::listTemplates(root);
  QCOMPARE(absent.fileStatus, EventControlsTemplateStore::FileStatus::Absent);
  QCOMPARE(absent.templates.size(), 1);
  QCOMPARE(absent.templates.at(0).name, EventControlsSetup::builtinDefaultName());
  QVERIFY(absent.templates.at(0).builtin);
  QCOMPARE(EventControlsTemplateStore::loadTemplate(EventControlsSetup::builtinDefaultName(), root).status,
           EventControlsTemplateStore::LoadStatus::Loaded);
  QVERIFY(!QFile::exists(templatePath));

  TagSession source;
  QVERIFY(source.setEventLabel(EventCatalog::EventId::Shot, QStringLiteral("Tiro libre")).accepted);
  QVERIFY(source.setEventShortcutKey(EventCatalog::EventId::Shot, Qt::Key_K).accepted);
  QVERIFY(source.setEventHidden(EventCatalog::EventId::OnTarget, true).accepted);
  QVERIFY(source.setEventLabel(EventCatalog::EventId::OnTarget, QStringLiteral("Off target")).accepted);
  const EventControlsSetup::Setup setup = EventControlsSetup::fromSession(source);

  const EventControlsTemplateStore::SaveResult saved =
      EventControlsTemplateStore::saveTemplate(QStringLiteral("  Jugadoras  "), setup, root);
  QCOMPARE(saved.status, EventControlsTemplateStore::SaveStatus::Saved);
  const QByteArray savedBytes = readBytes(templatePath);
  QVERIFY(!savedBytes.isEmpty());

  const EventControlsTemplateStore::SaveResult builtinSave =
      EventControlsTemplateStore::saveTemplate(QStringLiteral(" camila's LAYOUT "), setup, root);
  QCOMPARE(builtinSave.status, EventControlsTemplateStore::SaveStatus::BuiltinName);
  QCOMPARE(readBytes(templatePath), savedBytes);
  QCOMPARE(EventControlsTemplateStore::removeTemplate(QStringLiteral("Camila's layout"), root).status,
           EventControlsTemplateStore::RemoveStatus::BuiltinName);
  QCOMPARE(readBytes(templatePath), savedBytes);

  const EventControlsTemplateStore::Catalog listed = EventControlsTemplateStore::listTemplates(root);
  QCOMPARE(listed.fileStatus, EventControlsTemplateStore::FileStatus::Ready);
  QCOMPARE(listed.templates.size(), 2);
  QVERIFY(listed.templates.at(0).builtin);
  QCOMPARE(listed.templates.at(1).name, QStringLiteral("Jugadoras"));
  QVERIFY(!listed.templates.at(1).builtin);

  const EventControlsTemplateStore::LoadResult loaded =
      EventControlsTemplateStore::loadTemplate(QStringLiteral("Jugadoras"), root);
  QCOMPARE(loaded.status, EventControlsTemplateStore::LoadStatus::Loaded);
  QCOMPARE(loaded.setup.labelOverrides, setup.labelOverrides);
  QVERIFY(loaded.setup.labelOverrides.hidden(EventCatalog::EventId::OnTarget));
  QCOMPARE(loaded.setup.labelOverrides.shortcutKey(EventCatalog::EventId::Shot), Qt::Key_K);

  TagSession session;
  TagSession::GameTag tag;
  tag.mainEventId = EventCatalog::EventId::Pass;
  tag.markMs = 1000;
  tag.startMs = 500;
  tag.endMs = 2000;
  tag.team = QStringLiteral("Home");
  session.addTag(tag);
  QVERIFY(session.setEventLabel(EventCatalog::EventId::Goal, QStringLiteral("Tanto")).accepted);
  QCOMPARE(EventControlsSetup::applyToSession(session, loaded.setup),
           EventControlsSetup::ApplyStatus::Applied);
  QCOMPARE(session.tags().size(), 1);
  QCOMPARE(session.tags().at(0).mainEventId, EventCatalog::EventId::Pass);
  QCOMPARE(session.eventLabelOverrides(), setup.labelOverrides);
  QVERIFY(!session.eventLabelOverrides().hasLabel(EventCatalog::EventId::Goal));

  QCOMPARE(EventControlsSetup::applyToSession(session, EventControlsSetup::builtinDefault()),
           EventControlsSetup::ApplyStatus::Applied);
  QCOMPARE(session.eventLabelOverrides().records().size(), 0);
  QCOMPARE(session.tags().size(), 1);
  QCOMPARE(session.tags().at(0).id, static_cast<quint64>(1));

  QCOMPARE(EventControlsTemplateStore::saveTemplate(QString(), setup, root).status,
           EventControlsTemplateStore::SaveStatus::InvalidName);
  QCOMPARE(EventControlsTemplateStore::saveTemplate(QStringLiteral("home/away"), setup, root).status,
           EventControlsTemplateStore::SaveStatus::InvalidName);
  QCOMPARE(readBytes(templatePath), savedBytes);

  const QByteArray corruptBytes =
      QByteArrayLiteral("{\"format\":\"ava-control-templates\",\"version\":1,\"templates\":\"nope\"}\n");
  QVERIFY(writeBytes(templatePath, corruptBytes));
  const EventControlsTemplateStore::Catalog corrupt = EventControlsTemplateStore::listTemplates(root);
  QCOMPARE(corrupt.fileStatus, EventControlsTemplateStore::FileStatus::Corrupt);
  QCOMPARE(corrupt.templates.size(), 1);
  QVERIFY(corrupt.templates.at(0).builtin);
  QCOMPARE(EventControlsTemplateStore::saveTemplate(QStringLiteral("Jugadoras"), setup, root).status,
           EventControlsTemplateStore::SaveStatus::Corrupt);
  QCOMPARE(readBytes(templatePath), corruptBytes);
  QCOMPARE(EventControlsTemplateStore::loadTemplate(EventControlsSetup::builtinDefaultName(), root).status,
           EventControlsTemplateStore::LoadStatus::Loaded);

  const QByteArray newerBytes =
      QByteArrayLiteral("{\"format\":\"ava-control-templates\",\"version\":2,\"templates\":[]}\n");
  QVERIFY(writeBytes(templatePath, newerBytes));
  QCOMPARE(EventControlsTemplateStore::listTemplates(root).fileStatus,
           EventControlsTemplateStore::FileStatus::NewerVersion);
  QCOMPARE(EventControlsTemplateStore::saveTemplate(QStringLiteral("Jugadoras"), setup, root).status,
           EventControlsTemplateStore::SaveStatus::NewerVersion);
  QCOMPARE(EventControlsTemplateStore::removeTemplate(QStringLiteral("Jugadoras"), root).status,
           EventControlsTemplateStore::RemoveStatus::NewerVersion);
  QCOMPARE(readBytes(templatePath), newerBytes);
}

void MatchSessionFileTest::qsettingsNameKeysMigrateToCatalogIds() {
  EventDefaults::clearUserOverrides();
  const QString shootoutGroup = legacySettingsGroup(QStringLiteral("S.O."));
  QCOMPARE(shootoutGroup, QStringLiteral("S.O."));
  const QString foulGroup = legacySettingsGroup(QStringLiteral("PC Foul"));
  QCOMPARE(foulGroup, QStringLiteral("PC%20Foul"));
  const QString circleEntryGroup = legacySettingsGroup(QStringLiteral("Circle Entry"));
  QCOMPARE(circleEntryGroup, QStringLiteral("Circle%20Entry"));

  {
    QSettings settings;
    settings.beginGroup(QStringLiteral("clipDurations"));
    writeDurationGroup(settings, QStringLiteral("Shot"), 1500, 2500);
    writeDurationGroup(settings, shootoutGroup, 2100, 9100);
    writeDurationGroup(settings, QStringLiteral("6"), 3333, 4444);
    writeDurationGroup(settings, QStringLiteral("Pass"), 1111, 2222);
    writeDurationGroup(settings, foulGroup, 1200, 3400);
    writeDurationGroup(settings, QStringLiteral("Circle Entry"), 1700, 1800);
    settings.endGroup();
    settings.sync();
  }

  EventDefaults::loadFromSettings();

  const EventDefaults::EventDuration shot = EventDefaults::defaultFor(QStringLiteral("Shot"));
  QCOMPARE(shot.leadMs, static_cast<qint64>(1500));
  QCOMPARE(shot.lagMs, static_cast<qint64>(2500));
  const EventDefaults::EventDuration shootout = EventDefaults::defaultFor(QStringLiteral("S.O."));
  QCOMPARE(shootout.leadMs, static_cast<qint64>(2100));
  QCOMPARE(shootout.lagMs, static_cast<qint64>(9100));
  const EventDefaults::EventDuration pass = EventDefaults::defaultFor(QStringLiteral("Pass"));
  QCOMPARE(pass.leadMs, static_cast<qint64>(3333));
  QCOMPARE(pass.lagMs, static_cast<qint64>(4444));
  const EventDefaults::EventDuration foul = EventDefaults::defaultFor(QStringLiteral("PC Foul"));
  QCOMPARE(foul.leadMs, static_cast<qint64>(1200));
  QCOMPARE(foul.lagMs, static_cast<qint64>(3400));
  const EventDefaults::EventDuration circleEntry =
      EventDefaults::defaultFor(QStringLiteral("Circle Entry"));
  QCOMPARE(circleEntry.leadMs, static_cast<qint64>(1700));
  QCOMPARE(circleEntry.lagMs, static_cast<qint64>(1800));

  {
    QSettings settings;
    settings.beginGroup(QStringLiteral("clipDurations"));
    const QStringList groups = settings.childGroups();
    QVERIFY2(groups.contains(QStringLiteral("2")), qPrintable(groups.join(QLatin1Char(','))));
    QVERIFY2(groups.contains(QStringLiteral("14")), qPrintable(groups.join(QLatin1Char(','))));
    QVERIFY2(groups.contains(QStringLiteral("6")), qPrintable(groups.join(QLatin1Char(','))));
    QVERIFY2(groups.contains(QStringLiteral("4")), qPrintable(groups.join(QLatin1Char(','))));
    QVERIFY2(groups.contains(QStringLiteral("7")), qPrintable(groups.join(QLatin1Char(','))));
    QVERIFY(!groups.contains(QStringLiteral("Shot")));
    QVERIFY(!groups.contains(QStringLiteral("Pass")));
    QVERIFY(!groups.contains(QStringLiteral("PC Foul")));
    QVERIFY(!groups.contains(shootoutGroup));
    QVERIFY(!groups.contains(foulGroup));
    QVERIFY(!groups.contains(QStringLiteral("Circle Entry")));
    QVERIFY(!groups.contains(circleEntryGroup));
    settings.endGroup();
  }

  EventDefaults::loadFromSettings();
  QCOMPARE(EventDefaults::defaultFor(QStringLiteral("Shot")).leadMs, static_cast<qint64>(1500));
  QCOMPARE(EventDefaults::defaultFor(QStringLiteral("Pass")).leadMs, static_cast<qint64>(3333));
  QCOMPARE(EventDefaults::defaultFor(QStringLiteral("S.O.")).lagMs, static_cast<qint64>(9100));
  QCOMPARE(EventDefaults::defaultFor(QStringLiteral("PC Foul")).leadMs, static_cast<qint64>(1200));
  QCOMPARE(EventDefaults::defaultFor(QStringLiteral("Circle Entry")).leadMs, static_cast<qint64>(1700));

  EventDefaults::setUserOverride(QStringLiteral("Goal"), 1000, 2000);
  QCOMPARE(EventDefaults::defaultFor(QStringLiteral("Goal")).leadMs, static_cast<qint64>(1000));
  QCOMPARE(EventDefaults::defaultFor(QStringLiteral("Goal")).lagMs, static_cast<qint64>(2000));
  {
    QSettings settings;
    settings.beginGroup(QStringLiteral("clipDurations"));
    settings.beginGroup(QStringLiteral("1"));
    QCOMPARE(settings.value(QStringLiteral("preMs")).toLongLong(), static_cast<qlonglong>(1000));
    QCOMPARE(settings.value(QStringLiteral("postMs")).toLongLong(), static_cast<qlonglong>(2000));
    settings.endGroup();
    QVERIFY(!settings.childGroups().contains(QStringLiteral("Goal")));
    settings.endGroup();
  }
}

int runMatchSessionFileTests(int argc, char** argv) {
  MatchSessionFileTest tests;
  return QTest::qExec(&tests, argc, argv);
}

#include "MatchSessionFileTests.moc"
