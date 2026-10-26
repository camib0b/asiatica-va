#include "AppLocale.h"
#include "PlaybackSpeedometer.h"
#include "VideoControlsBar.h"
#include "VideoPlayer.h"
#include "WorkWindow.h"
#include "TabularDigits.h"

#include <QApplication>
#include <QCursor>
#include <QEvent>
#include <QLayout>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>

namespace {

class RestoreLanguage {
 public:
  RestoreLanguage() : previous_(AppLocale::currentLanguage()) {}
  ~RestoreLanguage() { AppLocale::setLanguage(previous_); }

 private:
  AppLocale::Language previous_;
};

void prepareDial(PlaybackSpeedometer& dial) {
  dial.setEnabled(true);
  dial.resize(dial.sizeHint());
  dial.show();
}

// Qt delivers a double-click as press, release, double-click, release.
// QTest::mouseDClick sends only the double-click event, so it never shows
// the press committing a step before the double-click handler runs.
void sendDoubleClick(QWidget* widget, const QPoint& position) {
  QTest::mouseEvent(QTest::MousePress, widget, Qt::LeftButton, Qt::NoModifier, position, 0);
  QTest::mouseEvent(QTest::MouseRelease, widget, Qt::LeftButton, Qt::NoModifier, position, 0);
  QTest::mouseEvent(QTest::MouseDClick, widget, Qt::LeftButton, Qt::NoModifier, position, 0);
  QTest::mouseEvent(QTest::MouseRelease, widget, Qt::LeftButton, Qt::NoModifier, position, 0);
}

void placeCursorOver(QWidget* widget) {
  QCursor::setPos(widget->mapToGlobal(widget->rect().center()));
  QApplication::processEvents();
}

void placeCursorOutsideControls(QWidget* controls) {
  QCursor::setPos(controls->mapToGlobal(QPoint(-80, -80)));
  QApplication::processEvents();
}

void sendLeave(QWidget* widget) {
  QEvent leave(QEvent::Leave);
  QApplication::sendEvent(widget, &leave);
}

struct ShownControls {
  VideoPlayer player;
  VideoControlsBar* bar = nullptr;
  PlaybackSpeedometer* speed = nullptr;
  QTimer* idleTimer = nullptr;
  QPropertyAnimation* fade = nullptr;

  bool open() {
    player.resize(960, 540);
    player.show();
    if (!QTest::qWaitForWindowExposed(&player)) {
      return false;
    }
    player.setControlsVisible(true);
    player.setControlsEnabled(true);
    QApplication::processEvents();
    bar = player.controlsBar();
    if (!bar || !bar->isVisible()) {
      return false;
    }
    speed = bar->findChild<PlaybackSpeedometer*>();
    idleTimer = player.findChild<QTimer*>(QStringLiteral("controlsIdleTimer"));
    fade = player.findChild<QPropertyAnimation*>(QStringLiteral("controlsFadeAnimation"));
    if (!speed || !idleTimer || !fade) {
      return false;
    }
    fade->setDuration(0);
    placeCursorOver(bar);
    return true;
  }
};

void reshowControls(ShownControls& controls) {
  controls.player.setControlsVisible(true);
  QApplication::processEvents();
  placeCursorOver(controls.bar);
}

bool hideAfterPointerLeaves(ShownControls& controls) {
  placeCursorOutsideControls(controls.bar);
  sendLeave(controls.bar);
  if (!controls.idleTimer->isActive()) {
    return false;
  }
  // A zero-duration opacity animation never reaches the finished handler on
  // the offscreen platform, so the fade needs a short real interval.
  controls.fade->setDuration(30);
  controls.idleTimer->start(0);
  return QTest::qWaitFor([&controls]() { return !controls.bar->isVisible(); }, 1000);
}

}  // namespace

class PlaybackSpeedTest : public QObject {
  Q_OBJECT

 private slots:
  void doubleClickOnTheTrackKeepsThatStep();
  void doubleClickOnTheReadoutResetsWithoutAPriorStep();
  void disabledReadoutDoubleClickDoesNotChangeSpeed();
  void clickAndDragSelectTheStepUnderThePointer();
  void readoutSitsBesideTheGrooveAndShowsTheRate();
  void controlsBarTooltipShowsTheSpeedShortcuts();
  void speedControlStaysBesideTransportInEveryMode();
  void secondPressWhileDraggingDoesNotOpenAnotherSession();
  void controlsHideAfterDoubleClickRapidClicksAndLeave();
};

