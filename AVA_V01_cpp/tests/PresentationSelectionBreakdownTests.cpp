#include "AppLocale.h"
#include "EventLabelOverrides.h"
#include "PresentationPanel.h"
#include "PresentationSelectionBreakdown.h"
#include "TagSession.h"

#include <QApplication>
#include <QComboBox>
#include <QEvent>
#include <QFile>
#include <QLabel>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTableView>
#include <QTest>
#include <QToolButton>

#include <algorithm>

namespace {

TagSession::GameTag playTag(const QString& mainEvent, const QString& team, qint64 markMs) {
  TagSession::GameTag tag;
  tag.setMainEventKey(TagEvent::keyFromCanonicalName(mainEvent));
  tag.team = team;
  tag.markMs = markMs;
  tag.startMs = markMs;
  tag.endMs = markMs + 4000;
  return tag;
}

bool applyTheme() {
  QFile file(QStringLiteral(AVA_THEME_QSS_PATH));
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
  qApp->setStyleSheet(QString::fromUtf8(file.readAll()));
  return true;
}

void showPanel(PresentationPanel& panel) {
  panel.setFixedSize(260, 720);
  panel.show();
  QApplication::processEvents();
}

QLabel* summaryLabel(PresentationPanel& panel) {
  return panel.findChild<QLabel*>(QStringLiteral("PresentationSelectionSummary"));
}

bool checkEveryRow(PresentationPanel& panel) {
  auto* table = panel.findChild<QTableView*>(QStringLiteral("PresentationInstancesTable"));
  if (!table || !table->model()) return false;
  for (int row = 0; row < table->model()->rowCount(); ++row) {
    if (!table->model()->setData(table->model()->index(row, 0), Qt::Checked, Qt::CheckStateRole)) {
      return false;
    }
  }
  return true;
}

void openBreakdown(QLabel* summary) {
  if (!summary) return;
  QEvent enter(QEvent::Enter);
  QApplication::sendEvent(summary, &enter);
  QApplication::processEvents();
}

QWidget* breakdownPopup(PresentationPanel& panel) {
  return panel.findChild<QWidget*>(QStringLiteral("PresentationSelectionPopup"));
}

QList<QWidget*> eventRows(QWidget* popup) {
  QList<QWidget*> rows = popup->findChildren<QWidget*>(QStringLiteral("PresentationSelectionEventRow"));
  std::sort(rows.begin(), rows.end(), [](const QWidget* left, const QWidget* right) {
    return left->mapTo(left->window(), QPoint(0, 0)).y() < right->mapTo(right->window(), QPoint(0, 0)).y();
  });
  return rows;
}

QWidget* rowWithLabel(const QList<QWidget*>& rows, const QString& label) {
  for (QWidget* row : rows) {
    if (row->property("displayLabel").toString() == label) return row;
  }
  return nullptr;
}

struct LanguageGuard {
  AppLocale::Language previous = AppLocale::currentLanguage();
  ~LanguageGuard() { AppLocale::setLanguage(previous); }
};

}  // namespace

class PresentationSelectionBreakdownTest : public QObject {
  Q_OBJECT

 private slots:
  void init();
  void cleanup();
  void catalogOrderIsGoalThenShotThenPenaltyCorner();
  void zeroCountsAndTimeControlsAreOmitted();
  void duplicateKeysMergeAndBlankLabelsUseTheCanonicalName();
  void oneEventOnTheCurrentListIsPlain();
  void oneEventOutsideTheFilterIsInteractive();
  void labelOverrideKeepsTheCatalogId();
  void worstCaseDropsTheZeroAndKeepsLongNames();
  void hoverListsEventsAndClickSetsTheFilter();
  void oneEventAlreadyListedDoesNotOpen();
  void worstCasePopupFitsTheSideColumn();
  void oneAndEmptyFixtures();
  void mapToggleDoesNotChangeTheCheck();

 private:
  AppLocale::Language previousLanguage_ = AppLocale::Language::English;
};

void PresentationSelectionBreakdownTest::init() {
  previousLanguage_ = AppLocale::currentLanguage();
  QVERIFY(applyTheme());
}

void PresentationSelectionBreakdownTest::cleanup() {
  qunsetenv("AVA_BREAK_UI");
  AppLocale::setLanguage(previousLanguage_);
}

