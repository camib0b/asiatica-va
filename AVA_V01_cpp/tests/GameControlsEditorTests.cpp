#include "AppLocale.h"
#include "EventCatalog.h"
#include "EventControlsSetup.h"
#include "EventControlsTemplateStore.h"
#include "EventLabelOverrides.h"
#include "EventLabelResolver.h"
#include "GameControls.h"
#include "GameControlsContextMenu.h"
#include "GameControlsEditorDialog.h"
#include "MatchSessionFile.h"
#include "TagSession.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeWidget>

#include <initializer_list>

namespace {

const QString kPlayerLabel = QStringLiteral("Jugadora 7");

void confirmYes() {
  QTimer::singleShot(0, []() {
    auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
    if (!box) return;
    if (auto* yes = box->findChild<QPushButton*>(QStringLiteral("ControlsConfirmYes"))) {
      yes->click();
      return;
    }
    box->reject();
  });
}

QPushButton* gridButton(QWidget* dialog, EventCatalog::EventId eventId) {
  if (!dialog) return nullptr;
  return dialog->findChild<QPushButton*>(
      QStringLiteral("ControlsGrid%1").arg(static_cast<int>(eventId)));
}

void selectEvent(QWidget* dialog, EventCatalog::EventId eventId) {
  if (QPushButton* button = gridButton(dialog, eventId)) button->click();
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

QString fullLabelOf(QPushButton* button) {
  if (!button) return {};
  for (QLabel* label : button->findChildren<QLabel*>()) {
    if (!label->property("fullLabel").isValid()) continue;
    return label->property("fullLabel").toString();
  }
  return {};
}

QTreeWidgetItem* itemWithId(QTreeWidgetItem* item, EventCatalog::EventId eventId) {
  if (!item) return nullptr;
  if (item->data(0, Qt::UserRole).toInt() == static_cast<int>(eventId)) return item;
  for (int index = 0; index < item->childCount(); ++index) {
    if (QTreeWidgetItem* found = itemWithId(item->child(index), eventId)) return found;
  }
  return nullptr;
}

QTreeWidgetItem* treeItem(QTreeWidget* tree, EventCatalog::EventId eventId) {
  if (!tree) return nullptr;
  for (int index = 0; index < tree->topLevelItemCount(); ++index) {
    if (QTreeWidgetItem* found = itemWithId(tree->topLevelItem(index), eventId)) return found;
  }
  return nullptr;
}

void showTaggingGrid(GameControls* controls) {
  controls->setTeamSide(TagSession::TeamSide::Home);
  controls->resize(420, 760);
  controls->show();
  controls->setFocus();
  QApplication::processEvents();
}

EventLabelOverrides::FollowUpPath makePath(EventCatalog::EventId main,
                                           std::initializer_list<EventCatalog::EventId> steps) {
  EventLabelOverrides::FollowUpPath path;
  path.mainEventId = main;
  path.stepCount = static_cast<int>(steps.size());
  int index = 0;
  for (const EventCatalog::EventId step : steps) {
    path.steps[static_cast<std::size_t>(index)] = step;
    ++index;
  }
  return path;
}

struct SharedPopupCapture {
  bool seen = false;
  bool applied = false;
  QStringList lines;
};

void armSharedPopup(const char* buttonName, SharedPopupCapture* capture) {
  QTimer::singleShot(0, [buttonName, capture]() {
    auto* popup = qobject_cast<QDialog*>(QApplication::activeModalWidget());
    if (popup == nullptr || popup->objectName() != QLatin1String("ControlsSharedPaths")) {
      if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) dialog->reject();
      return;
    }
    capture->seen = true;
    if (auto* list = popup->findChild<QListWidget*>(QStringLiteral("ControlsSharedList"))) {
      for (int index = 0; index < list->count(); ++index) {
        if (list->item(index) != nullptr) capture->lines.append(list->item(index)->text());
      }
    }
    auto* button = popup->findChild<QPushButton*>(QLatin1String(buttonName));
    if (button == nullptr) {
      popup->reject();
      return;
    }
    capture->applied = true;
    button->click();
  });
}

void collectTreeItems(QTreeWidgetItem* item, EventCatalog::EventId eventId,
                      QList<QTreeWidgetItem*>* found) {
  if (item == nullptr || found == nullptr) return;
  if (item->data(0, Qt::UserRole).toInt() == static_cast<int>(eventId)) found->append(item);
  for (int index = 0; index < item->childCount(); ++index) {
    collectTreeItems(item->child(index), eventId, found);
  }
}

QList<QTreeWidgetItem*> treeItems(QTreeWidget* tree, EventCatalog::EventId eventId) {
  QList<QTreeWidgetItem*> found;
  if (tree == nullptr) return found;
  for (int index = 0; index < tree->topLevelItemCount(); ++index) {
    collectTreeItems(tree->topLevelItem(index), eventId, &found);
  }
  return found;
}

QMenu* menuForEvent(QMenu* root, EventCatalog::EventId eventId) {
  if (root == nullptr) return nullptr;
  const QList<QMenu*> menus = root->findChildren<QMenu*>();
  for (QMenu* menu : menus) {
    if (menu->property("eventId").toInt() == static_cast<int>(eventId)) return menu;
  }
  return nullptr;
}

bool writeBytes(const QString& path, const QByteArray& bytes) {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
  return file.write(bytes) == bytes.size();
}

}  // namespace

class GameControlsEditorTest : public QObject {
  Q_OBJECT

 private slots:
  void init();
  void cleanup();
  void editorRenamesShotToJugadora7();
  void contextMenuRenamesShotToJugadora7();
  void keyChangeAndConflict();
  void hideMainEventAndFollowUp();
  void templateSaveAndApply();
  void resetToDefaultKeepsSessionUntilSave();
  void emptyNameBlocksSave();
  void contextMenuOffersExistingEditsOnly();
  void breakUiFixtures();
  void spanishChrome();
  void sharedGoalRenameOnPenaltyStrokeOnly();
  void sharedGoalRenameAppliesToEveryUse();
  void hidingGoalOnPenaltyStrokeKeepsShotTaggable();
  void circleEntryLeftRenameSkipsThePopup();
  void oldSessionAndTemplateFilesLoad();
  void sharedPathListBreakUi();

