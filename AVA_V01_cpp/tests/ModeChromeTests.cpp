#include "WorkWindow.h"

#include <QApplication>
#include <QLayout>
#include <QSplitter>
#include <QStackedWidget>
#include <QTableView>
#include <QTest>

namespace {

QWidget* gameControls(QWidget* root) {
  for (QWidget* widget : root->findChildren<QWidget*>()) {
    if (QLatin1String(widget->metaObject()->className()) == QLatin1String("GameControls")) {
      return widget;
    }
  }
  return nullptr;
}

void showWorkSurface(WorkWindow* window) {
  auto* outer = window->findChild<QSplitter*>(QStringLiteral("PresentationSplitter"));
  if (!outer) return;
  outer->show();
  QWidget* walker = outer;
  while (walker->parentWidget() && !qobject_cast<QStackedWidget*>(walker->parentWidget())) {
    walker = walker->parentWidget();
  }
  if (auto* content = qobject_cast<QStackedWidget*>(walker->parentWidget())) {
    content->setCurrentWidget(walker);
  }
  window->resize(1100, 700);
  window->show();
  if (window->layout()) window->layout()->activate();
  // Do not pump the event loop. A shown video surface keeps posting timer
  // events, and processEvents() then never returns.
  QApplication::sendPostedEvents();
}

QWidget* pageForMode(WorkWindow* window, WorkWindow::Mode mode) {
  switch (mode) {
    case WorkWindow::Mode::Tagging:
      return window->findChild<QWidget*>(QStringLiteral("TaggingRightCol"));
    case WorkWindow::Mode::Analyzing:
      return window->findChild<QWidget*>(QStringLiteral("StatsPanel"));
    case WorkWindow::Mode::Presenting:
      return window->findChild<QWidget*>(QStringLiteral("PresentationPanel"));
  }
  return nullptr;
}

}  // namespace

class WorkModeChromeTest : public QObject {
  Q_OBJECT

 private slots:
  void taggingAnalyzingTaggingShowsControls();
  void repeatedSwitchesKeepEachModesPanel();
};

void WorkModeChromeTest::taggingAnalyzingTaggingShowsControls() {
  WorkWindow window;
  showWorkSurface(&window);

  auto* tagging = window.findChild<QWidget*>(QStringLiteral("TaggingRightCol"));
  auto* stats = window.findChild<QWidget*>(QStringLiteral("StatsPanel"));
  auto* tags = window.findChild<QTableView*>(QStringLiteral("TagsTable"));
  auto* controls = gameControls(&window);
  auto* outer = window.findChild<QSplitter*>(QStringLiteral("PresentationSplitter"));
  QVERIFY(tagging && stats && tags && controls && outer);
  auto* side = qobject_cast<QStackedWidget*>(tagging->parentWidget());
  QVERIFY(side);

  QCOMPARE(side->currentWidget(), static_cast<QWidget*>(tagging));
  QCOMPARE(outer->indexOf(side), 0);
  QVERIFY(controls->isVisible());

  window.setMode(WorkWindow::Mode::Analyzing);
  QCOMPARE(side->currentWidget(), static_cast<QWidget*>(stats));
  QCOMPARE(outer->indexOf(side), 1);
  QVERIFY(!controls->isVisible());

  window.setMode(WorkWindow::Mode::Tagging);
  QCOMPARE(window.mode(), WorkWindow::Mode::Tagging);
  QCOMPARE(side->currentWidget(), static_cast<QWidget*>(tagging));
  QCOMPARE(outer->indexOf(side), 0);
  QVERIFY(controls->isVisible());
  QVERIFY(!stats->isVisible());
  QVERIFY(tags->mapTo(&window, QPoint(0, 0)).x() > tagging->mapTo(&window, QPoint(0, 0)).x());
}

void WorkModeChromeTest::repeatedSwitchesKeepEachModesPanel() {
  WorkWindow window;
  showWorkSurface(&window);
  auto* tagging = window.findChild<QWidget*>(QStringLiteral("TaggingRightCol"));
  auto* outer = window.findChild<QSplitter*>(QStringLiteral("PresentationSplitter"));
  QVERIFY(tagging && outer);
  auto* side = qobject_cast<QStackedWidget*>(tagging->parentWidget());
  QVERIFY(side);

  const WorkWindow::Mode sequence[] = {
      WorkWindow::Mode::Analyzing,  WorkWindow::Mode::Tagging,    WorkWindow::Mode::Presenting,
      WorkWindow::Mode::Analyzing,  WorkWindow::Mode::Presenting, WorkWindow::Mode::Tagging,
      WorkWindow::Mode::Analyzing,  WorkWindow::Mode::Tagging,    WorkWindow::Mode::Tagging};

  for (WorkWindow::Mode mode : sequence) {
    window.setMode(mode);
    QCOMPARE(window.mode(), mode);
    QCOMPARE(side->currentWidget(), pageForMode(&window, mode));
    const int expectedIndex = mode == WorkWindow::Mode::Tagging ? 0 : 1;
    QCOMPARE(outer->indexOf(side), expectedIndex);
  }

  auto* controls = gameControls(&window);
  QVERIFY(controls);
  QVERIFY(controls->isVisible());
  auto* stats = window.findChild<QWidget*>(QStringLiteral("StatsPanel"));
  QVERIFY(stats);
  QVERIFY(!stats->isVisible());
}

int runModeChromeTests(int argc, char** argv) {
  WorkModeChromeTest tests;
  return QTest::qExec(&tests, argc, argv);
}

#include "ModeChromeTests.moc"
