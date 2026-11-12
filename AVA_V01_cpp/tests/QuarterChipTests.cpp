#include "AppLocale.h"
#include "GameControlGrid.h"
#include "GameControls.h"
#include "TagSession.h"
#include "WorkWindow.h"

#include <QAbstractAnimation>
#include <QApplication>
#include <QFile>
#include <QFont>
#include <QHash>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace {

constexpr int kChipHeightPx = 28;
constexpr int kChipMinimumWidthPx = 24;
constexpr int kChipGapPx = 4;
constexpr double kTextContrastMinimum = 4.5;
constexpr double kBorderContrastMinimum = 3.0;

QString loadThemeStylesheet() {
  QFile themeFile(QStringLiteral(AVA_THEME_QSS_PATH));
  if (!themeFile.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
  return QString::fromUtf8(themeFile.readAll());
}

double srgbChannelLuminance(int component) {
  const double encoded = component / 255.0;
  if (encoded <= 0.04045) return encoded / 12.92;
  return std::pow((encoded + 0.055) / 1.055, 2.4);
}

double relativeLuminance(const QColor& color) {
  return 0.2126 * srgbChannelLuminance(color.red()) + 0.7152 * srgbChannelLuminance(color.green()) +
         0.0722 * srgbChannelLuminance(color.blue());
}

double contrastRatio(const QColor& first, const QColor& second) {
  const double firstLuminance = relativeLuminance(first);
  const double secondLuminance = relativeLuminance(second);
  const double lighter = std::max(firstLuminance, secondLuminance);
  const double darker = std::min(firstLuminance, secondLuminance);
  return (lighter + 0.05) / (darker + 0.05);
}

QColor opaqueOnWhite(QRgb pixel) {
  const QColor color = QColor::fromRgba(pixel);
  if (color.alpha() >= 255) return color;
  const double alpha = color.alpha() / 255.0;
  return QColor(qRound(color.red() * alpha + 255.0 * (1.0 - alpha)),
                qRound(color.green() * alpha + 255.0 * (1.0 - alpha)),
                qRound(color.blue() * alpha + 255.0 * (1.0 - alpha)));
}

bool colorsNear(const QColor& actual, const QColor& expected, int tolerance) {
  return qAbs(actual.red() - expected.red()) <= tolerance &&
         qAbs(actual.green() - expected.green()) <= tolerance &&
         qAbs(actual.blue() - expected.blue()) <= tolerance;
}

QString descriptionForState(const QString& quarterState) {
  if (quarterState == QLatin1String("current")) return QStringLiteral("Current");
  if (quarterState == QLatin1String("complete")) return QStringLiteral("Done");
  return QStringLiteral("Upcoming");
}

QList<QLabel*> quarterChips(QWidget* root) {
  QList<QLabel*> chips;
  if (!root) return chips;
  for (QLabel* label : root->findChildren<QLabel*>()) {
    if (label->property("role").toString() == QLatin1String("quarterSegment")) chips.append(label);
  }
  std::sort(chips.begin(), chips.end(), [](const QLabel* left, const QLabel* right) {
    return left->text() < right->text();
  });
  return chips;
}

QWidget* quarterTrack(QWidget* root) {
  if (!root) return nullptr;
  for (QWidget* widget : root->findChildren<QWidget*>()) {
    if (widget->property("role").toString() == QLatin1String("quarterTrack")) return widget;
  }
  return nullptr;
}

QPushButton* periodButton(QWidget* root) {
  if (!root) return nullptr;
  for (QLabel* label : root->findChildren<QLabel*>()) {
    if (label->property("role").toString() != QLatin1String("gameControlTitle")) continue;
    if (label->property("fullLabel").toString().isEmpty()) continue;
    auto* button = qobject_cast<QPushButton*>(label->parentWidget());
    if (button && button->property("gameEventName").toString().isEmpty()) return button;
  }
  return nullptr;
}

QStringList statesForQuarter(int currentQuarterIndex, bool gameEnded) {
  QStringList states;
  for (int quarterIndex = 0; quarterIndex < 4; ++quarterIndex) {
    if (gameEnded || (currentQuarterIndex >= 0 && quarterIndex < currentQuarterIndex)) {
      states.append(QStringLiteral("complete"));
    } else if (quarterIndex == currentQuarterIndex) {
      states.append(QStringLiteral("current"));
    } else {
      states.append(QStringLiteral("empty"));
    }
  }
  return states;
}

QString chipStateFailure(const QList<QLabel*>& chips, const QStringList& expectedStates) {
  if (chips.size() != expectedStates.size()) {
    return QStringLiteral("chip count %1").arg(chips.size());
  }
  for (int index = 0; index < chips.size(); ++index) {
    const QLabel* chip = chips.at(index);
    const QString state = chip->property("quarterState").toString();
    if (state != expectedStates.at(index)) {
      return QStringLiteral("%1 state %2, expected %3")
          .arg(chip->text(), state, expectedStates.at(index));
    }
    if (chip->accessibleDescription() != descriptionForState(state)) {
      return QStringLiteral("%1 description %2").arg(chip->text(), chip->accessibleDescription());
    }
    if (chip->text() != QStringLiteral("Q%1").arg(index + 1)) {
      return QStringLiteral("label %1").arg(chip->text());
    }
    if (chip->accessibleName() != chip->text()) {
      return QStringLiteral("%1 accessible name %2").arg(chip->text(), chip->accessibleName());
    }
    if (chip->cursor().shape() != Qt::ArrowCursor) {
      return QStringLiteral("%1 cursor").arg(chip->text());
    }
    if (chip->focusPolicy() != Qt::NoFocus) {
      return QStringLiteral("%1 focus").arg(chip->text());
    }
    if (!chip->styleSheet().isEmpty()) {
      return QStringLiteral("%1 has an inline stylesheet").arg(chip->text());
    }
    if (!chip->findChildren<QAbstractAnimation*>().isEmpty()) {
      return QStringLiteral("%1 is animating").arg(chip->text());
    }
  }
  return {};
}

void settle(QWidget* widget) {
  if (!widget) return;
  widget->ensurePolished();
  if (widget->layout()) widget->layout()->activate();
  QApplication::sendPostedEvents();
}

void showPanel(QWidget* host, GameControls* controls, int width) {
  host->setObjectName(QStringLiteral("TaggingRightCol"));
  host->resize(width, 820);
  controls->setGeometry(0, 0, width, 820);
  host->show();
  settle(host);
  settle(controls);
  controls->setGeometry(0, 0, width, 820);
  settle(controls);
}

struct PaintedChip {
  QColor fill;
  QColor border;
  QColor text;
};

PaintedChip sampleChip(QLabel* chip) {
  PaintedChip painted;
  QImage image(qMax(1, chip->width()), qMax(1, chip->height()), QImage::Format_ARGB32);
  image.fill(Qt::white);
  chip->render(&image);

  const QRect topBand(2, 2, qMax(1, image.width() - 4), 4);
  QHash<QRgb, int> fillCounts;
  for (int y = topBand.top(); y <= topBand.bottom(); ++y) {
    for (int x = topBand.left(); x <= topBand.right(); ++x) {
      fillCounts[opaqueOnWhite(image.pixel(x, y)).rgb()] += 1;
    }
  }
  QRgb fillKey = 0;
  int fillCount = -1;
  for (auto iterator = fillCounts.cbegin(); iterator != fillCounts.cend(); ++iterator) {
    if (iterator.value() > fillCount) {
      fillCount = iterator.value();
      fillKey = iterator.key();
    }
  }
  painted.fill = QColor::fromRgb(fillKey);
  painted.border = opaqueOnWhite(image.pixel(image.width() / 2, 0));

  const QRect textBand(qMax(0, image.width() / 2 - 8), 8, 16, qMax(1, image.height() - 12));
  QColor textColor = painted.fill;
  double textDistance = -1.0;
  const double fillLuminance = relativeLuminance(painted.fill);
  for (int y = textBand.top(); y <= textBand.bottom() && y < image.height(); ++y) {
    for (int x = textBand.left(); x <= textBand.right() && x < image.width(); ++x) {
      const QColor color = opaqueOnWhite(image.pixel(x, y));
      const double distance = std::abs(relativeLuminance(color) - fillLuminance);
      if (distance > textDistance) {
        textDistance = distance;
        textColor = color;
      }
    }
  }
  painted.text = textColor;
  return painted;
}

QString contrastLine(const QString& label, const QColor& foreground, const QColor& background) {
  return QStringLiteral("%1 %2 on %3 = %4:1")
      .arg(label, foreground.name(QColor::HexRgb), background.name(QColor::HexRgb))
      .arg(contrastRatio(foreground, background), 0, 'f', 2);
}

}  // namespace