void PlaybackSpeedTest::doubleClickOnTheTrackKeepsThatStep() {
  PlaybackSpeedometer dial;
  prepareDial(dial);
  dial.setRate(2.0);

  QSignalSpy rates(&dial, &PlaybackSpeedometer::rateChanged);
  QSignalSpy started(&dial, &PlaybackSpeedometer::interactionStarted);
  QSignalSpy finished(&dial, &PlaybackSpeedometer::interactionFinished);
  QVERIFY(rates.isValid());
  QVERIFY(started.isValid());
  QVERIFY(finished.isValid());

  const QRect groove = dial.trackGrooveRect();
  const QPoint fastEnd(groove.right(), groove.center().y());
  sendDoubleClick(&dial, fastEnd);

  QCOMPARE(dial.rate(), 4.0);
  QCOMPARE(dial.displayedRateText(), QStringLiteral("4.00x"));
  QCOMPARE(rates.count(), 1);
  QCOMPARE(rates.at(0).at(0).toDouble(), 4.0);
  QCOMPARE(started.count(), 1);
  QCOMPARE(finished.count(), 1);
}

void PlaybackSpeedTest::doubleClickOnTheReadoutResetsWithoutAPriorStep() {
  PlaybackSpeedometer dial;
  prepareDial(dial);
  dial.setRate(2.0);

  QSignalSpy rates(&dial, &PlaybackSpeedometer::rateChanged);
  QSignalSpy started(&dial, &PlaybackSpeedometer::interactionStarted);
  QSignalSpy finished(&dial, &PlaybackSpeedometer::interactionFinished);

  sendDoubleClick(&dial, dial.rateReadoutRect().center());

  QCOMPARE(dial.rate(), 1.0);
  QCOMPARE(dial.displayedRateText(), QStringLiteral("1.00x"));
  QCOMPARE(rates.count(), 1);
  QCOMPARE(rates.at(0).at(0).toDouble(), 1.0);
  QCOMPARE(started.count(), 0);
  QCOMPARE(finished.count(), 0);
}

void PlaybackSpeedTest::disabledReadoutDoubleClickDoesNotChangeSpeed() {
  PlaybackSpeedometer dial;
  prepareDial(dial);
  dial.setRate(2.0);
  dial.setEnabled(false);

  QSignalSpy rates(&dial, &PlaybackSpeedometer::rateChanged);
  sendDoubleClick(&dial, dial.rateReadoutRect().center());

  QCOMPARE(dial.rate(), 2.0);
  QCOMPARE(rates.count(), 0);
}

void PlaybackSpeedTest::clickAndDragSelectTheStepUnderThePointer() {
  PlaybackSpeedometer dial;
  prepareDial(dial);
  dial.show();

  const QRect groove = dial.trackGrooveRect();
  const QPoint slowEnd(groove.left(), groove.center().y());
  const QPoint fastEnd(groove.right(), groove.center().y());

  QSignalSpy rates(&dial, &PlaybackSpeedometer::rateChanged);
  QSignalSpy started(&dial, &PlaybackSpeedometer::interactionStarted);
  QSignalSpy finished(&dial, &PlaybackSpeedometer::interactionFinished);

  QTest::mouseClick(&dial, Qt::LeftButton, Qt::NoModifier, slowEnd);
  QCOMPARE(dial.rate(), 0.25);
  QCOMPARE(dial.displayedRateText(), QStringLiteral("0.25x"));
  QCOMPARE(started.count(), 1);
  QCOMPARE(finished.count(), 1);

  const int ratesAfterClick = rates.count();
  QTest::mouseClick(&dial, Qt::LeftButton, Qt::NoModifier, dial.rateReadoutRect().center());
  QCOMPARE(dial.rate(), 0.25);
  QCOMPARE(rates.count(), ratesAfterClick);
  QCOMPARE(started.count(), 1);
  QCOMPARE(finished.count(), 1);

  QTest::mousePress(&dial, Qt::LeftButton, Qt::NoModifier, slowEnd);
  QTest::mouseMove(&dial, fastEnd);
  QTest::mouseRelease(&dial, Qt::LeftButton, Qt::NoModifier, fastEnd);

  QCOMPARE(dial.rate(), 4.0);
  QCOMPARE(dial.displayedRateText(), QStringLiteral("4.00x"));
  QCOMPARE(started.count(), 2);
  QCOMPARE(finished.count(), 2);
  QVERIFY(rates.count() >= 2);
  QCOMPARE(rates.at(rates.count() - 1).at(0).toDouble(), 4.0);
}

