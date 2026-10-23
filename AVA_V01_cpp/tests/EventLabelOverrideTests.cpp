#include "AppLocale.h"
#include "ClipDurationSettingsDialog.h"
#include "CompilationSidecar.h"
#include "EventCatalog.h"
#include "EventLabelOverrides.h"
#include "EventLabelResolver.h"
#include "ExportClipBuilder.h"
#include "GameControls.h"
#include "LocaleNotifier.h"
#include "MatchNotesEditor.h"
#include "PresentationPlaybackController.h"
#include "PresentationQueue.h"
#include "SessionNotesWriter.h"
#include "StatsWindow.h"
#include "TagEvent.h"
#include "TagSession.h"
#include "TagToastOverlay.h"
#include "WorkWindow.h"
#include "XmlEventMappingDialog.h"
#include "XmlExporter.h"
#include "XmlImporter.h"

#include <optional>

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QEvent>
#include <QFile>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableView>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeWidget>

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

const QString kPlayerLabel = QStringLiteral("Jugadora 7");
const QString kFollowUpLabel = QStringLiteral("Arco 7");
const QString kStar = QString(QChar(0x2606));

#define EXPECT_ACCEPTED(resultExpression)                                         \
  do {                                                                            \
    const EventLabelOverrides::Result acceptedResult = (resultExpression);       \
    QVERIFY(acceptedResult.accepted);                                             \
    QCOMPARE(acceptedResult.reason, EventLabelOverrides::RejectReason::None);    \
    QCOMPARE(acceptedResult.conflictingEventId, EventCatalog::EventId::None);    \
  } while (false)

#define EXPECT_REJECTED(resultExpression, expectedReason, expectedConflict)            \
  do {                                                                                 \
    const EventLabelOverrides::Result rejectedResult = (resultExpression);            \
    QVERIFY(!rejectedResult.accepted);                                                 \
    QCOMPARE(rejectedResult.reason, expectedReason);                                  \
    QCOMPARE(rejectedResult.conflictingEventId, expectedConflict);                    \
  } while (false)

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

void configureMatch(TagSession& session) {
  session.setGameTeams(QStringLiteral("UC"), QStringLiteral("OR"), QStringLiteral("#3c5ac8"),
                       QStringLiteral("#c83c3c"));
  session.setGameMetadata(QDate(2026, 9, 27), QStringLiteral("UC"), QStringLiteral("OR"));
}

QByteArray xmlBytes(TagSession& session) {
  QTemporaryDir directory;
  if (!directory.isValid()) return {};
  const QString path = directory.filePath(QStringLiteral("match.xml"));
  QString error;
  if (!XmlExporter::writeAllInstances(&session, path, &error)) return {};
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return {};
  return file.readAll();
}

QPushButton* buttonNamed(QWidget* root, const char* eventName) {
  if (!root) return nullptr;
  for (QPushButton* button : root->findChildren<QPushButton*>()) {
    if (button->property("gameEventName").toString() == QLatin1String(eventName)) return button;
  }
  return nullptr;
}

QPushButton* followUpButton(QWidget* root, const char* canonicalKey) {
  if (!root) return nullptr;
  for (QPushButton* button : root->findChildren<QPushButton*>()) {
    if (button->property("gameEventKey").toString() == QLatin1String(canonicalKey)) return button;
  }
  return nullptr;
}

QString titleText(QPushButton* button) {
  if (!button) return {};
  for (QLabel* label : button->findChildren<QLabel*>()) {
    if (label->property("gameEventName").toString().isEmpty()) continue;
    if (!label->toolTip().isEmpty()) return label->toolTip();
    return label->text();
  }
  return {};
}

QString shortcutText(QPushButton* button) {
  if (!button) return {};
  for (QLabel* label : button->findChildren<QLabel*>()) {
    if (!label->property("gameEventName").toString().isEmpty()) continue;
    return label->text();
  }
  return {};
}

QTreeWidgetItem* topLevelItemWithId(QTreeWidget* tree, EventCatalog::EventId id) {
  if (!tree) return nullptr;
  for (int index = 0; index < tree->topLevelItemCount(); ++index) {
    QTreeWidgetItem* item = tree->topLevelItem(index);
    const TagEvent::MainEventKey key = item->data(0, Qt::UserRole).value<TagEvent::MainEventKey>();
    if (key.id == id) return item;
  }
  return nullptr;
}

QLabel* labelWithText(QWidget* root, const QString& text) {
  if (!root) return nullptr;
  for (QLabel* label : root->findChildren<QLabel*>()) {
    if (label->text() == text) return label;
  }
  return nullptr;
}

PresentationQueue::Clip clipFromTag(const TagSession::GameTag& tag, int index) {
  PresentationQueue::Clip clip;
  clip.tagSessionIndex = index;
  clip.tagId = tag.id;
  clip.markMs = tag.markMs;
  clip.startMs = tag.startMs;
  clip.endMs = tag.endMs;
  clip.mainEvent = tag.mainEventKey();
  clip.followUps = tag.followUpChain();
  clip.team = tag.team;
  clip.note = tag.note;
  return clip;
}

QString sidecarJson(const ClipCompilationRecord& record) {
  CompilationSidecar::SidecarBuildInput input;
  input.avaVersion = QStringLiteral("test");
  input.videoFileName = QStringLiteral("match.mp4");
  input.sourceVideoFileName = QStringLiteral("source.mp4");
  input.measuredDurationSeconds = {1.0};
  input.clipsInConcatOrder = {record};
  return QString::fromUtf8(CompilationSidecar::renderJson(CompilationSidecar::buildDocument(input)));
}

}  // namespace