class QuarterChipTest : public QObject {
  Q_OBJECT

 private slots:
  void init();
  void cleanup();
  void chipsMoveFromEmptyToCurrentToComplete();
  void renderedStatesMeetContrastMinimums();
  void chipsSitBesideThePeriodButtonAtBothColumnWidths();

 private:
  QString previousStyleSheet_;
  AppLocale::Language previousLanguage_ = AppLocale::Language::English;
};

void QuarterChipTest::init() {
  previousStyleSheet_ = qApp->styleSheet();
  previousLanguage_ = AppLocale::currentLanguage();
  AppLocale::setLanguage(AppLocale::Language::English);
  const QString stylesheet = loadThemeStylesheet();
  QVERIFY2(!stylesheet.isEmpty(), "theme_light.qss did not load");
  qApp->setStyleSheet(stylesheet);
}

void QuarterChipTest::cleanup() {
  qApp->setStyleSheet(previousStyleSheet_);
  AppLocale::setLanguage(previousLanguage_);
}

void QuarterChipTest::chipsMoveFromEmptyToCurrentToComplete() {
  QWidget window;
  auto* layout = new QVBoxLayout(&window);
  auto* controls = new GameControls(&window);
  layout->addWidget(controls);
  TagSession session;
  controls->setTagSession(&session);
  qint64 playheadMs = 0;
  connect(controls, &GameControls::gameStartRequested, &session, [&session, &playheadMs]() {
    session.startGame(playheadMs);
  });
  connect(controls, &GameControls::nextQuarterRequested, &session, [&session, &playheadMs]() {
    playheadMs += 15 * 60 * 1000;
    session.advanceQuarter(playheadMs);
  });

  window.resize(500, 820);
  window.show();
  QVERIFY(QTest::qWaitForWindowExposed(&window));
  controls->setFocus(Qt::OtherFocusReason);
  settle(controls);

  const QList<QLabel*> chips = quarterChips(controls);
  QWidget* track = quarterTrack(controls);
  QVERIFY(track);
  QCOMPARE(chips.size(), 4);
  const QString emptyFailure = chipStateFailure(chips, statesForQuarter(-1, false));
  QVERIFY2(emptyFailure.isEmpty(), qPrintable(emptyFailure));
  QCOMPARE(track->accessibleName(), QStringLiteral("Quarter progress, not started"));
  QCOMPARE(track->cursor().shape(), Qt::ArrowCursor);

  QSignalSpy started(controls, &GameControls::gameStartRequested);
  QSignalSpy advanced(controls, &GameControls::nextQuarterRequested);
  QTest::mouseClick(chips.at(0), Qt::LeftButton);
  QTest::mouseClick(chips.at(2), Qt::LeftButton);
  QCOMPARE(started.count(), 0);
  QCOMPARE(advanced.count(), 0);
  QCOMPARE(session.quarterPhase(), TagSession::QuarterPhase::NotStarted);
  QCOMPARE(chips.at(0)->property("quarterState").toString(), QStringLiteral("empty"));

  QList<QRect> geometryAtFirstQuarter;
  QKeyEvent startQuarter(QEvent::KeyPress, Qt::Key_G, Qt::NoModifier, QStringLiteral("g"), false);
  QApplication::sendEvent(controls, &startQuarter);
  QCOMPARE(session.currentQuarterIndex(), 0);
  const QString firstQuarterFailure = chipStateFailure(chips, statesForQuarter(0, false));
  QVERIFY2(firstQuarterFailure.isEmpty(), qPrintable(firstQuarterFailure));
  QCOMPARE(track->accessibleName(), QStringLiteral("Quarter progress, Q1 of 4"));
  for (const QLabel* chip : chips) geometryAtFirstQuarter.append(chip->geometry());

  QTest::mouseClick(chips.at(1), Qt::LeftButton);
  QCOMPARE(session.currentQuarterIndex(), 0);
  QCOMPARE(chips.at(1)->property("quarterState").toString(), QStringLiteral("empty"));

  QPushButton* period = periodButton(controls);
  QVERIFY(period);
  period->click();
  QCOMPARE(session.currentQuarterIndex(), 1);
  const QString secondQuarterFailure = chipStateFailure(chips, statesForQuarter(1, false));
  QVERIFY2(secondQuarterFailure.isEmpty(), qPrintable(secondQuarterFailure));
  QCOMPARE(track->accessibleName(), QStringLiteral("Quarter progress, Q2 of 4"));
  for (int index = 0; index < chips.size(); ++index) {
    QCOMPARE(chips.at(index)->geometry(), geometryAtFirstQuarter.at(index));
    QCOMPARE(chips.at(index)->height(), kChipHeightPx);
  }

  period = periodButton(controls);
  QVERIFY(period);
  period->click();
  QCOMPARE(session.currentQuarterIndex(), 2);
  const QString thirdQuarterFailure = chipStateFailure(chips, statesForQuarter(2, false));
  QVERIFY2(thirdQuarterFailure.isEmpty(), qPrintable(thirdQuarterFailure));
  QCOMPARE(chips.at(0)->property("quarterState").toString(), QStringLiteral("complete"));
  QCOMPARE(chips.at(1)->property("quarterState").toString(), QStringLiteral("complete"));
  QCOMPARE(chips.at(2)->property("quarterState").toString(), QStringLiteral("current"));
  QCOMPARE(chips.at(3)->property("quarterState").toString(), QStringLiteral("empty"));

  period = periodButton(controls);
  QVERIFY(period);
  period->click();
  QCOMPARE(session.currentQuarterIndex(), 3);
  const QString fourthQuarterFailure = chipStateFailure(chips, statesForQuarter(3, false));
  QVERIFY2(fourthQuarterFailure.isEmpty(), qPrintable(fourthQuarterFailure));
  QCOMPARE(track->accessibleName(), QStringLiteral("Quarter progress, Q4 of 4"));

  period = periodButton(controls);
  QVERIFY(period);
  period->click();
  QCOMPARE(session.quarterPhase(), TagSession::QuarterPhase::GameEnded);
  const QString endedFailure = chipStateFailure(chips, statesForQuarter(-1, true));
  QVERIFY2(endedFailure.isEmpty(), qPrintable(endedFailure));
  QCOMPARE(track->accessibleName(), QStringLiteral("Quarter progress, game ended"));
  QVERIFY(!period->isEnabled());
  const int advancesBeforeIgnoredClick = advanced.count();
  period->click();
  QCOMPARE(advanced.count(), advancesBeforeIgnoredClick);
  QCOMPARE(session.quarterPhase(), TagSession::QuarterPhase::GameEnded);
}