void PlaybackSpeedTest::readoutSitsBesideTheGrooveAndShowsTheRate() {
  PlaybackSpeedometer dial;
  prepareDial(dial);

  QCOMPARE(dial.displayedRateText(), QStringLiteral("1.00x"));
  dial.setRate(0.25);
  QCOMPARE(dial.displayedRateText(), QStringLiteral("0.25x"));
  dial.setRate(1.0);
  QCOMPARE(dial.displayedRateText(), QStringLiteral("1.00x"));
  dial.setRate(2.0);
  QCOMPARE(dial.displayedRateText(), QStringLiteral("2.00x"));

  const QRect groove = dial.trackGrooveRect();
  const QRect readout = dial.rateReadoutRect();
  QVERIFY(groove.width() > 0);
  QVERIFY(readout.width() > 0);
  QVERIFY(readout.left() > groove.right());
  QVERIFY(!readout.intersects(groove));
  QVERIFY(readout.height() >= dial.fontMetrics().height());
  const int widestRate =
      Style::tabularTextWidth(dial.fontMetrics(), QStringLiteral("4.00x"));
  QVERIFY(readout.width() >= widestRate);

  const QPoint normalSpeed(groove.left() + (3 * groove.width()) / 9, groove.center().y());
  QTest::mouseClick(&dial, Qt::LeftButton, Qt::NoModifier, normalSpeed);
  QCOMPARE(dial.rate(), 1.0);
}

void PlaybackSpeedTest::controlsBarTooltipShowsTheSpeedShortcuts() {
  const RestoreLanguage restoreLanguage;
  AppLocale::setLanguage(AppLocale::Language::English);

  VideoControlsBar bar;
  bar.setEnabledForMedia(true);
  auto* speed = bar.findChild<PlaybackSpeedometer*>();
  auto* play = bar.findChild<QPushButton*>(QStringLiteral("PlayPauseButton"));
  QVERIFY(speed);
  QVERIFY(play);

  const QString english = AppLocale::trUi("vc.tt.speed");
  QVERIFY(english.contains(QStringLiteral("Double-click")));
  QVERIFY(english.contains(QLatin1Char('-')));
  QVERIFY(english.contains(QLatin1Char('+')));
  QVERIFY(english.contains(QLatin1Char('}')));
  QCOMPARE(speed->toolTip(), english);

  QLayout* layout = bar.layout();
  QVERIFY(layout);
  QVERIFY(layout->indexOf(play) >= 0);
  QVERIFY(layout->indexOf(play) < layout->indexOf(speed));
  int lastWidgetIndex = -1;
  for (int index = 0; index < layout->count(); ++index) {
    if (layout->itemAt(index)->widget() != nullptr) {
      lastWidgetIndex = index;
    }
  }
  QCOMPARE(layout->indexOf(speed), lastWidgetIndex);
  QVERIFY(speed->isVisibleTo(&bar));

  AppLocale::setLanguage(AppLocale::Language::Spanish);
  bar.applyUiStrings();
  const QString spanish = AppLocale::trUi("vc.tt.speed");
  QVERIFY(spanish.contains(QStringLiteral("Doble clic")));
  QVERIFY(spanish.contains(QLatin1Char('}')));
  QCOMPARE(speed->toolTip(), spanish);
}

void PlaybackSpeedTest::speedControlStaysBesideTransportInEveryMode() {
  WorkWindow window;
  auto* speed = window.findChild<PlaybackSpeedometer*>();
  QVERIFY(speed);
  auto* bar = speed->parentWidget();
  QVERIFY(bar);
  QCOMPARE(bar->objectName(), QStringLiteral("VideoControlsBar"));
  QVERIFY(speed->isVisibleTo(bar));
  QVERIFY(!speed->displayedRateText().isEmpty());
  QVERIFY(!speed->toolTip().isEmpty());

  const WorkWindow::Mode modes[] = {
      WorkWindow::Mode::Analyzing,
      WorkWindow::Mode::Presenting,
      WorkWindow::Mode::Tagging,
  };
  for (WorkWindow::Mode mode : modes) {
    window.setMode(mode);
    QCOMPARE(window.mode(), mode);
    QCOMPARE(speed->parentWidget(), bar);
    QVERIFY(speed->isVisibleTo(bar));
    QVERIFY(!speed->toolTip().isEmpty());
    QCOMPARE(speed->displayedRateText(), QStringLiteral("1.00x"));
  }
}