 private:
  AppLocale::Language previousLanguage_ = AppLocale::Language::English;
};

void GameControlsEditorTest::init() {
  previousLanguage_ = AppLocale::currentLanguage();
  AppLocale::setLanguage(AppLocale::Language::English);
  qunsetenv("AVA_BREAK_UI");
}

void GameControlsEditorTest::cleanup() {
  AppLocale::setLanguage(previousLanguage_);
  qunsetenv("AVA_BREAK_UI");
}

void GameControlsEditorTest::editorRenamesShotToJugadora7() {
  TagSession session;
  TagSession::GameTag tag;
  tag.mainEventId = EventCatalog::EventId::Shot;
  tag.markMs = 1000;
  tag.startMs = 0;
  tag.endMs = 2000;
  tag.team = QStringLiteral("Home");
  tag.period = QStringLiteral("Q1");
  session.addTag(tag);

  GameControlsEditorDialog dialog(&session);
  dialog.show();
  selectEvent(&dialog, EventCatalog::EventId::Shot);
  auto* name = dialog.findChild<QLineEdit*>(QStringLiteral("ControlsEditorName"));
  QVERIFY(name);
  name->setText(kPlayerLabel);
  auto* save = dialog.findChild<QPushButton*>(QStringLiteral("ControlsEditorSave"));
  QVERIFY(save);
  QVERIFY(save->isEnabled());
  save->click();

  QCOMPARE(session.tags().size(), 1);
  QCOMPARE(session.tags().at(0).mainEventId, EventCatalog::EventId::Shot);
  QCOMPARE(session.eventLabelOverrides().label(EventCatalog::EventId::Shot), kPlayerLabel);

  GameControls controls;
  controls.setTagSession(&session);
  showTaggingGrid(&controls);
  QPushButton* shotButton = buttonNamed(&controls, "Shot");
  QVERIFY(shotButton);
  QCOMPARE(shotButton->property("gameEventName").toString(), QStringLiteral("Shot"));
  QCOMPARE(titleText(shotButton), kPlayerLabel);

  AppLocale::setLanguage(AppLocale::Language::Spanish);
  controls.applyUiLanguage();
  QCOMPARE(titleText(shotButton), kPlayerLabel);
  QCOMPARE(EventLabelResolver::labelForId(EventCatalog::EventId::Shot, &session,
                                          AppLocale::Language::Spanish),
           kPlayerLabel);
  QCOMPARE(titleText(buttonNamed(&controls, "Pass")), QStringLiteral("Pase"));
}

void GameControlsEditorTest::contextMenuRenamesShotToJugadora7() {
  TagSession session;
  QMenu menu;
  GameControlsContextMenu::populate(&menu, &session, EventCatalog::EventId::Shot, []() {});
  auto* rename = menu.findChild<QAction*>(QStringLiteral("ControlsMenuRename"));
  QVERIFY(rename);
  QTimer::singleShot(0, []() {
    QWidget* modal = QApplication::activeModalWidget();
    if (!modal) return;
    auto* field = modal->findChild<QLineEdit*>(QStringLiteral("ControlsRenameField"));
    auto* ok = modal->findChild<QPushButton*>(QStringLiteral("ControlsRenameOk"));
    if (!field || !ok) {
      if (auto* dialog = qobject_cast<QDialog*>(modal)) dialog->reject();
      return;
    }
    field->setText(kPlayerLabel);
    ok->click();
  });
  rename->trigger();
  QCOMPARE(session.eventLabelOverrides().label(EventCatalog::EventId::Shot), kPlayerLabel);

  GameControls controls;
  controls.setTagSession(&session);
  QPushButton* shotButton = buttonNamed(&controls, "Shot");
  QVERIFY(shotButton);
  QCOMPARE(shotButton->property("gameEventName").toString(), QStringLiteral("Shot"));
  QCOMPARE(titleText(shotButton), kPlayerLabel);
}

void GameControlsEditorTest::keyChangeAndConflict() {
  TagSession session;
  GameControlsEditorDialog dialog(&session);
  dialog.show();
  selectEvent(&dialog, EventCatalog::EventId::Shot);
  dialog.findChild<QLineEdit*>(QStringLiteral("ControlsEditorName"))->setText(kPlayerLabel);
  dialog.findChild<QLineEdit*>(QStringLiteral("ControlsEditorKey"))->setText(QStringLiteral("K"));
  dialog.findChild<QPushButton*>(QStringLiteral("ControlsEditorSave"))->click();
  QCOMPARE(session.eventLabelOverrides().shortcutKey(EventCatalog::EventId::Shot), Qt::Key_K);
  QVERIFY(!session.eventLabelOverrides().hasShortcutKey(EventCatalog::EventId::Goal));

  GameControls controls;
  controls.setTagSession(&session);
  showTaggingGrid(&controls);
  controls.activateWindow();
  controls.setFocus(Qt::OtherFocusReason);
  QSignalSpy advanced(&controls, &GameControls::tagSequenceAdvanced);
  QTest::keyClick(&controls, Qt::Key_K);
  QCOMPARE(advanced.count(), 1);
  QCOMPARE(advanced.at(0).at(1).toString(), QStringLiteral("Shot"));
  QTest::keyClick(&controls, Qt::Key_D);
  QCOMPARE(advanced.count(), 1);

  GameControlsEditorDialog again(&session);
  again.show();
  selectEvent(&again, EventCatalog::EventId::Goal);
  auto* key = again.findChild<QLineEdit*>(QStringLiteral("ControlsEditorKey"));
  QVERIFY(key);
  key->setText(QStringLiteral("K"));
  auto* error = again.findChild<QLabel*>(QStringLiteral("ControlsEditorKeyError"));
  QVERIFY(error);
  QVERIFY(error->text().contains(kPlayerLabel));
  QVERIFY(error->text().contains(QStringLiteral("K")));
  QVERIFY(!again.findChild<QPushButton*>(QStringLiteral("ControlsEditorSave"))->isEnabled());
  QVERIFY(!session.eventLabelOverrides().hasShortcutKey(EventCatalog::EventId::Goal));
  QCOMPARE(session.eventLabelOverrides().shortcutKey(EventCatalog::EventId::Goal), Qt::Key_unknown);
  QCOMPARE(shortcutText(buttonNamed(&controls, "Goal")), QStringLiteral("F"));
  again.reject();
}