class EventLabelOverrideTest : public QObject {
  Q_OBJECT

 private slots:
  void init();
  void cleanup();
  void validationRejectsEmptyAndDuplicateLabels();
  void hiddenSiblingIsExcludedUntilShown();
  void shortcutAndHiddenChangeTheTaggingGrid();
  void renameKeepsStatsGroupedById();
  void exportRoundTripKeepsStableCodesAndWritesTheDisplayLabel();
  void renamePropagatesThroughEveryDisplayPath();

 private:
  AppLocale::Language previousLanguage_ = AppLocale::Language::English;
};

void EventLabelOverrideTest::init() {
  previousLanguage_ = AppLocale::currentLanguage();
  AppLocale::setLanguage(AppLocale::Language::English);
}

void EventLabelOverrideTest::cleanup() { AppLocale::setLanguage(previousLanguage_); }

void EventLabelOverrideTest::validationRejectsEmptyAndDuplicateLabels() {
  QCOMPARE(EventLabelResolver::labelForId(EventCatalog::EventId::Shot, nullptr,
                                          AppLocale::Language::English),
           TagEvent::displayLabel(EventCatalog::EventId::Shot, AppLocale::Language::English));
  QCOMPARE(EventLabelResolver::labelForId(EventCatalog::EventId::Shot, nullptr,
                                          AppLocale::Language::Spanish),
           QStringLiteral("Tiro"));
  QCOMPARE(EventLabelResolver::labelForId(EventCatalog::EventId::Special, nullptr,
                                          AppLocale::Language::English),
           kStar);
  QCOMPARE(EventLabelResolver::labelForId(EventCatalog::EventId::Special, nullptr,
                                          AppLocale::Language::Spanish),
           kStar);
  QCOMPARE(EventLabelResolver::labelForId(EventCatalog::EventId::FollowUpGoal, nullptr,
                                          AppLocale::Language::Spanish),
           QStringLiteral("Gol"));

  TagSession session;
  QSignalSpy overridesChanged(&session, &TagSession::eventLabelOverridesChanged);

  EXPECT_REJECTED(session.setEventLabel(EventCatalog::EventId::None, kPlayerLabel),
                 EventLabelOverrides::RejectReason::UnknownEvent, EventCatalog::EventId::None);
  EXPECT_REJECTED(session.setEventLabel(static_cast<EventCatalog::EventId>(999), kPlayerLabel),
                 EventLabelOverrides::RejectReason::UnknownEvent, EventCatalog::EventId::None);
  EXPECT_REJECTED(session.setEventLabel(EventCatalog::EventId::Shot, QString()),
                 EventLabelOverrides::RejectReason::EmptyLabel, EventCatalog::EventId::None);
  EXPECT_REJECTED(session.setEventLabel(EventCatalog::EventId::Shot, QStringLiteral("   ")),
                 EventLabelOverrides::RejectReason::EmptyLabel, EventCatalog::EventId::None);
  QCOMPARE(overridesChanged.count(), 0);
  QVERIFY(!session.eventLabelOverrides().hasLabel(EventCatalog::EventId::Shot));

  EXPECT_ACCEPTED(session.setEventLabel(EventCatalog::EventId::Shot, QStringLiteral("  Jugadora 7  ")));
  QCOMPARE(overridesChanged.count(), 1);
  QCOMPARE(session.eventLabelOverrides().label(EventCatalog::EventId::Shot), kPlayerLabel);
  QCOMPARE(EventLabelResolver::labelForId(EventCatalog::EventId::Shot, &session,
                                          AppLocale::Language::English),
           kPlayerLabel);
  QCOMPARE(EventLabelResolver::labelForId(EventCatalog::EventId::Shot, &session,
                                          AppLocale::Language::Spanish),
           kPlayerLabel);

  EXPECT_ACCEPTED(session.setEventLabel(EventCatalog::EventId::Shot, kPlayerLabel));
  QCOMPARE(overridesChanged.count(), 1);

  EXPECT_REJECTED(session.setEventLabel(EventCatalog::EventId::Shot, QString()),
                 EventLabelOverrides::RejectReason::EmptyLabel, EventCatalog::EventId::None);
  QCOMPARE(session.eventLabelOverrides().label(EventCatalog::EventId::Shot), kPlayerLabel);

  const QString spacedLabel = QStringLiteral("Jugadora  7");
  EXPECT_ACCEPTED(session.setEventLabel(EventCatalog::EventId::Shot, QStringLiteral("  Jugadora  7")));
  QCOMPARE(overridesChanged.count(), 2);
  QCOMPARE(session.eventLabelOverrides().label(EventCatalog::EventId::Shot), spacedLabel);

  EXPECT_REJECTED(session.setEventLabel(EventCatalog::EventId::Shot, QStringLiteral("Goal")),
                 EventLabelOverrides::RejectReason::DuplicateLabel, EventCatalog::EventId::Goal);
  EXPECT_REJECTED(session.setEventLabel(EventCatalog::EventId::Shot, QStringLiteral("goal")),
                 EventLabelOverrides::RejectReason::DuplicateLabel, EventCatalog::EventId::Goal);
  EXPECT_REJECTED(session.setEventLabel(EventCatalog::EventId::Shot, QStringLiteral("Gol")),
                 EventLabelOverrides::RejectReason::DuplicateLabel, EventCatalog::EventId::Goal);
  EXPECT_REJECTED(session.setEventLabel(EventCatalog::EventId::Shot, kStar),
                 EventLabelOverrides::RejectReason::DuplicateLabel, EventCatalog::EventId::Special);
  // Shot no longer shows "Tiro", so Goal may use that word until the override is cleared.
  EXPECT_ACCEPTED(session.setEventLabel(EventCatalog::EventId::Goal, QStringLiteral("Tiro")));
  QCOMPARE(EventLabelResolver::labelForId(EventCatalog::EventId::Goal, &session,
                                          AppLocale::Language::Spanish),
           QStringLiteral("Tiro"));
  EXPECT_ACCEPTED(session.clearEventLabel(EventCatalog::EventId::Goal));
  QCOMPARE(session.eventLabelOverrides().label(EventCatalog::EventId::Shot), spacedLabel);
  QCOMPARE(overridesChanged.count(), 4);

  EXPECT_ACCEPTED(session.setEventLabel(EventCatalog::EventId::Goal, QStringLiteral("Gol")));
  QCOMPARE(EventLabelResolver::labelForId(EventCatalog::EventId::Goal, &session,
                                          AppLocale::Language::English),
           QStringLiteral("Gol"));
  QCOMPARE(EventLabelResolver::labelForId(EventCatalog::EventId::FollowUpGoal, &session,
                                          AppLocale::Language::English),
           QStringLiteral("Goal"));
  QCOMPARE(EventLabelResolver::labelForId(EventCatalog::EventId::FollowUpGoal, &session,
                                          AppLocale::Language::Spanish),
           QStringLiteral("Gol"));

  EXPECT_REJECTED(session.setEventLabel(EventCatalog::EventId::Good, QStringLiteral("Negativo")),
                 EventLabelOverrides::RejectReason::DuplicateLabel, EventCatalog::EventId::Bad);
  EXPECT_REJECTED(session.setEventLabel(EventCatalog::EventId::NoGoal, QStringLiteral("Goal")),
                 EventLabelOverrides::RejectReason::DuplicateLabel,
                 EventCatalog::EventId::FollowUpGoal);
  EXPECT_REJECTED(session.setEventLabel(EventCatalog::EventId::Left, QStringLiteral("Centro")),
                 EventLabelOverrides::RejectReason::DuplicateLabel, EventCatalog::EventId::Middle);
  EXPECT_REJECTED(session.setEventLabel(EventCatalog::EventId::Left, QStringLiteral(" centro ")),
                 EventLabelOverrides::RejectReason::DuplicateLabel, EventCatalog::EventId::Middle);

  EXPECT_ACCEPTED(session.setEventLabel(EventCatalog::EventId::Left, QStringLiteral("Banda")));
  QCOMPARE(EventLabelResolver::labelForId(EventCatalog::EventId::Left, &session,
                                          AppLocale::Language::English),
           QStringLiteral("Banda"));
  QCOMPARE(EventLabelResolver::labelForId(EventCatalog::EventId::Left, &session,
                                          AppLocale::Language::Spanish),
           QStringLiteral("Banda"));
  QCOMPARE(EventLabelResolver::labelForId(EventCatalog::EventId::Dribbling, &session,
                                          AppLocale::Language::English),
           QStringLiteral("Dribbling"));
  QCOMPARE(EventLabelResolver::labelForId(EventCatalog::EventId::Dribbling, &session,
                                          AppLocale::Language::Spanish),
           QStringLiteral("Conducción"));

  EXPECT_ACCEPTED(session.setEventLabel(EventCatalog::EventId::CircleEntry,
                                       QStringLiteral("Ingreso área")));
  QCOMPARE(EventLabelResolver::labelForId(EventCatalog::EventId::CircleEntry, &session,
                                          AppLocale::Language::English),
           QStringLiteral("Ingreso área"));
  QCOMPARE(EventLabelResolver::labelForId(EventCatalog::EventId::CircleEntry, &session,
                                          AppLocale::Language::Spanish),
           QStringLiteral("Ingreso área"));

  session.clear();
  QCOMPARE(session.tags().size(), 0);
  QCOMPARE(session.eventLabelOverrides().label(EventCatalog::EventId::Shot), spacedLabel);
  QVERIFY(session.eventLabelOverrides().hasLabel(EventCatalog::EventId::Left));

  EXPECT_ACCEPTED(session.clearEventLabel(EventCatalog::EventId::Shot));
  QCOMPARE(EventLabelResolver::labelForId(EventCatalog::EventId::Shot, &session,
                                          AppLocale::Language::Spanish),
           QStringLiteral("Tiro"));
  EXPECT_REJECTED(session.setEventLabel(EventCatalog::EventId::Goal, QStringLiteral("Tiro")),
                 EventLabelOverrides::RejectReason::DuplicateLabel, EventCatalog::EventId::Shot);
  EXPECT_ACCEPTED(session.setEventLabel(EventCatalog::EventId::Shot, QStringLiteral("Shot")));
  QCOMPARE(EventLabelResolver::labelForId(EventCatalog::EventId::Shot, &session,
                                          AppLocale::Language::English),
           QStringLiteral("Shot"));
  QCOMPARE(EventLabelResolver::labelForId(EventCatalog::EventId::Shot, &session,
                                          AppLocale::Language::Spanish),
           QStringLiteral("Shot"));
}