void PresentationSelectionBreakdownTest::catalogOrderIsGoalThenShotThenPenaltyCorner() {
  const QVector<TagEvent::MainEventKey> keys = {
      TagEvent::catalogKey(EventCatalog::EventId::PenaltyCorner),
      TagEvent::catalogKey(EventCatalog::EventId::Shot),
      TagEvent::catalogKey(EventCatalog::EventId::Goal),
      TagEvent::catalogKey(EventCatalog::EventId::Shot),
      TagEvent::unlistedKey(QStringLiteral("zeta")),
      TagEvent::unlistedKey(QStringLiteral("Alpha")),
  };

  const PresentationSelectionBreakdown::Result result =
      PresentationSelectionBreakdown::fromSelectedKeys(keys, {}, true, nullptr);

  QVERIFY(result.interactive);
  QCOMPARE(result.events.size(), 5);
  QCOMPARE(result.events.at(0).eventKey.id, EventCatalog::EventId::Goal);
  QCOMPARE(result.events.at(0).selectedCount, 1);
  QCOMPARE(result.events.at(0).displayLabel, QStringLiteral("Goal"));
  QCOMPARE(result.events.at(1).eventKey.id, EventCatalog::EventId::Shot);
  QCOMPARE(result.events.at(1).selectedCount, 2);
  QCOMPARE(result.events.at(2).eventKey.id, EventCatalog::EventId::PenaltyCorner);
  QCOMPARE(result.events.at(3).eventKey, TagEvent::unlistedKey(QStringLiteral("Alpha")));
  QCOMPARE(result.events.at(4).eventKey, TagEvent::unlistedKey(QStringLiteral("zeta")));
}

void PresentationSelectionBreakdownTest::zeroCountsAndTimeControlsAreOmitted() {
  PresentationSelectionBreakdown::EventCount zero;
  zero.eventKey = TagEvent::catalogKey(EventCatalog::EventId::Goal);
  zero.displayLabel = QStringLiteral("Goal");
  zero.selectedCount = 0;
  PresentationSelectionBreakdown::EventCount shot;
  shot.eventKey = TagEvent::catalogKey(EventCatalog::EventId::Shot);
  shot.displayLabel = QStringLiteral("Shot");
  shot.selectedCount = 1;

  const QVector<PresentationSelectionBreakdown::EventCount> ordered =
      PresentationSelectionBreakdown::inCatalogOrder({zero, shot});
  QCOMPARE(ordered.size(), 1);
  QCOMPARE(ordered.at(0).eventKey.id, EventCatalog::EventId::Shot);

  const QVector<TagEvent::MainEventKey> keys = {
      TagEvent::catalogKey(EventCatalog::EventId::Shot),
      TagEvent::unlistedKey(QStringLiteral("Q1")),
      TagEvent::unlistedKey(QStringLiteral("Inicio")),
      TagEvent::catalogKey(EventCatalog::EventId::Shot),
  };
  const PresentationSelectionBreakdown::Result result =
      PresentationSelectionBreakdown::fromSelectedKeys(keys, TagEvent::catalogKey(EventCatalog::EventId::Shot),
                                                      false, nullptr);
  QCOMPARE(result.events.size(), 1);
  QCOMPARE(result.events.at(0).selectedCount, 2);
  QVERIFY(!result.interactive);
}

void PresentationSelectionBreakdownTest::duplicateKeysMergeAndBlankLabelsUseTheCanonicalName() {
  PresentationSelectionBreakdown::EventCount firstShot;
  firstShot.eventKey = TagEvent::catalogKey(EventCatalog::EventId::Shot);
  firstShot.displayLabel = QStringLiteral("Tiro");
  firstShot.selectedCount = 1;
  PresentationSelectionBreakdown::EventCount goal;
  goal.eventKey = TagEvent::catalogKey(EventCatalog::EventId::Goal);
  goal.displayLabel = QStringLiteral("Gol");
  goal.selectedCount = 2;
  PresentationSelectionBreakdown::EventCount secondShot = firstShot;
  secondShot.selectedCount = 4;
  PresentationSelectionBreakdown::EventCount blank;
  blank.eventKey = TagEvent::unlistedKey(QStringLiteral("Untitled import"));
  blank.displayLabel = QStringLiteral("   ");
  blank.selectedCount = 2;

  const QVector<PresentationSelectionBreakdown::EventCount> ordered =
      PresentationSelectionBreakdown::inCatalogOrder({firstShot, goal, secondShot, blank});
  QCOMPARE(ordered.size(), 3);
  QCOMPARE(ordered.at(0).eventKey.id, EventCatalog::EventId::Goal);
  QCOMPARE(ordered.at(0).selectedCount, 2);
  QCOMPARE(ordered.at(1).eventKey.id, EventCatalog::EventId::Shot);
  QCOMPARE(ordered.at(1).selectedCount, 5);
  QCOMPARE(ordered.at(1).displayLabel, QStringLiteral("Tiro"));
  QCOMPARE(ordered.at(2).displayLabel, QStringLiteral("Untitled import"));
  QCOMPARE(ordered.at(2).selectedCount, 2);
}