void GameControlsEditorTest::hideMainEventAndFollowUp() {
  TagSession session;
  {
    GameControlsEditorDialog dialog(&session);
    dialog.show();
    selectEvent(&dialog, EventCatalog::EventId::Shot);
    confirmYes();
    dialog.findChild<QCheckBox*>(QStringLiteral("ControlsEditorShown"))->setChecked(false);
    QVERIFY(session.eventLabelOverrides().hidden(EventCatalog::EventId::Shot) == false);
    dialog.findChild<QPushButton*>(QStringLiteral("ControlsEditorSave"))->click();
  }
  QVERIFY(session.eventLabelOverrides().hidden(EventCatalog::EventId::Shot));

  GameControls controls;
  controls.setTagSession(&session);
  showTaggingGrid(&controls);
  QPushButton* shotButton = buttonNamed(&controls, "Shot");
  QVERIFY(shotButton);
  QVERIFY(!shotButton->isHidden());
  QVERIFY(shotButton->property("controlsHidden").toBool());
  QSignalSpy advanced(&controls, &GameControls::tagSequenceAdvanced);
  shotButton->click();
  QTest::keyClick(&controls, Qt::Key_D);
  QCOMPARE(advanced.count(), 0);

  {
    GameControlsEditorDialog dialog(&session);
    dialog.show();
    selectEvent(&dialog, EventCatalog::EventId::Shot);
    dialog.findChild<QCheckBox*>(QStringLiteral("ControlsEditorShown"))->setChecked(true);
    auto* tree = dialog.findChild<QTreeWidget*>(QStringLiteral("ControlsEditorFollowUps"));
    QVERIFY(tree);
    QTreeWidgetItem* onTarget = treeItem(tree, EventCatalog::EventId::OnTarget);
    QVERIFY(onTarget);
    confirmYes();
    onTarget->setCheckState(1, Qt::Unchecked);
    dialog.findChild<QPushButton*>(QStringLiteral("ControlsEditorSave"))->click();
  }
  QVERIFY(!session.eventLabelOverrides().hidden(EventCatalog::EventId::Shot));
  QVERIFY(session.eventLabelOverrides().hidden(EventCatalog::EventId::OnTarget));

  controls.setEventLabelOverrides(session.eventLabelOverrides());
  shotButton->click();
  QVERIFY(followUpButton(&controls, "On target") == nullptr);
  QVERIFY(followUpButton(&controls, "Off target"));
  QVERIFY(followUpButton(&controls, "Blocked"));
}

void GameControlsEditorTest::templateSaveAndApply() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  TagSession session;
  GameControlsEditorDialog dialog(&session, nullptr, directory.path());
  dialog.show();
  selectEvent(&dialog, EventCatalog::EventId::Shot);
  dialog.findChild<QLineEdit*>(QStringLiteral("ControlsEditorName"))->setText(kPlayerLabel);
  auto* templateName = dialog.findChild<QLineEdit*>(QStringLiteral("ControlsEditorTemplateName"));
  QVERIFY(templateName);
  templateName->setText(QStringLiteral("Camila's layout"));
  dialog.findChild<QPushButton*>(QStringLiteral("ControlsEditorSaveTemplate"))->click();
  auto* status = dialog.findChild<QLabel*>(QStringLiteral("ControlsEditorTemplateStatus"));
  QVERIFY(status);
  QCOMPARE(status->text(), AppLocale::trUi("controls_editor.template_builtin"));
  QVERIFY(!session.eventLabelOverrides().hasLabel(EventCatalog::EventId::Shot));

  templateName->setText(QStringLiteral("Night match"));
  dialog.findChild<QPushButton*>(QStringLiteral("ControlsEditorSaveTemplate"))->click();
  QVERIFY(status->text().contains(QStringLiteral("Night match")));
  QVERIFY(!session.eventLabelOverrides().hasLabel(EventCatalog::EventId::Shot));

  auto* combo = dialog.findChild<QComboBox*>(QStringLiteral("ControlsEditorTemplates"));
  QVERIFY(combo);
  const int builtinIndex = combo->findData(EventControlsSetup::builtinDefaultName());
  QVERIFY(builtinIndex >= 0);
  combo->setCurrentIndex(builtinIndex);
  QVERIFY(!dialog.findChild<QPushButton*>(QStringLiteral("ControlsEditorRemoveTemplate"))->isEnabled());

  TagSession other;
  GameControlsEditorDialog second(&other, nullptr, directory.path());
  second.show();
  auto* secondCombo = second.findChild<QComboBox*>(QStringLiteral("ControlsEditorTemplates"));
  QVERIFY(secondCombo);
  const int nightIndex = secondCombo->findData(QStringLiteral("Night match"));
  QVERIFY(nightIndex >= 0);
  secondCombo->setCurrentIndex(nightIndex);
  second.findChild<QPushButton*>(QStringLiteral("ControlsEditorLoad"))->click();
  selectEvent(&second, EventCatalog::EventId::Shot);
  QCOMPARE(second.findChild<QLineEdit*>(QStringLiteral("ControlsEditorName"))->text(), kPlayerLabel);
  QVERIFY(!other.eventLabelOverrides().hasLabel(EventCatalog::EventId::Shot));
  second.findChild<QPushButton*>(QStringLiteral("ControlsEditorSave"))->click();
  QCOMPARE(other.eventLabelOverrides().label(EventCatalog::EventId::Shot), kPlayerLabel);
  QCOMPARE(session.eventLabelOverrides().label(EventCatalog::EventId::Shot), QString());
}