void EventLabelOverrideTest::hiddenSiblingIsExcludedUntilShown() {
  TagSession session;
  EXPECT_ACCEPTED(session.setEventHidden(EventCatalog::EventId::Goal, true));
  EXPECT_ACCEPTED(session.setEventLabel(EventCatalog::EventId::Shot, QStringLiteral("Gol")));
  QCOMPARE(EventLabelResolver::labelForId(EventCatalog::EventId::Shot, &session,
                                          AppLocale::Language::English),
           QStringLiteral("Gol"));

  const auto unhide = session.setEventHidden(EventCatalog::EventId::Goal, false);
  EXPECT_REJECTED(unhide, EventLabelOverrides::RejectReason::DuplicateLabel,
                 EventCatalog::EventId::Shot);
  QVERIFY(session.eventLabelOverrides().hidden(EventCatalog::EventId::Goal));
  QCOMPARE(session.eventLabelOverrides().label(EventCatalog::EventId::Shot),
           QStringLiteral("Gol"));
}

void EventLabelOverrideTest::shortcutAndHiddenChangeTheTaggingGrid() {
  TagSession session;
  EXPECT_ACCEPTED(session.setEventShortcutKey(EventCatalog::EventId::Shot, Qt::Key_P));
  EXPECT_ACCEPTED(session.setEventShortcutKey(EventCatalog::EventId::Goal, Qt::Key_P));
  EXPECT_ACCEPTED(session.setEventHidden(EventCatalog::EventId::Shot, true));
  EXPECT_ACCEPTED(session.setEventLabel(EventCatalog::EventId::Shot, kPlayerLabel));
  EXPECT_ACCEPTED(session.setEventLabel(EventCatalog::EventId::OnTarget, kFollowUpLabel));
  QCOMPARE(session.eventLabelOverrides().shortcutKey(EventCatalog::EventId::Shot), Qt::Key_P);
  QCOMPARE(session.eventLabelOverrides().shortcutKey(EventCatalog::EventId::Goal), Qt::Key_P);
  QVERIFY(session.eventLabelOverrides().hidden(EventCatalog::EventId::Shot));

  EXPECT_ACCEPTED(session.setEventShortcutKey(EventCatalog::EventId::Shot, Qt::Key_unknown));
  QVERIFY(!session.eventLabelOverrides().hasShortcutKey(EventCatalog::EventId::Shot));
  EXPECT_ACCEPTED(session.setEventShortcutKey(EventCatalog::EventId::Shot, Qt::Key_P));

  GameControls controls;
  controls.setTeamSide(TagSession::TeamSide::Home);
  controls.setEventLabelOverrides(session.eventLabelOverrides());

  QPushButton* shotButton = buttonNamed(&controls, "Shot");
  QVERIFY(shotButton);
  QVERIFY(!shotButton->isHidden());
  QVERIFY(shotButton->property("controlsHidden").toBool());
  QCOMPARE(shotButton->property("gameEventName").toString(), QStringLiteral("Shot"));
  QCOMPARE(shotButton->property("gameEventId").toInt(),
           static_cast<int>(EventCatalog::EventId::Shot));
  QCOMPARE(shortcutText(shotButton), QStringLiteral("P"));
  QCOMPARE(titleText(shotButton), kPlayerLabel);

  QSignalSpy advanced(&controls, &GameControls::tagSequenceAdvanced);
  shotButton->click();
  QCOMPARE(advanced.count(), 0);
  QVERIFY(followUpButton(&controls, "On target") == nullptr);

  EXPECT_ACCEPTED(session.setEventHidden(EventCatalog::EventId::Shot, false));
  controls.setEventLabelOverrides(session.eventLabelOverrides());
  QVERIFY(!shotButton->property("controlsHidden").toBool());
  QCOMPARE(shortcutText(shotButton), QStringLiteral("P"));
  shotButton->click();
  QCOMPARE(advanced.count(), 1);
  QCOMPARE(advanced.at(0).at(1).toString(), QStringLiteral("Shot"));
  QCOMPARE(advanced.at(0).at(5).value<EventCatalog::EventId>(), EventCatalog::EventId::Shot);

  QPushButton* onTargetButton = followUpButton(&controls, "On target");
  QVERIFY(onTargetButton);
  QCOMPARE(onTargetButton->property("gameEventKey").toString(), QStringLiteral("On target"));
  QCOMPARE(titleText(onTargetButton), kFollowUpLabel);

  QPushButton* blockedButton = followUpButton(&controls, "Blocked");
  QVERIFY(blockedButton);
  QSignalSpy committed(&controls, &GameControls::tagCommitted);
  blockedButton->click();
  QCOMPARE(committed.count(), 1);
  QCOMPARE(committed.at(0).at(0).toString(), QStringLiteral("Shot"));
  QCOMPARE(committed.at(0).at(1).toString(), QStringLiteral("Blocked"));

  QVERIFY(!TagEvent::parseLegacyFollowUpPayload(EventCatalog::EventId::Shot, kFollowUpLabel,
                                                QStringLiteral("UC"), QStringLiteral("OR"))
               .has_value());
  const std::optional<TagEvent::FollowUpChain> parsed = TagEvent::parseLegacyFollowUpPayload(
      EventCatalog::EventId::Shot, QStringLiteral("On target"), QStringLiteral("UC"),
      QStringLiteral("OR"));
  QVERIFY(parsed.has_value());
  QCOMPARE(parsed->size(), 1);
  QCOMPARE(parsed->at(0), EventCatalog::EventId::OnTarget);
}

