#include "TimelineBar.h"

#include <QLabel>
#include <QSignalSpy>
#include <QSlider>
#include <QTest>

namespace {

constexpr qint64 kDurationMs = 100000;
constexpr qint64 kPressPositionMs = 5000;
constexpr qint64 kDropPositionMs = 80000;

// Qt emits sliderReleased before it copies sliderPosition into value when
// tracking is off. Driving setSliderDown matches QSlider::mouseReleaseEvent.
void dragSlider(QSlider* slider, int dropSliderValue) {
  slider->setSliderDown(true);
  slider->setSliderPosition(dropSliderValue);
}

} // namespace

class TimelineScrubTest : public QObject {
  Q_OBJECT

private slots:
  void releaseSeeksToTheDropPosition();
};

void TimelineScrubTest::releaseSeeksToTheDropPosition() {
  TimelineBar bar;
  bar.setDurationMs(kDurationMs);
  bar.setPositionMs(kPressPositionMs);

  auto* slider = bar.findChild<QSlider*>(QStringLiteral("TimelineSlider"));
  auto* label = bar.findChild<QLabel*>();
  QVERIFY(slider);
  QVERIFY(label);
  QVERIFY(!slider->hasTracking());
  QCOMPARE(slider->value(), static_cast<int>(kPressPositionMs));

  QSignalSpy finished(&bar, &TimelineBar::scrubFinished);
  QSignalSpy liveSeek(&bar, &TimelineBar::scrubSeekTo);
  QVERIFY(finished.isValid());
  QVERIFY(liveSeek.isValid());

  // Release before the 33ms throttle fires. value() is still the press point.
  dragSlider(slider, static_cast<int>(kDropPositionMs));
  QCOMPARE(slider->value(), static_cast<int>(kPressPositionMs));
  QCOMPARE(slider->sliderPosition(), static_cast<int>(kDropPositionMs));
  QCOMPARE(label->text(), QStringLiteral("01:20 / 01:40"));

  slider->setSliderDown(false);

  QCOMPARE(finished.count(), 1);
  QCOMPARE(finished.at(0).at(0).toLongLong(), kDropPositionMs);
  QCOMPARE(slider->value(), static_cast<int>(kDropPositionMs));
  QCOMPARE(label->text(), QStringLiteral("01:20 / 01:40"));
  QCOMPARE(liveSeek.count(), 0);

  // Let the player "arrive" so the commit wait releases the slider, then drag
  // again and let the throttle consume the pending target before release.
  bar.setPositionMs(kDropPositionMs);
  bar.setPositionMs(kPressPositionMs);
  QCOMPARE(slider->value(), static_cast<int>(kPressPositionMs));

  dragSlider(slider, static_cast<int>(kDropPositionMs));
  QTest::qWait(80);
  QVERIFY(liveSeek.count() >= 1);
  QCOMPARE(liveSeek.at(liveSeek.count() - 1).at(0).toLongLong(), kDropPositionMs);
  QCOMPARE(slider->value(), static_cast<int>(kPressPositionMs));

  slider->setSliderDown(false);

  QCOMPARE(finished.count(), 2);
  QCOMPARE(finished.at(1).at(0).toLongLong(), kDropPositionMs);
  QCOMPARE(slider->value(), static_cast<int>(kDropPositionMs));
  QCOMPARE(label->text(), QStringLiteral("01:20 / 01:40"));
}

int runTimelineScrubTests(int argc, char** argv) {
  TimelineScrubTest tests;
  return QTest::qExec(&tests, argc, argv);
}

#include "TimelineScrubTests.moc"