void GameControlsEditorTest::resetToDefaultKeepsSessionUntilSave() {
  TagSession session;
  QVERIFY(session.setEventLabel(EventCatalog::EventId::Shot, kPlayerLabel).accepted);

  GameControlsEditorDialog dialog(&session);
  dialog.show();
  selectEvent(&dialog, EventCatalog::EventId::Shot);
  auto* name = dialog.findChild<QLineEdit*>(QStringLiteral("ControlsEditorName"));
  QCOMPARE(name->text(), kPlayerLabel);
  confirmYes();
  dialog.findChild<QPushButton*>(QStringLiteral("ControlsEditorReset"))->click();
  QCOMPARE(name->text(), QStringLiteral("Shot"));
  QCOMPARE(session.eventLabelOverrides().label(EventCatalog::EventId::Shot), kPlayerLabel);
  dialog.findChild<QPushButton*>(QStringLiteral("ControlsEditorSave"))->click();
  QVERIFY(!session.eventLabelOverrides().hasLabel(EventCatalog::EventId::Shot));

  GameControls controls;
  controls.setTagSession(&session);
  QPushButton* shotButton = buttonNamed(&controls, "Shot");
  QVERIFY(shotButton);
  QCOMPARE(titleText(shotButton), QStringLiteral("Shot"));
  QCOMPARE(shortcutText(shotButton), QStringLiteral("D"));
}

void GameControlsEditorTest::emptyNameBlocksSave() {
  TagSession session;
  QVERIFY(session.setEventLabel(EventCatalog::EventId::Shot, kPlayerLabel).accepted);
  GameControlsEditorDialog dialog(&session);
  dialog.show();
  selectEvent(&dialog, EventCatalog::EventId::Shot);
  auto* name = dialog.findChild<QLineEdit*>(QStringLiteral("ControlsEditorName"));
  name->clear();
  auto* error = dialog.findChild<QLabel*>(QStringLiteral("ControlsEditorNameError"));
  QVERIFY(error);
  QCOMPARE(error->text(), QStringLiteral("Enter a name."));
  QVERIFY(!dialog.findChild<QPushButton*>(QStringLiteral("ControlsEditorSave"))->isEnabled());
  dialog.findChild<QPushButton*>(QStringLiteral("ControlsEditorCancel"))->click();
  QCOMPARE(session.eventLabelOverrides().label(EventCatalog::EventId::Shot), kPlayerLabel);
}

void GameControlsEditorTest::contextMenuOffersExistingEditsOnly() {
  TagSession session;
  QMenu menu;
  GameControlsContextMenu::populate(&menu, &session, EventCatalog::EventId::Shot, []() {});
  QVERIFY(menu.findChild<QAction*>(QStringLiteral("ControlsMenuRename")));
  QVERIFY(menu.findChild<QAction*>(QStringLiteral("ControlsMenuChangeKey")));
  QVERIFY(menu.findChild<QAction*>(QStringLiteral("ControlsMenuVisibility")));
  QVERIFY(menu.findChild<QMenu*>(QStringLiteral("ControlsMenuFollowUps")));
  QVERIFY(menu.findChild<QAction*>(QStringLiteral("ControlsMenuReset")));
  QVERIFY(menu.findChild<QAction*>(QStringLiteral("ControlsMenuEditAll")));
  QVERIFY(menu.findChild<QAction*>(QStringLiteral("ControlsMenuFollowUpRename")));
  QVERIFY(menu.findChild<QAction*>(QStringLiteral("ControlsMenuFollowUpVisibility")));
  for (QAction* action : menu.findChildren<QAction*>()) {
    QVERIFY(!action->objectName().contains(QStringLiteral("Add")));
  }

  QMenu emptyFollowUps;
  GameControlsContextMenu::populate(&emptyFollowUps, &session, EventCatalog::EventId::FiftyYard,
                                    []() {});
  QVERIFY(emptyFollowUps.findChild<QMenu*>(QStringLiteral("ControlsMenuFollowUps")) == nullptr);
}