void EventLabelOverrideTest::renameKeepsStatsGroupedById() {
  TagSession session;
  const TagEvent::FollowUpChain onTarget = TagEvent::chainOf(EventCatalog::EventId::OnTarget);
  session.addTag(manualTag(EventCatalog::EventId::Shot, QStringLiteral("Home"), 10000, onTarget));
  session.addTag(manualTag(EventCatalog::EventId::Shot, QStringLiteral("Home"), 20000));
  session.addTag(manualTag(EventCatalog::EventId::Goal, QStringLiteral("Home"), 30000));

  const TagEvent::MainEventKey shotKey = TagEvent::catalogKey(EventCatalog::EventId::Shot);
  const TagEvent::MainEventKey goalKey = TagEvent::catalogKey(EventCatalog::EventId::Goal);
  QCOMPARE(session.mainEventCounts().value(shotKey), 2);
  QCOMPARE(session.mainEventCounts().value(goalKey), 1);
  QCOMPARE(session.followUpCountsByMainEvent().value(shotKey).value(onTarget), 1);

  EXPECT_ACCEPTED(session.setEventLabel(EventCatalog::EventId::Shot, kPlayerLabel));
  EXPECT_ACCEPTED(session.setEventLabel(EventCatalog::EventId::OnTarget, kFollowUpLabel));
  QCOMPARE(session.tags().at(0).mainEventId, EventCatalog::EventId::Shot);
  QCOMPARE(session.tags().at(1).mainEventId, EventCatalog::EventId::Shot);
  QCOMPARE(session.mainEventCounts().value(shotKey), 2);
  QCOMPARE(session.followUpCountsByMainEvent().value(shotKey).value(onTarget), 1);

  StatsWindow stats;
  stats.setTagSession(&session);
  QTreeWidget* tree = stats.findChild<QTreeWidget*>(QStringLiteral("StatsTree"));
  QVERIFY(tree);
  QCOMPARE(tree->topLevelItemCount(), 2);
  QCOMPARE(topLevelItemWithId(tree, EventCatalog::EventId::Goal), tree->topLevelItem(0));
  QTreeWidgetItem* shotItem = tree->topLevelItem(1);
  QCOMPARE(shotItem->text(0), kPlayerLabel);
  QCOMPARE(shotItem->text(1), QStringLiteral("2"));
  QCOMPARE(shotItem->data(0, Qt::UserRole).value<TagEvent::MainEventKey>().id,
           EventCatalog::EventId::Shot);
  QCOMPARE(shotItem->childCount(), 1);
  QCOMPARE(shotItem->child(0)->text(0), QStringLiteral("  ") + kFollowUpLabel);
  QCOMPARE(shotItem->child(0)->text(1), QStringLiteral("1 (50%)"));
  QCOMPARE(shotItem->child(0)->data(0, Qt::UserRole + 1).value<TagEvent::FollowUpChain>().at(0),
           EventCatalog::EventId::OnTarget);
  QCOMPARE(tree->topLevelItem(0)->text(0), QStringLiteral("Goal"));

  EXPECT_ACCEPTED(session.setEventLabel(EventCatalog::EventId::Shot, QStringLiteral("Jugadora 9")));
  QCOMPARE(tree->topLevelItemCount(), 2);
  QCOMPARE(tree->topLevelItem(0)->text(0), QStringLiteral("Goal"));
  QCOMPARE(tree->topLevelItem(1)->text(0), QStringLiteral("Jugadora 9"));
  QCOMPARE(tree->topLevelItem(1)->data(0, Qt::UserRole).value<TagEvent::MainEventKey>().id,
           EventCatalog::EventId::Shot);
  QCOMPARE(session.mainEventCounts().value(shotKey), 2);
}