void PresentationSelectionBreakdownTest::oneEventOnTheCurrentListIsPlain() {
  const QVector<TagEvent::MainEventKey> shots = {
      TagEvent::catalogKey(EventCatalog::EventId::Shot),
      TagEvent::catalogKey(EventCatalog::EventId::Shot),
  };
  const PresentationSelectionBreakdown::Result onThatEvent =
      PresentationSelectionBreakdown::fromSelectedKeys(shots, TagEvent::catalogKey(EventCatalog::EventId::Shot),
                                                      false, nullptr);
  QVERIFY(!onThatEvent.interactive);

  const PresentationSelectionBreakdown::Result onAllEvents =
      PresentationSelectionBreakdown::fromSelectedKeys(shots, {}, true, nullptr);
  QVERIFY(!onAllEvents.interactive);

  const PresentationSelectionBreakdown::Result none =
      PresentationSelectionBreakdown::fromSelectedKeys({}, {}, true, nullptr);
  QVERIFY(!none.interactive);
  QCOMPARE(none.events.size(), 0);
}

void PresentationSelectionBreakdownTest::oneEventOutsideTheFilterIsInteractive() {
  const QVector<TagEvent::MainEventKey> shots = {
      TagEvent::catalogKey(EventCatalog::EventId::Shot),
      TagEvent::catalogKey(EventCatalog::EventId::Shot),
      TagEvent::catalogKey(EventCatalog::EventId::Shot),
  };
  const PresentationSelectionBreakdown::Result result =
      PresentationSelectionBreakdown::fromSelectedKeys(shots, TagEvent::catalogKey(EventCatalog::EventId::Goal),
                                                      false, nullptr);
  QVERIFY(result.interactive);
  QCOMPARE(result.events.size(), 1);
  QCOMPARE(result.events.at(0).eventKey.id, EventCatalog::EventId::Shot);
  QCOMPARE(result.events.at(0).selectedCount, 3);
  QCOMPARE(result.events.at(0).displayLabel, QStringLiteral("Shot"));
}

void PresentationSelectionBreakdownTest::labelOverrideKeepsTheCatalogId() {
  LanguageGuard languageGuard;
  TagSession session;
  const EventLabelOverrides::Result renamed =
      session.setEventLabel(EventCatalog::EventId::Shot, QStringLiteral("Jugadora 7"));
  QVERIFY(renamed.accepted);

  const QVector<TagEvent::MainEventKey> keys = {
      TagEvent::catalogKey(EventCatalog::EventId::Shot),
      TagEvent::catalogKey(EventCatalog::EventId::Goal),
      TagEvent::catalogKey(EventCatalog::EventId::Shot),
      TagEvent::catalogKey(EventCatalog::EventId::Special),
  };
  const PresentationSelectionBreakdown::Result english =
      PresentationSelectionBreakdown::fromSelectedKeys(keys, {}, true, &session);
  QCOMPARE(english.events.size(), 3);
  QCOMPARE(english.events.at(0).eventKey.id, EventCatalog::EventId::Goal);
  QCOMPARE(english.events.at(0).displayLabel, QStringLiteral("Goal"));
  QCOMPARE(english.events.at(0).selectedCount, 1);
  QCOMPARE(english.events.at(1).eventKey.id, EventCatalog::EventId::Shot);
  QCOMPARE(english.events.at(1).displayLabel, QStringLiteral("Jugadora 7"));
  QCOMPARE(english.events.at(1).selectedCount, 2);
  QCOMPARE(english.events.at(2).eventKey.id, EventCatalog::EventId::Special);
  QCOMPARE(english.events.at(2).displayLabel, QStringLiteral("☆"));

  AppLocale::setLanguage(AppLocale::Language::Spanish);
  const PresentationSelectionBreakdown::Result spanish =
      PresentationSelectionBreakdown::fromSelectedKeys(
          {TagEvent::catalogKey(EventCatalog::EventId::Shot),
           TagEvent::catalogKey(EventCatalog::EventId::Goal)},
          {}, true, nullptr);
  QCOMPARE(spanish.events.at(0).displayLabel, QStringLiteral("Gol"));
  QCOMPARE(spanish.events.at(1).displayLabel, QStringLiteral("Tiro"));
}