void GameControlsEditorTest::breakUiFixtures() {
  TagSession session;
  GameControlsEditorDialog dialog(&session);
  dialog.resize(900, 700);
  dialog.show();
  QVERIFY(dialog.findChild<QWidget*>(QStringLiteral("BreakUiToggle")) == nullptr);

  dialog.applyDataFixture(GameControlsEditorDialog::DataFixture::WorstCase);
  auto* banner = dialog.findChild<QLabel*>(QStringLiteral("ControlsEditorBanner"));
  QVERIFY(banner);
  QVERIFY(banner->isVisible());
  QVERIFY(!banner->text().isEmpty());
  QVERIFY(!dialog.findChild<QPushButton*>(QStringLiteral("ControlsEditorSave"))->isEnabled());
  QVERIFY(fullLabelOf(gridButton(&dialog, EventCatalog::EventId::Shot))
              .contains(QStringLiteral("Aleksandra")));
  const QString script = QStringLiteral("<script>alert(1)</script>");
  QLabel* scriptLabel = nullptr;
  for (QLabel* label : dialog.findChildren<QLabel*>()) {
    if (label->property("fullLabel").toString() == script) scriptLabel = label;
  }
  QVERIFY(scriptLabel);
  QCOMPARE(scriptLabel->property("fullLabel").toString(), script);
  QCOMPARE(scriptLabel->textFormat(), Qt::PlainText);

  dialog.applyDataFixture(GameControlsEditorDialog::DataFixture::EmptyFollowUps);
  QCOMPARE(dialog.findChild<QLineEdit*>(QStringLiteral("ControlsEditorName"))->text(),
           QStringLiteral("50-yd"));
  auto* empty = dialog.findChild<QLabel*>(QStringLiteral("ControlsEditorFollowUpEmpty"));
  QVERIFY(empty);
  QVERIFY(empty->isVisible());
  QCOMPARE(empty->text(), QStringLiteral("No follow-ups"));
  auto* tree = dialog.findChild<QTreeWidget*>(QStringLiteral("ControlsEditorFollowUps"));
  QVERIFY(tree);
  QVERIFY(!tree->isVisible());
  QCOMPARE(dialog.findChild<QLabel*>(QStringLiteral("ControlsEditorFollowUpCount"))->text(),
           QStringLiteral("No follow-ups"));

  dialog.applyDataFixture(GameControlsEditorDialog::DataFixture::OneFollowUp);
  QCOMPARE(dialog.findChild<QLabel*>(QStringLiteral("ControlsEditorFollowUpCount"))->text(),
           QStringLiteral("1 of 8 follow-ups shown"));
  QVERIFY(tree->isVisible());

  qputenv("AVA_BREAK_UI", "1");
  GameControlsEditorDialog toggled(&session);
  toggled.resize(900, 700);
  toggled.show();
  auto* toggle = toggled.findChild<QWidget*>(QStringLiteral("BreakUiToggle"));
  QVERIFY(toggle);
  QVERIFY(toggled.findChild<QPushButton*>(QStringLiteral("BreakUiWorst")));
  QVERIFY(toggled.findChild<QPushButton*>(QStringLiteral("BreakUiEmpty")));
  QVERIFY(toggled.findChild<QPushButton*>(QStringLiteral("BreakUiOne")));
  toggled.findChild<QPushButton*>(QStringLiteral("BreakUiDemo"))->click();
  QVERIFY(toggled.findChild<QPushButton*>(QStringLiteral("ControlsEditorSave"))->isEnabled());
}

void GameControlsEditorTest::spanishChrome() {
  AppLocale::setLanguage(AppLocale::Language::Spanish);
  TagSession session;
  GameControlsEditorDialog dialog(&session);
  QCOMPARE(dialog.windowTitle(), QStringLiteral("Controles del partido"));
  auto* title = dialog.findChild<QLabel*>(QStringLiteral("ControlsEditorTitle"));
  QVERIFY(title);
  QCOMPARE(title->text(), QStringLiteral("Controles del partido"));
  QCOMPARE(dialog.findChild<QPushButton*>(QStringLiteral("ControlsEditorSave"))->text(),
           QStringLiteral("Guardar"));
}

void GameControlsEditorTest::sharedGoalRenameOnPenaltyStrokeOnly() {
  TagSession session;
  GameControlsEditorDialog dialog(&session);
  dialog.show();
  selectEvent(&dialog, EventCatalog::EventId::PenaltyStroke);
  auto* tree = dialog.findChild<QTreeWidget*>(QStringLiteral("ControlsEditorFollowUps"));
  QVERIFY(tree);
  QTreeWidgetItem* goal = treeItem(tree, EventCatalog::EventId::FollowUpGoal);
  QVERIFY(goal);

  SharedPopupCapture capture;
  armSharedPopup("ControlsSharedApplyOne", &capture);
  goal->setText(0, QStringLiteral("Penal gol"));
  QVERIFY(capture.seen);
  QVERIFY(capture.applied);
  QCOMPARE(capture.lines.size(), 6);
  QVERIFY(capture.lines.contains(QStringLiteral("Shot, then On target, then Goal")));
  QVERIFY(capture.lines.contains(QStringLiteral("PC, then Variant, then Goal")));
  QVERIFY(!capture.lines.contains(QStringLiteral("PS, then Goal")));
  dialog.findChild<QPushButton*>(QStringLiteral("ControlsEditorSave"))->click();

  const EventLabelOverrides::FollowUpPath penalty =
      makePath(EventCatalog::EventId::PenaltyStroke, {EventCatalog::EventId::FollowUpGoal});
  const EventLabelOverrides::FollowUpPath shot = makePath(
      EventCatalog::EventId::Shot, {EventCatalog::EventId::OnTarget, EventCatalog::EventId::FollowUpGoal});
  const EventLabelOverrides::FollowUpPath corner = makePath(
      EventCatalog::EventId::PenaltyCorner,
      {EventCatalog::EventId::Variant, EventCatalog::EventId::FollowUpGoal});
  QCOMPARE(session.eventLabelOverrides().pathLabel(penalty), QStringLiteral("Penal gol"));
  QVERIFY(!session.eventLabelOverrides().hasLabel(EventCatalog::EventId::FollowUpGoal));
  QCOMPARE(EventLabelResolver::labelForPath(shot, &session), QStringLiteral("Goal"));
  QCOMPARE(EventLabelResolver::labelForPath(corner, &session), QStringLiteral("Goal"));
  QCOMPARE(EventCatalog::canonicalName(EventCatalog::EventId::FollowUpGoal), QStringLiteral("Goal"));

  TagEvent::MainEventKey penaltyKey;
  penaltyKey.id = EventCatalog::EventId::PenaltyStroke;
  TagEvent::FollowUpChain penaltyChain;
  penaltyChain.ids[0] = EventCatalog::EventId::FollowUpGoal;
  penaltyChain.count = 1;
  QCOMPARE(EventLabelResolver::tagLine(penaltyKey, penaltyChain, &session),
           QStringLiteral("PS") + AppLocale::kCompoundPathSeparator + QStringLiteral("Penal gol"));
  TagEvent::MainEventKey shotKey;
  shotKey.id = EventCatalog::EventId::Shot;
  TagEvent::FollowUpChain shotChain;
  shotChain.ids[0] = EventCatalog::EventId::OnTarget;
  shotChain.ids[1] = EventCatalog::EventId::FollowUpGoal;
  shotChain.count = 2;
  QCOMPARE(EventLabelResolver::tagLine(shotKey, shotChain, &session),
           QStringLiteral("Shot") + AppLocale::kCompoundPathSeparator + QStringLiteral("On target") +
               AppLocale::kCompoundPathSeparator + QStringLiteral("Goal"));

  GameControls controls;
  controls.setTagSession(&session);
  showTaggingGrid(&controls);
  buttonNamed(&controls, "PS")->click();
  QCOMPARE(titleText(followUpButton(&controls, "Goal")), QStringLiteral("Penal gol"));
  buttonNamed(&controls, "Shot")->click();
  followUpButton(&controls, "On target")->click();
  QCOMPARE(titleText(followUpButton(&controls, "Goal")), QStringLiteral("Goal"));
}