void PlaybackSpeedTest::secondPressWhileDraggingDoesNotOpenAnotherSession() {
  PlaybackSpeedometer dial;
  prepareDial(dial);
  const QPoint fastEnd(dial.trackGrooveRect().right(), dial.trackGrooveRect().center().y());

  QSignalSpy started(&dial, &PlaybackSpeedometer::interactionStarted);
  QSignalSpy finished(&dial, &PlaybackSpeedometer::interactionFinished);
  QVERIFY(started.isValid());
  QVERIFY(finished.isValid());

  QTest::mouseEvent(QTest::MousePress, &dial, Qt::LeftButton, Qt::NoModifier, fastEnd, 0);
  QTest::mouseEvent(QTest::MousePress, &dial, Qt::LeftButton, Qt::NoModifier, fastEnd, 0);
  QTest::mouseEvent(QTest::MouseRelease, &dial, Qt::LeftButton, Qt::NoModifier, fastEnd, 0);
  QCOMPARE(started.count(), 1);
  QCOMPARE(finished.count(), 1);

  started.clear();
  finished.clear();
  QTest::mouseEvent(QTest::MousePress, &dial, Qt::LeftButton, Qt::NoModifier, fastEnd, 0);
  QTest::mouseEvent(QTest::MouseDClick, &dial, Qt::LeftButton, Qt::NoModifier, fastEnd, 0);
  QTest::mouseEvent(QTest::MouseRelease, &dial, Qt::LeftButton, Qt::NoModifier, fastEnd, 0);
  QCOMPARE(started.count(), 1);
  QCOMPARE(finished.count(), 1);
}

void PlaybackSpeedTest::controlsHideAfterDoubleClickRapidClicksAndLeave() {
  ShownControls controls;
  QVERIFY2(controls.open(), "speed controls did not show");

  placeCursorOver(controls.bar);
  sendDoubleClick(controls.speed, controls.speed->trackGrooveRect().center());
  QVERIFY2(!controls.idleTimer->isActive(),
           "double-click left the idle timer running while the pointer was still over the bar");
  QVERIFY2(hideAfterPointerLeaves(controls),
           "double-click left the speed controls visible after the pointer left");

  reshowControls(controls);
  QVERIFY(controls.bar->isVisible());
  QSignalSpy started(controls.speed, &PlaybackSpeedometer::interactionStarted);
  QSignalSpy finished(controls.speed, &PlaybackSpeedometer::interactionFinished);
  const QPoint onTrack = controls.speed->trackGrooveRect().center();
  for (int click = 0; click < 4; ++click) {
    placeCursorOver(controls.bar);
    QTest::mouseClick(controls.speed, Qt::LeftButton, Qt::NoModifier, onTrack);
  }
  QCOMPARE(started.count(), 4);
  QCOMPARE(finished.count(), 4);
  placeCursorOver(controls.bar);
  QVERIFY(!controls.idleTimer->isActive());
  QVERIFY2(hideAfterPointerLeaves(controls),
           "rapid clicks left the speed controls visible after the pointer left");

  reshowControls(controls);
  placeCursorOver(controls.bar);
  QTest::mousePress(controls.speed, Qt::LeftButton, Qt::NoModifier, onTrack);
  QVERIFY(!controls.idleTimer->isActive());
  placeCursorOutsideControls(controls.bar);
  sendLeave(controls.bar);
  QVERIFY2(!controls.idleTimer->isActive(),
           "leaving during a speed drag armed the idle timer");
  QTest::mouseRelease(controls.speed, Qt::LeftButton, Qt::NoModifier, onTrack);
  sendLeave(controls.bar);
  QVERIFY2(controls.idleTimer->isActive(),
           "releasing a speed drag outside the bar did not arm the idle timer");
  controls.idleTimer->start(0);
  QVERIFY(QTest::qWaitFor([&controls]() { return !controls.bar->isVisible(); }, 1000));
}

int runPlaybackSpeedTests(int argc, char** argv) {
  PlaybackSpeedTest tests;
  return QTest::qExec(&tests, argc, argv);
}

#include "PlaybackSpeedTests.moc"