void PresentationSelectionBreakdownTest::worstCaseDropsTheZeroAndKeepsLongNames() {
  const PresentationSelectionBreakdown::Result worst = PresentationSelectionBreakdown::worstCaseResult();
  QVERIFY(worst.interactive);
  QVERIFY(worst.events.size() >= 14);

  bool sawOne = false;
  bool sawLarge = false;
  bool sawScript = false;
  bool sawShort = false;
  EventCatalog::EventId previousId = EventCatalog::EventId::None;
  bool seenUnlisted = false;
  for (const PresentationSelectionBreakdown::EventCount& event : worst.events) {
    QVERIFY(event.selectedCount >= 1);
    QVERIFY(!event.displayLabel.trimmed().isEmpty());
    if (event.selectedCount == 1) sawOne = true;
    if (event.selectedCount == 1284) sawLarge = true;
    if (event.displayLabel == QStringLiteral("<script>alert(1)</script>")) sawScript = true;
    if (event.displayLabel == QStringLiteral("J")) sawShort = true;
    if (event.eventKey.id == EventCatalog::EventId::None) {
      seenUnlisted = true;
    } else {
      QVERIFY(!seenUnlisted);
      QVERIFY(static_cast<int>(previousId) < static_cast<int>(event.eventKey.id) ||
              previousId == EventCatalog::EventId::None);
      previousId = event.eventKey.id;
    }
    QVERIFY(event.eventKey.id != EventCatalog::EventId::Goal);
  }
  QVERIFY(sawOne);
  QVERIFY(sawLarge);
  QVERIFY(sawScript);
  QVERIFY(sawShort);
  QCOMPARE(worst.events.first().eventKey.id, EventCatalog::EventId::Shot);
  QCOMPARE(worst.events.at(worst.events.size() - 3).displayLabel,
           QStringLiteral("bartholomew.fitzgerald@northwind-industries-holdings.example.com"));
  QCOMPARE(worst.events.at(worst.events.size() - 2).displayLabel,
           QStringLiteral("customer-feedback-from-enterprise-onboarding"));
  QCOMPARE(worst.events.last().displayLabel, QStringLiteral("Untitled import"));

  const PresentationSelectionBreakdown::Result one = PresentationSelectionBreakdown::oneEventResult();
  QVERIFY(one.interactive);
  QCOMPARE(one.events.size(), 1);
  QCOMPARE(one.events.at(0).displayLabel, QStringLiteral("J"));
  QCOMPARE(one.events.at(0).selectedCount, 1);
}