void QuarterChipTest::renderedStatesMeetContrastMinimums() {
  const QString stylesheet = qApp->styleSheet();
  QVERIFY(!stylesheet.contains(QStringLiteral("quarterSegment\"]:hover")));
  QVERIFY(!stylesheet.contains(QStringLiteral("quarterSegment\"]:pressed")));

  QWidget host;
  auto* controls = new GameControls(&host);
  TagSession session;
  controls->setTagSession(&session);
  QVERIFY(session.startGame(0));
  QVERIFY(session.advanceQuarter(15 * 60 * 1000));
  QVERIFY(session.advanceQuarter(30 * 60 * 1000));
  QCOMPARE(session.currentQuarterIndex(), 2);

  showPanel(&host, controls, WorkWindow::kMaximumTaggingSidePanelWidthPx);
  const QList<QLabel*> chips = quarterChips(controls);
  QCOMPARE(chips.size(), 4);

  const struct ExpectedPaint {
    const char* state;
    QColor text;
    QColor fill;
    QColor border;
    QFont::Weight fontWeight;
  } expectedPaints[] = {
      {"complete", QColor(QStringLiteral("#fafafa")), QColor(QStringLiteral("#3f3f46")),
       QColor(QStringLiteral("#3f3f46")), QFont::Medium},
      {"complete", QColor(QStringLiteral("#fafafa")), QColor(QStringLiteral("#3f3f46")),
       QColor(QStringLiteral("#3f3f46")), QFont::Medium},
      {"current", QColor(QStringLiteral("#fafafa")), QColor(QStringLiteral("#18181b")),
       QColor(QStringLiteral("#18181b")), QFont::DemiBold},
      {"empty", QColor(QStringLiteral("#52525b")), QColor(QStringLiteral("#f4f4f5")),
       QColor(QStringLiteral("#71717a")), QFont::Normal},
  };

  const QColor panel(QStringLiteral("#ffffff"));
  for (int index = 0; index < 4; ++index) {
    QLabel* chip = chips.at(index);
    const ExpectedPaint& expected = expectedPaints[index];
    QCOMPARE(chip->property("quarterState").toString(), QString::fromLatin1(expected.state));
    QCOMPARE(chip->font().pixelSize(), 12);
    QCOMPARE(chip->font().weight(), expected.fontWeight);
    QCOMPARE(chip->alignment(), Qt::AlignCenter);

    const PaintedChip painted = sampleChip(chip);
    QVERIFY2(colorsNear(painted.fill, expected.fill, 8),
             qPrintable(QStringLiteral("%1 fill %2")
                            .arg(chip->text(), painted.fill.name(QColor::HexRgb))));
    QVERIFY2(colorsNear(painted.border, expected.border, 16),
             qPrintable(QStringLiteral("%1 border %2")
                            .arg(chip->text(), painted.border.name(QColor::HexRgb))));

    const double textRatio = contrastRatio(painted.text, painted.fill);
    const double borderRatio = contrastRatio(painted.border, panel);
    const QString report = contrastLine(chip->text() + QStringLiteral(" text"), painted.text, painted.fill) +
                           QStringLiteral("; ") +
                           contrastLine(chip->text() + QStringLiteral(" border"), painted.border, panel);
    QVERIFY2(textRatio + 0.05 >= kTextContrastMinimum, qPrintable(report));
    QVERIFY2(borderRatio + 0.05 >= kBorderContrastMinimum, qPrintable(report));
  }
}

