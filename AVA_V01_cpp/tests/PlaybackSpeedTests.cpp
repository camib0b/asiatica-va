#include "AppLocale.h"
#include "PlaybackSpeedometer.h"
#include "VideoControlsBar.h"
#include "WorkWindow.h"
#include "TabularDigits.h"

#include <QLayout>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>

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

int runPlaybackSpeedTests(int argc, char** argv) {
  PlaybackSpeedTest tests;
  return QTest::qExec(&tests, argc, argv);
}

#include "PlaybackSpeedTests.moc"