void PresentationSelectionBreakdownTest::hoverListsEventsAndClickSetsTheFilter() {
  TagSession session;
  session.addTag(playTag(QStringLiteral("Shot"), QStringLiteral("Home"), 1000));
  session.addTag(playTag(QStringLiteral("Shot"), QStringLiteral("Home"), 2000));
  session.addTag(playTag(QStringLiteral("Goal"), QStringLiteral("Away"), 3000));
  session.addTag(playTag(QStringLiteral("Pass"), QStringLiteral("Home"), 4000));
  session.addTag(playTag(QStringLiteral("Alpha"), QStringLiteral("Away"), 5000));
  QVERIFY(session.setEventLabel(EventCatalog::EventId::Shot, QStringLiteral("Jugadora 7")).accepted);

  PresentationPanel panel;
  panel.setTagSession(&session);
  showPanel(panel);
  QVERIFY(checkEveryRow(panel));
  QCOMPARE(panel.selectedTagIndexes().size(), 5);

  QLabel* summary = summaryLabel(panel);
  QVERIFY(summary);
  QCOMPARE(summary->property("interactive").toBool(), true);
  QCOMPARE(summary->cursor().shape(), Qt::PointingHandCursor);
  summary->ensurePolished();
  QVERIFY2(summary->font().underline(), "interactive summary has no underline");

  openBreakdown(summary);
  QWidget* popup = breakdownPopup(panel);
  QVERIFY(popup);
  QVERIFY(popup->isVisible());
  QCOMPARE(popup->width(), 260);

  const QList<QWidget*> rows = eventRows(popup);
  QCOMPARE(rows.size(), 4);
  QCOMPARE(rows.at(0)->property("displayLabel").toString(), QStringLiteral("Goal"));
  QCOMPARE(rows.at(0)->property("selectedCount").toInt(), 1);
  QCOMPARE(rows.at(1)->property("displayLabel").toString(), QStringLiteral("Jugadora 7"));
  QCOMPARE(rows.at(1)->property("selectedCount").toInt(), 2);
  QCOMPARE(rows.at(2)->property("displayLabel").toString(), QStringLiteral("Pass"));
  QCOMPARE(rows.at(3)->property("displayLabel").toString(), QStringLiteral("Alpha"));
  QCOMPARE(rows.at(3)->property("eventId").toInt(), static_cast<int>(EventCatalog::EventId::None));

  const QVector<int> selectionBefore = panel.selectedTagIndexes();
  QWidget* goalRow = rowWithLabel(rows, QStringLiteral("Goal"));
  QVERIFY(goalRow);
  QVERIFY(goalRow->height() >= 32);
  QTest::mouseClick(goalRow, Qt::LeftButton);
  QApplication::processEvents();

  QCOMPARE(panel.selectedTagIndexes(), selectionBefore);
  auto* eventFilter = panel.findChild<QComboBox*>(QStringLiteral("PresentationEventFilter"));
  QVERIFY(eventFilter);
  QCOMPARE(eventFilter->currentData().toInt(), static_cast<int>(EventCatalog::EventId::Goal));
  QVERIFY(!popup->isVisible());

  QCOMPARE(summary->property("interactive").toBool(), true);
  openBreakdown(summary);
  popup = breakdownPopup(panel);
  QVERIFY(popup && popup->isVisible());
  QWidget* alphaRow = rowWithLabel(eventRows(popup), QStringLiteral("Alpha"));
  QVERIFY(alphaRow);
  QTest::mouseClick(alphaRow, Qt::LeftButton);
  QApplication::processEvents();
  QCOMPARE(panel.selectedTagIndexes(), selectionBefore);
  QCOMPARE(eventFilter->currentData().toString(), QStringLiteral("Alpha"));
}

void PresentationSelectionBreakdownTest::oneEventAlreadyListedDoesNotOpen() {
  TagSession session;
  session.addTag(playTag(QStringLiteral("Shot"), QStringLiteral("Home"), 1000));
  session.addTag(playTag(QStringLiteral("Shot"), QStringLiteral("Away"), 2000));
  session.addTag(playTag(QStringLiteral("Goal"), QStringLiteral("Home"), 3000));

  PresentationPanel panel;
  panel.setTagSession(&session);
  showPanel(panel);

  auto* table = panel.findChild<QTableView*>(QStringLiteral("PresentationInstancesTable"));
  QVERIFY(table);
  for (int row = 0; row < table->model()->rowCount(); ++row) {
    const QString event = table->model()->data(table->model()->index(row, 2)).toString();
    if (!event.contains(QStringLiteral("Shot"))) continue;
    QVERIFY(table->model()->setData(table->model()->index(row, 0), Qt::Checked, Qt::CheckStateRole));
  }
  QCOMPARE(panel.selectedTagIndexes().size(), 2);

  QLabel* summary = summaryLabel(panel);
  QVERIFY(summary);
  QCOMPARE(summary->property("interactive").toBool(), false);
  QCOMPARE(summary->cursor().shape(), Qt::ArrowCursor);
  openBreakdown(summary);
  QWidget* popup = breakdownPopup(panel);
  QVERIFY(!popup || !popup->isVisible());

  auto* teams = panel.findChild<QComboBox*>(QStringLiteral("PresentationTeamFilter"));
  QVERIFY(teams);
  teams->setCurrentIndex(teams->findData(QStringLiteral("Away")));
  QApplication::processEvents();
  QCOMPARE(summary->property("interactive").toBool(), false);

  auto* events = panel.findChild<QComboBox*>(QStringLiteral("PresentationEventFilter"));
  QVERIFY(events);
  events->setCurrentIndex(events->findData(static_cast<int>(EventCatalog::EventId::Goal)));
  QApplication::processEvents();
  QCOMPARE(summary->property("interactive").toBool(), true);
  openBreakdown(summary);
  popup = breakdownPopup(panel);
  QVERIFY(popup && popup->isVisible());
  const QList<QWidget*> rows = eventRows(popup);
  QCOMPARE(rows.size(), 1);
  QCOMPARE(rows.at(0)->property("displayLabel").toString(), QStringLiteral("Shot"));
  QCOMPARE(rows.at(0)->property("selectedCount").toInt(), 2);

  const QVector<int> selectionBefore = panel.selectedTagIndexes();
  QTest::mouseClick(rows.at(0), Qt::LeftButton);
  QApplication::processEvents();
  QCOMPARE(panel.selectedTagIndexes(), selectionBefore);
  QCOMPARE(events->currentData().toInt(), static_cast<int>(EventCatalog::EventId::Shot));
  QVERIFY(!popup->isVisible());
  QCOMPARE(summary->property("interactive").toBool(), false);
}