void GameControlsEditorTest::sharedGoalRenameAppliesToEveryUse() {
  TagSession session;
  QMenu menu;
  GameControlsContextMenu::populate(&menu, &session, EventCatalog::EventId::PenaltyStroke, []() {});
  QMenu* goalMenu = menuForEvent(&menu, EventCatalog::EventId::FollowUpGoal);
  QVERIFY(goalMenu);
  auto* rename = goalMenu->findChild<QAction*>(QStringLiteral("ControlsMenuFollowUpRename"));
  QVERIFY(rename);

  SharedPopupCapture capture;
  QTimer::singleShot(0, [&capture]() {
    QWidget* modal = QApplication::activeModalWidget();
    auto* field = modal == nullptr ? nullptr
                                   : modal->findChild<QLineEdit*>(QStringLiteral("ControlsRenameField"));
    auto* ok = modal == nullptr ? nullptr
                                : modal->findChild<QPushButton*>(QStringLiteral("ControlsRenameOk"));
    if (field == nullptr || ok == nullptr) {
      if (auto* dialog = qobject_cast<QDialog*>(modal)) dialog->reject();
      return;
    }
    field->setText(QStringLiteral("Penal gol"));
    armSharedPopup("ControlsSharedApplyAll", &capture);
    ok->click();
  });
  rename->trigger();
  QVERIFY(capture.seen);
  QVERIFY(capture.applied);
  QVERIFY(capture.lines.contains(QStringLiteral("Shot, then On target, then Goal")));
  QVERIFY(capture.lines.contains(QStringLiteral("PC, then Direct shot, then Hit, then Goal")));
  QCOMPARE(session.eventLabelOverrides().label(EventCatalog::EventId::FollowUpGoal),
           QStringLiteral("Penal gol"));
  QVERIFY(session.eventLabelOverrides().pathRecords().isEmpty());

  const EventLabelOverrides::FollowUpPath penalty =
      makePath(EventCatalog::EventId::PenaltyStroke, {EventCatalog::EventId::FollowUpGoal});
  const EventLabelOverrides::FollowUpPath shot = makePath(
      EventCatalog::EventId::Shot, {EventCatalog::EventId::OnTarget, EventCatalog::EventId::FollowUpGoal});
  const EventLabelOverrides::FollowUpPath corner = makePath(
      EventCatalog::EventId::PenaltyCorner,
      {EventCatalog::EventId::DirectShot, EventCatalog::EventId::DirectShotHit,
       EventCatalog::EventId::FollowUpGoal});
  QCOMPARE(EventLabelResolver::labelForPath(penalty, &session), QStringLiteral("Penal gol"));
  QCOMPARE(EventLabelResolver::labelForPath(shot, &session), QStringLiteral("Penal gol"));
  QCOMPARE(EventLabelResolver::labelForPath(corner, &session), QStringLiteral("Penal gol"));
  QCOMPARE(EventCatalog::canonicalName(EventCatalog::EventId::FollowUpGoal), QStringLiteral("Goal"));
}

void GameControlsEditorTest::hidingGoalOnPenaltyStrokeKeepsShotTaggable() {
  TagSession session;
  GameControlsEditorDialog dialog(&session);
  dialog.show();
  selectEvent(&dialog, EventCatalog::EventId::PenaltyStroke);
  auto* tree = dialog.findChild<QTreeWidget*>(QStringLiteral("ControlsEditorFollowUps"));
  QVERIFY(tree);
  QTreeWidgetItem* goal = treeItem(tree, EventCatalog::EventId::FollowUpGoal);
  QVERIFY(goal);
  SharedPopupCapture capture;
  armSharedPopup("ControlsSharedApplyOne", &capture);
  goal->setCheckState(1, Qt::Unchecked);
  QVERIFY(capture.seen);
  QVERIFY(capture.applied);
  dialog.findChild<QPushButton*>(QStringLiteral("ControlsEditorSave"))->click();

  const EventLabelOverrides::FollowUpPath penalty =
      makePath(EventCatalog::EventId::PenaltyStroke, {EventCatalog::EventId::FollowUpGoal});
  const EventLabelOverrides::FollowUpPath shot = makePath(
      EventCatalog::EventId::Shot, {EventCatalog::EventId::OnTarget, EventCatalog::EventId::FollowUpGoal});
  QVERIFY(session.eventLabelOverrides().hiddenOnPath(penalty));
  QVERIFY(!session.eventLabelOverrides().hiddenOnPath(shot));
  QVERIFY(!session.eventLabelOverrides().hidden(EventCatalog::EventId::FollowUpGoal));

  GameControls controls;
  controls.setTagSession(&session);
  showTaggingGrid(&controls);
  buttonNamed(&controls, "PS")->click();
  QVERIFY(followUpButton(&controls, "Goal") == nullptr);
  QVERIFY(followUpButton(&controls, "No Goal"));
  buttonNamed(&controls, "Shot")->click();
  QPushButton* onTarget = followUpButton(&controls, "On target");
  QVERIFY(onTarget);
  onTarget->click();
  QPushButton* shotGoal = followUpButton(&controls, "Goal");
  QVERIFY(shotGoal);
  QSignalSpy committed(&controls, &GameControls::tagCommitted);
  shotGoal->click();
  QCOMPARE(committed.count(), 1);
  QCOMPARE(committed.at(0).at(0).toString(), QStringLiteral("Shot"));
  QVERIFY(committed.at(0).at(1).toString().contains(QStringLiteral("Goal")));
  QCOMPARE(EventCatalog::canonicalName(EventCatalog::EventId::FollowUpGoal), QStringLiteral("Goal"));
}