void EventLabelOverrideTest::exportRoundTripKeepsStableCodesAndWritesTheDisplayLabel() {
  WarningCapture warnings;
  InstalledWarningCapture capture(&warnings);

  TagSession plain;
  TagSession renamed;
  configureMatch(plain);
  configureMatch(renamed);
  const TagEvent::FollowUpChain onTarget = TagEvent::chainOf(EventCatalog::EventId::OnTarget);
  plain.addTag(manualTag(EventCatalog::EventId::Shot, QStringLiteral("Home"), 30000, onTarget));
  plain.addTag(manualTag(EventCatalog::EventId::Goal, QStringLiteral("Home"), 40000));
  renamed.addTag(manualTag(EventCatalog::EventId::Shot, QStringLiteral("Home"), 30000, onTarget));
  renamed.addTag(manualTag(EventCatalog::EventId::Goal, QStringLiteral("Home"), 40000));
  EXPECT_ACCEPTED(renamed.setEventLabel(EventCatalog::EventId::Shot, kPlayerLabel));
  EXPECT_ACCEPTED(renamed.setEventLabel(EventCatalog::EventId::OnTarget, kFollowUpLabel));

  const QByteArray plainXml = xmlBytes(plain);
  const QByteArray renamedXml = xmlBytes(renamed);
  QVERIFY(!plainXml.isEmpty());
  QCOMPARE(renamedXml, plainXml);
  const QString xmlText = QString::fromUtf8(renamedXml);
  QVERIFY(xmlText.contains(QStringLiteral("UC SHOT+")));
  QVERIFY(xmlText.contains(QStringLiteral("UC GOAL+")));
  QVERIFY(!xmlText.contains(kPlayerLabel));
  QVERIFY(!xmlText.contains(kFollowUpLabel));

  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString xmlPath = directory.filePath(QStringLiteral("match.xml"));
  QString error;
  QVERIFY2(XmlExporter::writeAllInstances(&renamed, xmlPath, &error), qPrintable(error));
  QVector<XmlImporter::ParsedInstance> instances;
  QVERIFY2(XmlImporter::parse(xmlPath, &instances, &error), qPrintable(error));
  XmlEventMappingDialog dialog(instances, 0, &renamed);
  auto* table = dialog.findChild<QTableWidget*>(QStringLiteral("XmlEventMappingTable"));
  QVERIFY(table);
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
  bool importedShot = false;
  for (const TagSession::GameTag& tag : dialog.importResult().tags) {
    if (tag.mainEventId != EventCatalog::EventId::Shot) continue;
    importedShot = true;
    QVERIFY(tag.followUpChain().empty());
  }
  QVERIFY(importedShot);
  QCOMPARE(TagEvent::mainEventKeyFromExportedCode(QStringLiteral("UC SHOT+")).id,
           EventCatalog::EventId::Shot);

  const PresentationQueue::Clip plainClip = clipFromTag(plain.tags().at(0), 0);
  const PresentationQueue::Clip renamedClip = clipFromTag(renamed.tags().at(0), 0);
  ExportClipBuilder::OverlayOptions englishOptions;
  englishOptions.includeBottomOverlay = true;
  englishOptions.includeScoreboardOverlay = false;
  englishOptions.includeNotesOverlay = false;
  englishOptions.language = AppLocale::Language::English;
  ExportClipBuilder::OverlayOptions spanishOptions = englishOptions;
  spanishOptions.language = AppLocale::Language::Spanish;

  const QVector<ClipSegment> plainEnglish =
      ExportClipBuilder::buildClipSegments(&plain, {plainClip}, englishOptions);
  const QVector<ClipSegment> renamedEnglish =
      ExportClipBuilder::buildClipSegments(&renamed, {renamedClip}, englishOptions);
  const QVector<ClipSegment> plainSpanish =
      ExportClipBuilder::buildClipSegments(&plain, {plainClip}, spanishOptions);
  const QVector<ClipSegment> renamedSpanish =
      ExportClipBuilder::buildClipSegments(&renamed, {renamedClip}, spanishOptions);
  QCOMPARE(plainEnglish.size(), 1);
  QCOMPARE(renamedEnglish.at(0).compilation.code, plainEnglish.at(0).compilation.code);
  QCOMPARE(renamedEnglish.at(0).compilation.code, QStringLiteral("UC SHOT+"));
  QCOMPARE(plainEnglish.at(0).compilation.overlayLabel, QStringLiteral("UC - Shot"));
  QCOMPARE(renamedEnglish.at(0).compilation.overlayLabel,
           QStringLiteral("UC - ") + kPlayerLabel);
  QCOMPARE(plainSpanish.at(0).compilation.overlayLabel, QStringLiteral("UC - Tiro"));
  QCOMPARE(renamedSpanish.at(0).compilation.overlayLabel,
           QStringLiteral("UC - ") + kPlayerLabel);

  const QString plainJson = sidecarJson(plainEnglish.at(0).compilation);
  const QString renamedJson = sidecarJson(renamedEnglish.at(0).compilation);
  QVERIFY(plainJson.contains(QStringLiteral("\"code\": \"UC SHOT+\"")));
  QVERIFY(renamedJson.contains(QStringLiteral("\"code\": \"UC SHOT+\"")));
  QVERIFY(renamedJson.contains(QStringLiteral("\"overlayLabel\": \"UC - ") + kPlayerLabel +
                                QStringLiteral("\"")));
  QVERIFY(!renamedJson.contains(kFollowUpLabel));
  QVERIFY(!plainJson.contains(kPlayerLabel));

  const QString renamedFileName =
      ExportClipBuilder::compilationBaseName(&renamed, {renamedClip});
  QVERIFY(renamedFileName.contains(kPlayerLabel));
  QVERIFY(!renamedFileName.contains(QStringLiteral("Shot")));
  AppLocale::setLanguage(AppLocale::Language::Spanish);
  const QString renamedSpanishFileName =
      ExportClipBuilder::compilationBaseName(&renamed, {renamedClip});
  QVERIFY(renamedSpanishFileName.contains(kPlayerLabel));
  QVERIFY(ExportClipBuilder::compilationBaseName(&plain, {plainClip}).contains(
      QStringLiteral("Tiro")));
  AppLocale::setLanguage(AppLocale::Language::English);

  TagSession bare;
  bare.setGameTeams(QStringLiteral("UC"), QStringLiteral("OR"), QStringLiteral("#3c5ac8"),
                    QStringLiteral("#c83c3c"));
  bare.addTag(manualTag(EventCatalog::EventId::Shot, QStringLiteral("Home"), 30000, onTarget));
  EXPECT_ACCEPTED(bare.setEventLabel(EventCatalog::EventId::Shot, kPlayerLabel));
  EXPECT_ACCEPTED(bare.setEventLabel(EventCatalog::EventId::OnTarget, kFollowUpLabel));
  const QVector<ClipSegment> bareSegments = ExportClipBuilder::buildClipSegments(
      &bare, {clipFromTag(bare.tags().at(0), 0)}, englishOptions);
  QCOMPARE(bareSegments.at(0).compilation.code, QStringLiteral("Shot"));
  QCOMPARE(bareSegments.at(0).compilation.overlayLabel, QStringLiteral("UC - ") + kPlayerLabel);
  const QString bareXml = QString::fromUtf8(xmlBytes(bare));
  QVERIFY(bareXml.contains(QStringLiteral("<code>Shot</code>")));
  QVERIFY(!bareXml.contains(kPlayerLabel));
  QVERIFY(!bareXml.contains(kFollowUpLabel));

  QVERIFY(bare.setTagNote(0, QStringLiteral("keep the note")));
  const QString notesPath = directory.filePath(QStringLiteral("match.notes.txt"));
  QVERIFY2(SessionNotesWriter::writeFile(bare, notesPath, &error), qPrintable(error));
  QFile notesFile(notesPath);
  QVERIFY(notesFile.open(QIODevice::ReadOnly));
  const QString notes = QString::fromUtf8(notesFile.readAll());
  QVERIFY(notes.contains(kPlayerLabel));
  QVERIFY(notes.contains(kFollowUpLabel));
  QVERIFY(notes.contains(QStringLiteral("keep the note")));
  QVERIFY(!notes.contains(QStringLiteral("On target")));
  QVERIFY(!notes.contains(QStringLiteral("Shot")));

  QCOMPARE(ExportClipBuilder::eventLabelForExportFileName(QStringLiteral("Special")),
           QStringLiteral("special"));
  TagSession special;
  configureMatch(special);
  special.addTag(manualTag(EventCatalog::EventId::Special, QStringLiteral("Home"), 12000));
  const PresentationQueue::Clip specialClip = clipFromTag(special.tags().at(0), 0);
  const QString factorySpecialName =
      ExportClipBuilder::compilationBaseName(&special, {specialClip});
  QVERIFY(factorySpecialName.contains(QStringLiteral("special")));
  QVERIFY(!factorySpecialName.contains(kStar));
  EXPECT_ACCEPTED(special.setEventLabel(EventCatalog::EventId::Special, QStringLiteral("Estrella")));
  QVERIFY(ExportClipBuilder::compilationBaseName(&special, {specialClip}).contains(
      QStringLiteral("Estrella")));
}