void PresentationSelectionBreakdownTest::worstCasePopupFitsTheSideColumn() {
  qputenv("AVA_BREAK_UI", QByteArray("1"));
  PresentationPanel panel;
  showPanel(panel);

  auto* worst = panel.findChild<QToolButton*>(QStringLiteral("PresentationSelectionMapWorst"));
  QVERIFY(worst);
  QTest::mouseClick(worst, Qt::LeftButton);
  QApplication::processEvents();

  QLabel* summary = summaryLabel(panel);
  QCOMPARE(summary->property("interactive").toBool(), true);
  openBreakdown(summary);
  QWidget* popup = breakdownPopup(panel);
  QVERIFY(popup && popup->isVisible());
  QVERIFY(popup->width() <= 260);
  QVERIFY(popup->height() <= 240 + 8);

  auto* scroll = popup->findChild<QScrollArea*>(QStringLiteral("PresentationSelectionEventScroll"));
  QVERIFY(scroll);
  QVERIFY(scroll->verticalScrollBar()->maximum() > 0);
  QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);

  const QList<QWidget*> rows = eventRows(popup);
  QVERIFY(rows.size() >= 14);
  bool sawOne = false;
  bool sawLarge = false;
  bool sawScript = false;
  bool sawNewline = false;
  bool sawShort = false;
  bool sawEmail = false;
  for (QWidget* row : rows) {
    QVERIFY(row->property("selectedCount").toInt() >= 1);
    QVERIFY(row->height() >= 32);
    QVERIFY(row->width() <= popup->width());

    auto* name = row->findChild<QWidget*>(QStringLiteral("PresentationSelectionEventName"));
    auto* count = row->findChild<QWidget*>(QStringLiteral("PresentationSelectionEventCount"));
    auto* dash = row->findChild<QWidget*>(QStringLiteral("PresentationSelectionEventDash"));
    QVERIFY(name && count && dash);
    const QRect nameRect(name->mapTo(row, QPoint(0, 0)), name->size());
    const QRect countRect(count->mapTo(row, QPoint(0, 0)), count->size());
    QVERIFY2(row->rect().adjusted(-1, -1, 1, 1).contains(nameRect),
             qPrintable(row->property("displayLabel").toString()));
    QVERIFY2(row->rect().contains(countRect), qPrintable(row->property("displayLabel").toString()));
    QVERIFY(!nameRect.adjusted(0, 0, -1, -1).intersects(countRect));
    QVERIFY(name->height() >= name->fontMetrics().height());
    QCOMPARE(name->property("fullText").toString(), row->property("displayLabel").toString());

    const int countValue = row->property("selectedCount").toInt();
    if (countValue == 1) sawOne = true;
    if (countValue == 1284) sawLarge = true;
    const QString label = row->property("displayLabel").toString();
    if (label == QStringLiteral("<script>alert(1)</script>")) {
      sawScript = true;
      QCOMPARE(name->property("fullText").toString(), label);
      QVERIFY(!row->toolTip().contains(QStringLiteral("<script>")));
    }
    if (label.contains(QLatin1Char('\n'))) {
      sawNewline = true;
      QVERIFY(name->height() > name->fontMetrics().height());
    }
    if (label == QStringLiteral("J")) {
      sawShort = true;
      QVERIFY(row->width() > row->fontMetrics().horizontalAdvance(label) + 24);
    }
    if (label.contains(QStringLiteral("bartholomew.fitzgerald@"))) {
      sawEmail = true;
      QVERIFY(name->fontMetrics().horizontalAdvance(label) > name->width());
      QVERIFY(name->height() > name->fontMetrics().height());
    }
    if (row->property("clamped").toBool()) {
      QCOMPARE(row->toolTip(), label.toHtmlEscaped());
      QVERIFY(name->height() <= name->fontMetrics().lineSpacing() * 6 + 2);
    }
  }
  QVERIFY(sawOne);
  QVERIFY(sawLarge);
  QVERIFY(sawScript);
  QVERIFY(sawNewline);
  QVERIFY(sawShort);
  QVERIFY(sawEmail);
}