void GameControlsEditorTest::circleEntryLeftRenameSkipsThePopup() {
  TagSession session;
  GameControlsEditorDialog dialog(&session);
  dialog.show();
  selectEvent(&dialog, EventCatalog::EventId::CircleEntry);
  auto* tree = dialog.findChild<QTreeWidget*>(QStringLiteral("ControlsEditorFollowUps"));
  QVERIFY(tree);
  const QList<QTreeWidgetItem*> leftItems = treeItems(tree, EventCatalog::EventId::Left);
  QCOMPARE(leftItems.size(), 3);
  bool sawShared = false;
  QTimer::singleShot(0, [&sawShared]() {
    QWidget* modal = QApplication::activeModalWidget();
    if (modal != nullptr && modal->objectName() == QLatin1String("ControlsSharedPaths")) {
      sawShared = true;
      if (auto* popup = qobject_cast<QDialog*>(modal)) popup->reject();
    }
  });
  leftItems.first()->setText(0, QStringLiteral("Por la izquierda"));
  QApplication::processEvents();
  QVERIFY(!sawShared);
  for (QTreeWidgetItem* item : treeItems(tree, EventCatalog::EventId::Left)) {
    QCOMPARE(item->text(0), QStringLiteral("Por la izquierda"));
  }
  dialog.findChild<QPushButton*>(QStringLiteral("ControlsEditorSave"))->click();
  QCOMPARE(session.eventLabelOverrides().label(EventCatalog::EventId::Left),
           QStringLiteral("Por la izquierda"));
  QVERIFY(session.eventLabelOverrides().pathRecords().isEmpty());

  TagSession menuSession;
  QMenu menu;
  GameControlsContextMenu::populate(&menu, &menuSession, EventCatalog::EventId::CircleEntry, []() {});
  QMenu* dribbling = menuForEvent(&menu, EventCatalog::EventId::Dribbling);
  QVERIFY(dribbling);
  QMenu* leftMenu = nullptr;
  for (QAction* action : dribbling->actions()) {
    if (action->menu() != nullptr &&
        action->menu()->property("eventId").toInt() == static_cast<int>(EventCatalog::EventId::Left)) {
      leftMenu = action->menu();
    }
  }
  QVERIFY(leftMenu);
  auto* rename = leftMenu->findChild<QAction*>(QStringLiteral("ControlsMenuFollowUpRename"));
  QVERIFY(rename);
  bool menuSawShared = false;
  QTimer::singleShot(0, [&menuSawShared]() {
    QWidget* modal = QApplication::activeModalWidget();
    auto* field = modal == nullptr ? nullptr
                                   : modal->findChild<QLineEdit*>(QStringLiteral("ControlsRenameField"));
    auto* ok = modal == nullptr ? nullptr
                                : modal->findChild<QPushButton*>(QStringLiteral("ControlsRenameOk"));
    if (field == nullptr || ok == nullptr) {
      if (auto* dialog = qobject_cast<QDialog*>(modal)) dialog->reject();
      return;
    }
    field->setText(QStringLiteral("Por la izquierda"));
    QTimer::singleShot(0, [&menuSawShared]() {
      QWidget* top = QApplication::activeModalWidget();
      if (top != nullptr && top->objectName() == QLatin1String("ControlsSharedPaths")) {
        menuSawShared = true;
        if (auto* popup = qobject_cast<QDialog*>(top)) popup->reject();
      }
    });
    ok->click();
  });
  rename->trigger();
  QVERIFY(!menuSawShared);
  QCOMPARE(menuSession.eventLabelOverrides().label(EventCatalog::EventId::Left),
           QStringLiteral("Por la izquierda"));
  QCOMPARE(EventLabelResolver::labelForId(EventCatalog::EventId::Left, &menuSession),
           QStringLiteral("Por la izquierda"));
}