void EventLabelOverrideTest::renamePropagatesThroughEveryDisplayPath() {
  TagSession session;
  configureMatch(session);
  const TagEvent::FollowUpChain onTarget = TagEvent::chainOf(EventCatalog::EventId::OnTarget);
  session.addTag(manualTag(EventCatalog::EventId::Shot, QStringLiteral("Home"), 10000, onTarget));
  session.addTag(manualTag(EventCatalog::EventId::Shot, QStringLiteral("Home"), 20000));
  session.addTag(manualTag(EventCatalog::EventId::Goal, QStringLiteral("Home"), 30000));
  EXPECT_ACCEPTED(session.setEventLabel(EventCatalog::EventId::Shot, kPlayerLabel));
  EXPECT_ACCEPTED(session.setEventLabel(EventCatalog::EventId::OnTarget, kFollowUpLabel));
  const QString shotLine = kPlayerLabel + AppLocale::kCompoundPathSeparator + kFollowUpLabel;

  QLabel stageEvent;
  QLabel stageContext;
  QLabel stageNote;
  PresentationQueue queue;
  PresentationPlaybackController controller;
  queue.setTagSession(&session);
  controller.setStageLabels(&stageEvent, &stageContext, &stageNote);
  controller.setTagSession(&session);
  controller.setPresentationQueue(&queue);
  controller.wirePresentationQueueSignals();
  queue.setSelectedTagIndexes({0});
  QCOMPARE(stageEvent.text(), shotLine);

  WorkWindow window;
  window.setTagSession(&session);

  QTableView* tagsTable = window.findChild<QTableView*>(QStringLiteral("TagsTable"));
  QVERIFY(tagsTable);
  QVERIFY(tagsTable->model());
  QCOMPARE(tagsTable->model()->index(0, 2).data().toString(), shotLine);
  QCOMPARE(tagsTable->model()->index(1, 2).data().toString(), kPlayerLabel);
  QCOMPARE(tagsTable->model()->index(2, 2).data().toString(), QStringLiteral("Goal"));

  QMenu* filterMenu = window.findChild<QMenu*>(QStringLiteral("TagsFilterMenu"));
  QVERIFY(filterMenu);
  QAction* shotFilter = nullptr;
  for (QAction* action : filterMenu->actions()) {
    const TagEvent::MainEventKey key = action->data().value<TagEvent::MainEventKey>();
    if (key.id == EventCatalog::EventId::Shot) shotFilter = action;
  }
  QVERIFY(shotFilter);
  QCOMPARE(shotFilter->text(), kPlayerLabel);

  QTreeWidget* statsTree = window.findChild<QTreeWidget*>(QStringLiteral("StatsTree"));
  QVERIFY(statsTree);
  QTreeWidgetItem* shotStats = topLevelItemWithId(statsTree, EventCatalog::EventId::Shot);
  QVERIFY(shotStats);
  QCOMPARE(shotStats->text(0), kPlayerLabel);
  QCOMPARE(shotStats->text(1), QStringLiteral("2"));
  QCOMPARE(shotStats->child(0)->text(0), QStringLiteral("  ") + kFollowUpLabel);

  auto* eventFilter = window.findChild<QComboBox*>(QStringLiteral("PresentationEventFilter"));
  QVERIFY(eventFilter);
  const int shotFilterIndex =
      eventFilter->findData(static_cast<int>(EventCatalog::EventId::Shot));
  QVERIFY(shotFilterIndex >= 0);
  QCOMPARE(eventFilter->itemText(shotFilterIndex), QStringLiteral("Jugadora 7  (2)"));

  auto* instances = window.findChild<QTableView*>(QStringLiteral("PresentationInstancesTable"));
  QVERIFY(instances);
  QVERIFY(instances->model());
  QCOMPARE(instances->model()->index(0, 2).data().toString(), shotLine);
  QVERIFY(instances->model()->setData(instances->model()->index(0, 0), Qt::Checked,
                                     Qt::CheckStateRole));
  auto* presentationStage = window.findChild<QLabel*>(QStringLiteral("PresentationEventLabel"));
  QVERIFY(presentationStage);
  QCOMPARE(presentationStage->text(), shotLine);

  GameControls* controls = window.findChild<GameControls*>();
  QVERIFY(controls);
  QPushButton* shotButton = buttonNamed(controls, "Shot");
  QPushButton* passButton = buttonNamed(controls, "Pass");
  QVERIFY(shotButton);
  QVERIFY(passButton);
  QCOMPARE(titleText(shotButton), kPlayerLabel);
  QCOMPARE(shotButton->property("gameEventName").toString(), QStringLiteral("Shot"));
  QCOMPARE(shortcutText(shotButton), QStringLiteral("D"));

  ClipDurationSettingsDialog durations(&session);
  connect(&LocaleNotifier::instance(), &LocaleNotifier::languageChanged, &durations,
          &ClipDurationSettingsDialog::applyUiStrings);
  QLabel* durationShot = labelWithText(&durations, kPlayerLabel);
  QLabel* durationPass = labelWithText(&durations, QStringLiteral("Pass"));
  QVERIFY(durationShot);
  QVERIFY(durationPass);

  auto* notesEditor = window.findChild<MatchNotesEditor*>();
  QVERIFY(notesEditor);
  QTest::keyClicks(notesEditor, QStringLiteral("@"));
  auto* mentionPopup = notesEditor->findChild<QListWidget*>(QStringLiteral("MatchNoteMentionPopup"));
  QVERIFY(mentionPopup);
  QStringList mentionLabels;
  for (int row = 0; row < mentionPopup->count(); ++row) {
    mentionLabels.append(mentionPopup->item(row)->text());
  }
  QVERIFY(mentionLabels.join(QLatin1Char('\n')).contains(shotLine));

  shotButton->click();
  auto* toast = window.findChild<TagToastOverlay*>(QStringLiteral("TagToastOverlay"));
  QVERIFY(toast);
  QCOMPARE(toast->chainText(), kPlayerLabel);
  QPushButton* onTargetButton = followUpButton(controls, "On target");
  QVERIFY(onTargetButton);
  QCOMPARE(onTargetButton->property("gameEventKey").toString(), QStringLiteral("On target"));
  QCOMPARE(titleText(onTargetButton), kFollowUpLabel);
  onTargetButton->click();
  QVERIFY(toast->chainText().contains(kPlayerLabel));
  QVERIFY(toast->chainText().contains(kFollowUpLabel));
  QCOMPARE(session.tags().size(), 3);

  EXPECT_ACCEPTED(session.setEventLabel(EventCatalog::EventId::Shot, QStringLiteral("Jugadora 9")));
  const QString renamedLine =
      QStringLiteral("Jugadora 9") + AppLocale::kCompoundPathSeparator + kFollowUpLabel;
  QCOMPARE(stageEvent.text(), renamedLine);
  QCOMPARE(presentationStage->text(), renamedLine);
  QCOMPARE(tagsTable->model()->index(0, 2).data().toString(), renamedLine);
  QCOMPARE(shotFilter->text(), QStringLiteral("Jugadora 9"));
  shotStats = topLevelItemWithId(statsTree, EventCatalog::EventId::Shot);
  QVERIFY(shotStats);
  QCOMPARE(shotStats->text(0), QStringLiteral("Jugadora 9"));
  QCOMPARE(shotStats->data(0, Qt::UserRole).value<TagEvent::MainEventKey>().id,
           EventCatalog::EventId::Shot);
  QCOMPARE(eventFilter->itemText(eventFilter->findData(static_cast<int>(EventCatalog::EventId::Shot))),
           QStringLiteral("Jugadora 9  (2)"));
  QCOMPARE(instances->model()->index(0, 2).data().toString(), renamedLine);
  QCOMPARE(titleText(shotButton), QStringLiteral("Jugadora 9"));
  QCOMPARE(durationShot->text(), QStringLiteral("Jugadora 9"));
  QCOMPARE(session.mainEventCounts().value(TagEvent::catalogKey(EventCatalog::EventId::Shot)), 2);

  AppLocale::setLanguage(AppLocale::Language::Spanish);
  QCOMPARE(titleText(shotButton), QStringLiteral("Jugadora 9"));
  QCOMPARE(titleText(passButton), QStringLiteral("Pase"));
  QCOMPARE(durationShot->text(), QStringLiteral("Jugadora 9"));
  QCOMPARE(durationPass->text(), QStringLiteral("Pase"));
  QCOMPARE(tagsTable->model()->index(0, 2).data().toString(), renamedLine);
  QCOMPARE(topLevelItemWithId(statsTree, EventCatalog::EventId::Goal)->text(0),
           QStringLiteral("Gol"));
  shotStats = topLevelItemWithId(statsTree, EventCatalog::EventId::Shot);
  QVERIFY(shotStats);
  QCOMPARE(shotStats->text(0), QStringLiteral("Jugadora 9"));
  QCOMPARE(EventLabelResolver::labelForId(EventCatalog::EventId::Pass, &session,
                                          AppLocale::Language::Spanish),
           QStringLiteral("Pase"));
}

int runEventLabelOverrideTests(int argc, char** argv) {
  EventLabelOverrideTest tests;
  return QTest::qExec(&tests, argc, argv);
}

#include "EventLabelOverrideTests.moc"