void PresentationSelectionBreakdownTest::oneAndEmptyFixtures() {
  qputenv("AVA_BREAK_UI", QByteArray("1"));
  PresentationPanel panel;
  showPanel(panel);
  QLabel* summary = summaryLabel(panel);
  QCOMPARE(summary->property("interactive").toBool(), false);

  auto* one = panel.findChild<QToolButton*>(QStringLiteral("PresentationSelectionMapOne"));
  QVERIFY(one);
  QTest::mouseClick(one, Qt::LeftButton);
  QApplication::processEvents();
  QCOMPARE(summary->property("interactive").toBool(), true);
  QCOMPARE(summary->cursor().shape(), Qt::PointingHandCursor);
  openBreakdown(summary);
  QWidget* popup = breakdownPopup(panel);
  QVERIFY(popup && popup->isVisible());
  const QList<QWidget*> rows = eventRows(popup);
  QCOMPARE(rows.size(), 1);
  QCOMPARE(rows.at(0)->property("displayLabel").toString(), QStringLiteral("J"));
  QCOMPARE(rows.at(0)->property("selectedCount").toInt(), 1);
  QVERIFY(rows.at(0)->height() >= 32);
  QVERIFY(rows.at(0)->width() > 32);

  auto* empty = panel.findChild<QToolButton*>(QStringLiteral("PresentationSelectionMapEmpty"));
  QVERIFY(empty);
  QTest::mouseClick(empty, Qt::LeftButton);
  QApplication::processEvents();
  QCOMPARE(summary->property("interactive").toBool(), false);
  QCOMPARE(summary->cursor().shape(), Qt::ArrowCursor);
  QVERIFY(!popup->isVisible());
  openBreakdown(summary);
  QVERIFY(!breakdownPopup(panel) || !breakdownPopup(panel)->isVisible());
}

void PresentationSelectionBreakdownTest::mapToggleDoesNotChangeTheCheck() {
  qputenv("AVA_BREAK_UI", QByteArray("1"));
  TagSession session;
  session.addTag(playTag(QStringLiteral("Goal"), QStringLiteral("Home"), 1000));
  session.addTag(playTag(QStringLiteral("Shot"), QStringLiteral("Away"), 2000));

  PresentationPanel panel;
  panel.setTagSession(&session);
  showPanel(panel);
  QVERIFY(checkEveryRow(panel));
  QCOMPARE(panel.selectedTagIndexes().size(), 2);
  QSignalSpy selectionSpy(&panel, &PresentationPanel::selectedTagIndexesChanged);

  const char* names[] = {"PresentationSelectionMapWorst", "PresentationSelectionMapEmpty",
                         "PresentationSelectionMapOne", "PresentationSelectionMapDemo"};
  for (const char* name : names) {
    auto* button = panel.findChild<QToolButton*>(QLatin1String(name));
    QVERIFY(button);
    QTest::mouseClick(button, Qt::LeftButton);
  }
  QCOMPARE(panel.selectedTagIndexes().size(), 2);
  QCOMPARE(selectionSpy.count(), 0);
  QVERIFY(panel.findChild<QWidget*>(QStringLiteral("PresentationSelectionMapToggle")));
}

int runPresentationSelectionBreakdownTests(int argc, char** argv) {
  PresentationSelectionBreakdownTest tests;
  return QTest::qExec(&tests, argc, argv);
}

#include "PresentationSelectionBreakdownTests.moc"