void QuarterChipTest::chipsSitBesideThePeriodButtonAtBothColumnWidths() {
  const int widths[] = {GameControls::kMinimumPanelWidthPx, WorkWindow::kMaximumTaggingSidePanelWidthPx};
  int previousChipWidth = 0;
  for (int width : widths) {
    QWidget host;
    auto* controls = new GameControls(&host);
    TagSession session;
    session.setGameTeams(QStringLiteral("Universidad Católica"), QStringLiteral("Las Leonas"),
                         QStringLiteral("#0033A0"), QStringLiteral("#6FCFEB"));
    controls->setTagSession(&session);
    QVERIFY(session.startGame(2 * 60 * 1000));
    showPanel(&host, controls, width);

    QVERIFY2(controls->width() <= width + 2, qPrintable(QString::number(controls->width())));
    const QList<QLabel*> chips = quarterChips(controls);
    QCOMPARE(chips.size(), 4);
    QPushButton* period = periodButton(controls);
    QVERIFY(period);
    QCOMPARE(period->cursor().shape(), Qt::PointingHandCursor);

    const int chipMidY =
        chips.at(0)->mapTo(controls, QPoint(0, chips.at(0)->height() / 2)).y();
    const int buttonMidY = period->mapTo(controls, QPoint(0, period->height() / 2)).y();
    QVERIFY2(qAbs(chipMidY - buttonMidY) <= 1,
             qPrintable(QStringLiteral("chip %1 button %2").arg(chipMidY).arg(buttonMidY)));

    int sharedWidth = -1;
    for (int index = 0; index < chips.size(); ++index) {
      const QLabel* chip = chips.at(index);
      QCOMPARE(chip->height(), kChipHeightPx);
      QVERIFY(chip->minimumWidth() >= kChipMinimumWidthPx);
      QVERIFY2(chip->width() >= kChipMinimumWidthPx,
               qPrintable(QStringLiteral("%1 width %2").arg(chip->text()).arg(chip->width())));
      QVERIFY(chip->mapTo(&host, QPoint(chip->width() - 1, 0)).x() < host.width());
      if (sharedWidth < 0) sharedWidth = chip->width();
      QVERIFY(qAbs(chip->width() - sharedWidth) <= 1);
      if (index > 0) {
        const QLabel* previous = chips.at(index - 1);
        const int gap = chip->x() - (previous->x() + previous->width());
        QCOMPARE(gap, kChipGapPx);
      }
    }
    QCOMPARE(quarterTrack(controls)->layout()->spacing(), GameControlGrid::kKeyGapPx);
    if (previousChipWidth > 0) QVERIFY(sharedWidth > previousChipWidth);
    previousChipWidth = sharedWidth;
  }

  QWidget host;
  auto* controls = new GameControls(&host);
  TagSession session;
  controls->setTagSession(&session);
  AppLocale::setLanguage(AppLocale::Language::Spanish);
  controls->applyUiLanguage();
  showPanel(&host, controls, GameControls::kMinimumPanelWidthPx);
  QVERIFY2(controls->width() <= GameControls::kMinimumPanelWidthPx + 2,
           qPrintable(QString::number(controls->width())));
  for (const QLabel* chip : quarterChips(controls)) {
    QVERIFY2(chip->width() >= kChipMinimumWidthPx,
             qPrintable(QStringLiteral("%1 width %2").arg(chip->text()).arg(chip->width())));
    QVERIFY(chip->mapTo(&host, QPoint(chip->width() - 1, 0)).x() < host.width());
  }
}

int runQuarterChipTests(int argc, char** argv) {
  QuarterChipTest tests;
  return QTest::qExec(&tests, argc, argv);
}

#include "QuarterChipTests.moc"