void GameControlsEditorTest::oldSessionAndTemplateFilesLoad() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString sessionPath = QDir(directory.path()).filePath(QStringLiteral("old-session.json"));
  const QByteArray sessionBytes = QByteArrayLiteral(
      "{\"format\":\"ava-match-session\",\"version\":1,\"tags\":[],"
      "\"labelOverrides\":[{\"eventId\":2,\"label\":\"Jugadora 7\"}]}\n");
  QVERIFY(writeBytes(sessionPath, sessionBytes));
  const MatchSessionFile::LoadResult loaded = MatchSessionFile::loadFile(sessionPath);
  QCOMPARE(loaded.status, MatchSessionFile::LoadStatus::Loaded);
  QCOMPARE(loaded.document.controls.labelOverrides.label(EventCatalog::EventId::Shot),
           QStringLiteral("Jugadora 7"));
  QVERIFY(loaded.document.controls.labelOverrides.pathRecords().isEmpty());

  const QString templatePath = QDir(directory.path()).filePath(QStringLiteral("control-templates.json"));
  const QByteArray templateBytes = QByteArrayLiteral(
      "{\"format\":\"ava-control-templates\",\"version\":1,\"templates\":["
      "{\"name\":\"Old\",\"labelOverrides\":[{\"eventId\":2,\"label\":\"Jugadora 7\"}]}]}\n");
  QVERIFY(writeBytes(templatePath, templateBytes));
  const EventControlsTemplateStore::LoadResult oldTemplate =
      EventControlsTemplateStore::loadTemplate(QStringLiteral("Old"), directory.path());
  QCOMPARE(oldTemplate.status, EventControlsTemplateStore::LoadStatus::Loaded);
  QCOMPARE(oldTemplate.setup.labelOverrides.label(EventCatalog::EventId::Shot),
           QStringLiteral("Jugadora 7"));
  QVERIFY(oldTemplate.setup.labelOverrides.pathRecords().isEmpty());

  TagSession session;
  EventLabelOverrides::Table table;
  const EventLabelOverrides::FollowUpPath penalty =
      makePath(EventCatalog::EventId::PenaltyStroke, {EventCatalog::EventId::FollowUpGoal});
  QVERIFY(table.setPathLabel(penalty, QStringLiteral("Penal gol")).accepted);
  QVERIFY(session.replaceEventLabelOverrides(table));
  const QString roundTripPath = QDir(directory.path()).filePath(QStringLiteral("round-trip.json"));
  const MatchSessionFile::Document captured =
      MatchSessionFile::capture(session, nullptr, nullptr, {});
  QCOMPARE(MatchSessionFile::saveFile(roundTripPath, captured, MatchSessionFile::ExistingFileGuard::Replace)
               .status,
           MatchSessionFile::SaveStatus::Saved);
  const QByteArray roundTripBytes = QFile(roundTripPath).readAll(); // file not open
  QFile roundTripFile(roundTripPath);
  QVERIFY(roundTripFile.open(QIODevice::ReadOnly));
  const QByteArray savedBytes = roundTripFile.readAll();
  QVERIFY(savedBytes.contains("\"version\": 2"));
  QVERIFY(savedBytes.contains("\"pathOverrides\""));
  QVERIFY(savedBytes.contains("\"mainEventId\": 13"));
  const MatchSessionFile::LoadResult restored = MatchSessionFile::loadFile(roundTripPath);
  QCOMPARE(restored.status, MatchSessionFile::LoadStatus::Loaded);
  QCOMPARE(restored.document.controls.labelOverrides.pathLabel(penalty), QStringLiteral("Penal gol"));
  QVERIFY(!restored.document.controls.labelOverrides.hasLabel(EventCatalog::EventId::FollowUpGoal));
  QCOMPARE(EventCatalog::canonicalName(EventCatalog::EventId::FollowUpGoal), QStringLiteral("Goal"));
  Q_UNUSED(roundTripBytes);
}

void GameControlsEditorTest::sharedPathListBreakUi() {
  qputenv("AVA_BREAK_UI", QByteArrayLiteral("1"));
  TagSession session;
  GameControlsEditorDialog dialog(&session);
  dialog.show();
  selectEvent(&dialog, EventCatalog::EventId::PenaltyStroke);
  auto* tree = dialog.findChild<QTreeWidget*>(QStringLiteral("ControlsEditorFollowUps"));
  QVERIFY(tree);
  QTreeWidgetItem* goal = treeItem(tree, EventCatalog::EventId::FollowUpGoal);
  QVERIFY(goal);

  bool sawLong = false;
  bool sawScript = false;
  bool sawPlainAmp = false;
  int emptyCount = -1;
  int oneCount = -1;
  bool emptyVisible = false;
  bool buttonsRemain = false;
  QTimer::singleShot(0, [&]() {
    auto* popup = qobject_cast<QDialog*>(QApplication::activeModalWidget());
    if (popup == nullptr || popup->objectName() != QLatin1String("ControlsSharedPaths")) {
      if (auto* other = qobject_cast<QDialog*>(QApplication::activeModalWidget())) other->reject();
      return;
    }
    auto* worst = popup->findChild<QPushButton*>(QStringLiteral("ControlsSharedBreakWorst"));
    auto* empty = popup->findChild<QPushButton*>(QStringLiteral("ControlsSharedBreakEmpty"));
    auto* one = popup->findChild<QPushButton*>(QStringLiteral("ControlsSharedBreakOne"));
    auto* list = popup->findChild<QListWidget*>(QStringLiteral("ControlsSharedList"));
    auto* cancel = popup->findChild<QPushButton*>(QStringLiteral("ControlsSharedCancel"));
    auto* applyAll = popup->findChild<QPushButton*>(QStringLiteral("ControlsSharedApplyAll"));
    if (worst == nullptr || empty == nullptr || one == nullptr || list == nullptr || cancel == nullptr ||
        applyAll == nullptr) {
      popup->reject();
      return;
    }
    worst->click();
    QString joined;
    for (int index = 0; index < list->count(); ++index) {
      if (list->item(index) != nullptr) joined += list->item(index)->text();
    }
    sawLong = joined.contains(QStringLiteral("Aleksandra Wiśniewska-Kowalczyk"));
    sawScript = joined.contains(QStringLiteral("<script>alert(1)</script>"));
    sawPlainAmp = joined.contains(QStringLiteral("&amp;"));
    empty->click();
    emptyCount = list->count();
    auto* emptyLabel = popup->findChild<QLabel*>(QStringLiteral("ControlsSharedEmpty"));
    emptyVisible = emptyLabel != nullptr && emptyLabel->isVisible();
    one->click();
    oneCount = list->count();
    buttonsRemain = applyAll->isEnabled() && cancel->isEnabled();
    cancel->click();
  });
  goal->setText(0, QStringLiteral("Penal gol"));
  QVERIFY(sawLong);
  QVERIFY(sawScript);
  QVERIFY(sawPlainAmp);
  QCOMPARE(emptyCount, 0);
  QVERIFY(emptyVisible);
  QCOMPARE(oneCount, 1);
  QVERIFY(buttonsRemain);
  QVERIFY(!session.eventLabelOverrides().hasLabel(EventCatalog::EventId::FollowUpGoal));
  QVERIFY(session.eventLabelOverrides().pathRecords().isEmpty());
}

int runGameControlsEditorTests(int argc, char** argv) {
  GameControlsEditorTest tests;
  return QTest::qExec(&tests, argc, argv);
}

#include "GameControlsEditorTests.moc"
