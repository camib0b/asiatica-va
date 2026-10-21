#include "AppLocale.h"
#include "GameControls.h"
#include "TagToastOverlay.h"

#include <QApplication>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>

namespace {

QPushButton* buttonNamed(QWidget* root, const char* eventName) {
  if (!root) return nullptr;
  for (QPushButton* button : root->findChildren<QPushButton*>()) {
    if (button->property("gameEventName").toString() == QLatin1String(eventName)) {
      return button;
    }
  }
  return nullptr;
}

QPushButton* buttonWithTeamLabel(QWidget* root, const QString& fullName) {
  if (!root) return nullptr;
  for (QPushButton* button : root->findChildren<QPushButton*>()) {
    if (button->toolTip() == fullName || button->text() == fullName) return button;
  }
  return nullptr;
}

void settleLayout(QWidget* widget) {
  if (!widget) return;
  widget->ensurePolished();
  if (widget->layout()) widget->layout()->activate();
  QApplication::processEvents();
}

int mappedX(QWidget* widget, QWidget* ancestor) {
  return widget->mapTo(ancestor, QPoint(0, 0)).x();
}

}  // namespace

class TaggingToastTest : public QObject {
  Q_OBJECT

private slots:
  void chainStaysUpUntilTheLastAnswerThenPasses();
  void escapeClearsTheToastImmediately();
  void simpleEventPassesQuickly();
  void appendBeforeAChainDoesNothing();
  void newEventReplacesTheOpenChain();
  void suspendKeepsTheChainAndResumeShowsIt();
  void plainTextDoesNotInterpretMarkup();
  void longChainStaysInsideTheVideoFrame();
  void toastDoesNotTakeFocusOrMouse();
  void escapeKeepsTheMainTag();
  void mouseAndKeyboardBothPublishTheShortcut();
  void shotChainPublishesEachAnswer();
  void keyboardRowsFollowQwertyOffsets();
  void longTeamNamesDoNotWidenThePanel();
};

void TaggingToastTest::chainStaysUpUntilTheLastAnswerThenPasses() {
  TagToastOverlay toast;
  const QColor team(QStringLiteral("#1d4ed8"));
  toast.beginEvent(QStringLiteral("D"), QStringLiteral("Shot"), team, true);
  QVERIFY(toast.isPresented());
  QVERIFY(toast.isChainOpen());
  QCOMPARE(toast.shortcutKey(), QStringLiteral("D"));
  QCOMPARE(toast.chainText(), QStringLiteral("Shot"));
  QCOMPARE(toast.displayLine(), QStringLiteral("D  Shot"));
  QCOMPARE(toast.teamColor(), team);

  toast.appendFollowUp(QStringLiteral("On target"), true);
  QVERIFY(toast.isChainOpen());
  QCOMPARE(toast.chainText(), QStringLiteral("Shot") + TagToastOverlay::chainSeparator() +
                                  QStringLiteral("On target"));

  QTest::qWait(TagToastOverlay::kPassingDismissMilliseconds + 200);
  QVERIFY(toast.isPresented());

  toast.appendFollowUp(QStringLiteral("Saved"), false);
  QVERIFY(toast.isPresented());
  QVERIFY(!toast.isChainOpen());
  QCOMPARE(toast.chainText(), QStringLiteral("Shot") + TagToastOverlay::chainSeparator() +
                                  QStringLiteral("On target") + TagToastOverlay::chainSeparator() +
                                  QStringLiteral("Saved"));
  QCOMPARE(toast.displayLine(), QStringLiteral("D  Shot") + TagToastOverlay::chainSeparator() +
                                    QStringLiteral("On target") + TagToastOverlay::chainSeparator() +
                                    QStringLiteral("Saved"));

  QTest::qWait(TagToastOverlay::kChainCompleteDismissMilliseconds + 250);
  QVERIFY(!toast.isPresented());
}

void TaggingToastTest::escapeClearsTheToastImmediately() {
  TagToastOverlay toast;
  toast.beginEvent(QStringLiteral("D"), QStringLiteral("Shot"), QColor(QStringLiteral("#b91c1c")), true);
  toast.appendFollowUp(QStringLiteral("On target"), true);
  toast.dismissImmediately();
  QVERIFY(!toast.isPresented());
  QVERIFY(!toast.isChainOpen());
  QCOMPARE(toast.chainText(), QString());
  QCOMPARE(toast.shortcutKey(), QString());
  QVERIFY(!toast.isVisible());

  QTest::qWait(TagToastOverlay::kChainCompleteDismissMilliseconds + 100);
  QVERIFY(!toast.isPresented());
  QCOMPARE(toast.chainText(), QString());
}

void TaggingToastTest::simpleEventPassesQuickly() {
  TagToastOverlay toast;
  toast.beginEvent(QStringLiteral("W"), QStringLiteral("50-yd"), QColor(QStringLiteral("#fafafa")), false);
  QVERIFY(toast.isPresented());
  QVERIFY(!toast.isChainOpen());
  QCOMPARE(toast.displayLine(), QStringLiteral("W  50-yd"));
  QTest::qWait(TagToastOverlay::kPassingDismissMilliseconds + 250);
  QVERIFY(!toast.isPresented());
}

void TaggingToastTest::appendBeforeAChainDoesNothing() {
  TagToastOverlay toast;
  toast.appendFollowUp(QStringLiteral("Saved"), false);
  QVERIFY(!toast.isPresented());
  QCOMPARE(toast.chainText(), QString());
}

void TaggingToastTest::newEventReplacesTheOpenChain() {
  TagToastOverlay toast;
  const QColor home(QStringLiteral("#1d4ed8"));
  const QColor away(QStringLiteral("#fde68a"));
  toast.beginEvent(QStringLiteral("D"), QStringLiteral("Shot"), home, true);
  toast.appendFollowUp(QStringLiteral("On target"), true);
  toast.beginEvent(QStringLiteral("F"), QStringLiteral("Goal"), away, false);
  QCOMPARE(toast.shortcutKey(), QStringLiteral("F"));
  QCOMPARE(toast.chainText(), QStringLiteral("Goal"));
  QCOMPARE(toast.teamColor(), away);
  QVERIFY(toast.isPresented());
  QVERIFY(!toast.isChainOpen());
}

void TaggingToastTest::suspendKeepsTheChainAndResumeShowsIt() {
  TagToastOverlay toast;
  toast.beginEvent(QStringLiteral("D"), QStringLiteral("Shot"), QColor(QStringLiteral("#1d4ed8")), true);
  toast.appendFollowUp(QStringLiteral("On target"), true);
  toast.suspend();
  QVERIFY(!toast.isPresented());
  QVERIFY(!toast.isVisible());
  QVERIFY(toast.isChainOpen());
  QCOMPARE(toast.chainText(), QStringLiteral("Shot") + TagToastOverlay::chainSeparator() +
                                  QStringLiteral("On target"));
  toast.resumeOpenChain();
  QVERIFY(toast.isPresented());
  QCOMPARE(toast.shortcutKey(), QStringLiteral("D"));
  toast.resumeOpenChain();
  QVERIFY(toast.isPresented());
}

void TaggingToastTest::plainTextDoesNotInterpretMarkup() {
  TagToastOverlay toast;
  const QString literal = QStringLiteral("<b>Shot</b> &amp; **goal**");
  toast.beginEvent(QStringLiteral("D"), literal, QColor(QStringLiteral("#18181b")), false);
  QLabel* chain = toast.findChild<QLabel*>(QStringLiteral("TagToastChainLabel"));
  QVERIFY(chain);
  QCOMPARE(chain->textFormat(), Qt::PlainText);
  QCOMPARE(chain->text(), literal);
  QCOMPARE(chain->textInteractionFlags(), Qt::NoTextInteraction);
}

void TaggingToastTest::longChainStaysInsideTheVideoFrame() {
  QWidget videoFrame;
  videoFrame.resize(320, 180);
  videoFrame.show();
  settleLayout(&videoFrame);

  TagToastOverlay toast;
  toast.trackVideoFrame(&videoFrame);
  const QString longStep = QStringLiteral("Ingreso al círculo por la banda izquierda con arrastre");
  toast.beginEvent(QStringLiteral("A"), QStringLiteral("Aleksandra Wiśniewska-Kowalczyk"),
                   QColor(QStringLiteral("#fde68a")), true);
  toast.appendFollowUp(longStep, true);
  toast.appendFollowUp(QStringLiteral("Đặng Thị Ngọc Hân"), false);
  settleLayout(&toast);

  QVERIFY(toast.isPresented());
  QVERIFY2(toast.width() > 0 && toast.width() <= videoFrame.width(),
           qPrintable(QStringLiteral("toast %1 frame %2").arg(toast.width()).arg(videoFrame.width())));
  QVERIFY(toast.height() > 0);
  QVERIFY(toast.height() <= TagToastOverlay::kMaximumBodyHeightPixels);
  const QPoint frameOrigin = videoFrame.mapToGlobal(QPoint(0, 0));
  QCOMPARE(toast.pos(), QPoint((videoFrame.width() - toast.width()) / 2, TagToastOverlay::kTopMarginPixels) +
                            frameOrigin);
  QVERIFY(toast.pos().x() >= frameOrigin.x());
  QVERIFY(toast.pos().x() + toast.width() <= frameOrigin.x() + videoFrame.width());

  QLabel* chain = toast.findChild<QLabel*>(QStringLiteral("TagToastChainLabel"));
  QVERIFY(chain);
  QVERIFY(chain->width() < videoFrame.width());
  QVERIFY(chain->text().contains(QStringLiteral("Wiśniewska")));
  QVERIFY(chain->text().contains(QStringLiteral("Ngọc")));
  const int neededHeight = chain->heightForWidth(chain->width());
  QVERIFY2(neededHeight <= chain->height(),
           qPrintable(QStringLiteral("needed %1 label %2 toast %3")
                          .arg(neededHeight)
                          .arg(chain->height())
                          .arg(toast.height())));
}

void TaggingToastTest::toastDoesNotTakeFocusOrMouse() {
  QLineEdit editor;
  editor.show();
  editor.setFocus(Qt::OtherFocusReason);
  // Offscreen often leaves focusWidget null. The toast must not change it either way.
  QWidget* focusBefore = QApplication::focusWidget();

  TagToastOverlay toast;
  toast.beginEvent(QStringLiteral("X"), QStringLiteral("☆"), QColor(QStringLiteral("#18181b")), false);
  QCOMPARE(QApplication::focusWidget(), focusBefore);
  QVERIFY(QApplication::focusWidget() != static_cast<QWidget*>(&toast));
  QVERIFY(!toast.hasFocus());
  QCOMPARE(toast.focusPolicy(), Qt::NoFocus);
  QVERIFY(toast.testAttribute(Qt::WA_TransparentForMouseEvents));
  QVERIFY(toast.testAttribute(Qt::WA_ShowWithoutActivating));
  QVERIFY(toast.windowFlags().testFlag(Qt::WindowDoesNotAcceptFocus));
  QVERIFY(toast.windowFlags().testFlag(Qt::WindowTransparentForInput));
  QVERIFY(toast.windowFlags().testFlag(Qt::Tool));
}

void TaggingToastTest::escapeKeepsTheMainTag() {
  GameControls controls;
  controls.setTeamSide(TagSession::TeamSide::Home);
  controls.resize(420, 760);
  controls.show();
  settleLayout(&controls);

  QSignalSpy advanced(&controls, &GameControls::tagSequenceAdvanced);
  QSignalSpy dismissed(&controls, &GameControls::tagSequenceDismissed);
  QSignalSpy committed(&controls, &GameControls::tagCommitted);

  QTest::keyClick(&controls, Qt::Key_D);
  QCOMPARE(advanced.count(), 1);
  QCOMPARE(advanced.at(0).at(0).toString(), QStringLiteral("D"));
  QCOMPARE(advanced.at(0).at(1).toString(), QStringLiteral("Shot"));
  QCOMPARE(advanced.at(0).at(3).toBool(), true);
  QCOMPARE(advanced.at(0).at(4).toBool(), true);
  QCOMPARE(committed.count(), 0);

  QTest::keyClick(&controls, Qt::Key_1);
  QCOMPARE(advanced.count(), 2);
  QCOMPARE(advanced.at(1).at(1).toString(), QStringLiteral("On target"));
  QCOMPARE(advanced.at(1).at(3).toBool(), true);
  QCOMPARE(advanced.at(1).at(4).toBool(), false);

  QTest::keyClick(&controls, Qt::Key_Escape);
  QCOMPARE(dismissed.count(), 1);
  QCOMPARE(committed.count(), 1);
  QCOMPARE(committed.at(0).at(0).toString(), QStringLiteral("Shot"));
  QCOMPARE(committed.at(0).at(1).toString(), QString());
}

void TaggingToastTest::mouseAndKeyboardBothPublishTheShortcut() {
  GameControls controls;
  controls.setSessionTeamNames(QStringLiteral("Home"), QStringLiteral("Away"), QStringLiteral("#ff0000"),
                              QStringLiteral("#0000ff"));
  controls.setTeamSide(TagSession::TeamSide::Home);
  controls.resize(420, 760);
  controls.show();
  settleLayout(&controls);

  QPushButton* shot = buttonNamed(&controls, "Shot");
  QVERIFY(shot);
  QCOMPARE(shot->property("shortcutHint").toString(), QStringLiteral("D"));

  QSignalSpy advanced(&controls, &GameControls::tagSequenceAdvanced);
  QTest::mouseClick(shot, Qt::LeftButton);
  QCOMPARE(advanced.count(), 1);
  QCOMPARE(advanced.at(0).at(0).toString(), QStringLiteral("D"));
  QCOMPARE(advanced.at(0).at(1).toString(), QStringLiteral("Shot"));
  QCOMPARE(advanced.at(0).at(2).value<QColor>(), QColor(QStringLiteral("#ff0000")));

  QTest::keyClick(&controls, Qt::Key_Escape);
  advanced.clear();
  QTest::keyClick(&controls, Qt::Key_D);
  QCOMPARE(advanced.count(), 1);
  QCOMPARE(advanced.at(0).at(0).toString(), QStringLiteral("D"));
  QCOMPARE(advanced.at(0).at(2).value<QColor>(), QColor(QStringLiteral("#ff0000")));
}

void TaggingToastTest::shotChainPublishesEachAnswer() {
  GameControls controls;
  controls.setTeamSide(TagSession::TeamSide::Away);
  controls.resize(420, 760);
  controls.show();
  settleLayout(&controls);

  QSignalSpy advanced(&controls, &GameControls::tagSequenceAdvanced);
  QSignalSpy committed(&controls, &GameControls::tagCommitted);

  QTest::keyClick(&controls, Qt::Key_D);
  QTest::keyClick(&controls, Qt::Key_1);
  QTest::keyClick(&controls, Qt::Key_2);
  QCOMPARE(advanced.count(), 3);
  QCOMPARE(advanced.at(2).at(1).toString(), QStringLiteral("Saved"));
  QCOMPARE(advanced.at(2).at(3).toBool(), false);
  QCOMPARE(advanced.at(2).at(4).toBool(), false);
  QCOMPARE(committed.count(), 1);
  QCOMPARE(committed.at(0).at(0).toString(), QStringLiteral("Shot"));
  QCOMPARE(committed.at(0).at(1).toString(), QStringLiteral("On target \u2192 Saved"));
}

void TaggingToastTest::keyboardRowsFollowQwertyOffsets() {
  GameControls controls;
  controls.resize(420, 780);
  controls.show();
  settleLayout(&controls);
  controls.resize(420, 780);
  settleLayout(&controls);

  QPushButton* top = buttonNamed(&controls, "16-yd");
  QPushButton* home = buttonNamed(&controls, "Circle Entry");
  QPushButton* bottom = buttonNamed(&controls, "Pass");
  QPushButton* penaltyStroke = buttonNamed(&controls, "PS");
  QPushButton* shootout = buttonNamed(&controls, "S.O.");
  QPushButton* special = buttonNamed(&controls, "Special");
  QVERIFY(top && home && bottom && penaltyStroke && shootout && special);
  QVERIFY(top->width() > 20);

  const int narrowHomeShift = mappedX(home, &controls) - mappedX(top, &controls);
  const int narrowBottomShift = mappedX(bottom, &controls) - mappedX(top, &controls);
  QVERIFY(narrowHomeShift > top->width() / 8);
  QVERIFY(narrowHomeShift < top->width() / 2);
  QVERIFY(narrowBottomShift > top->width() / 2);
  QVERIFY(narrowBottomShift < top->width());
  QVERIFY(narrowBottomShift > narrowHomeShift);
  QVERIFY(qAbs(top->width() - bottom->width()) <= 2);
  QVERIFY(qAbs(top->width() - penaltyStroke->width()) <= 2);
  QVERIFY(qAbs(mappedX(bottom, &controls) - mappedX(penaltyStroke, &controls)) <= 3);
  QVERIFY(qAbs(mappedX(special, &controls) - mappedX(shootout, &controls)) <= 3);

  controls.resize(680, 780);
  settleLayout(&controls);
  const int wideHomeShift = mappedX(home, &controls) - mappedX(top, &controls);
  QVERIFY(wideHomeShift > narrowHomeShift);

  controls.resize(GameControls::kMinimumPanelWidthPx, 820);
  settleLayout(&controls);
  QPushButton* turnover = buttonNamed(&controls, "Turnover");
  QVERIFY(turnover);
  QLabel* turnoverTitle = nullptr;
  QLabel* circleTitle = nullptr;
  for (QLabel* label : turnover->findChildren<QLabel*>()) {
    if (label->property("gameEventName").toString() == QLatin1String("Turnover")) turnoverTitle = label;
  }
  for (QLabel* label : home->findChildren<QLabel*>()) {
    if (label->property("gameEventName").toString() == QLatin1String("Circle Entry")) circleTitle = label;
  }
  QVERIFY(turnoverTitle);
  QVERIFY(circleTitle);
  const auto shownFits = [](const QLabel* title, const QString& fullText) {
    QString stem = title->text();
    stem.remove(QChar(0x2026));
    stem.remove(QLatin1String("..."));
    if (title->text() == fullText) return true;
    return fullText.startsWith(stem) &&
           title->fontMetrics().horizontalAdvance(title->text()) <= title->width() + 1;
  };
  QVERIFY2(shownFits(circleTitle, AppLocale::trEvent(QStringLiteral("Circle Entry"))),
           qPrintable(circleTitle->text()));
  QVERIFY2(shownFits(turnoverTitle, AppLocale::trEvent(QStringLiteral("Turnover"))),
           qPrintable(turnoverTitle->text()));
  QVERIFY(turnoverTitle->fontMetrics().horizontalAdvance(turnoverTitle->text()) <=
          turnoverTitle->width() + 1);
  QVERIFY(turnover->mapTo(&controls, QPoint(turnover->width() - 1, 0)).x() < controls.width());
  QPushButton* card = buttonNamed(&controls, "Card");
  QVERIFY(card);
  QVERIFY(card->mapTo(&controls, QPoint(card->width() - 1, 0)).x() < controls.width());
}

void TaggingToastTest::longTeamNamesDoNotWidenThePanel() {
  const QString homeName = QStringLiteral("Aleksandra Wiśniewska-Kowalczyk");
  const QString awayName = QStringLiteral("Đặng Thị Ngọc Hân");
  GameControls controls;
  controls.resize(GameControls::kMinimumPanelWidthPx, 820);
  controls.show();
  settleLayout(&controls);
  controls.setSessionTeamNames(homeName, awayName, QStringLiteral("#fde68a"), QStringLiteral("#1d4ed8"));
  controls.resize(GameControls::kMinimumPanelWidthPx, 820);
  settleLayout(&controls);

  QVERIFY2(controls.width() <= GameControls::kMinimumPanelWidthPx + 2,
           qPrintable(QString::number(controls.width())));
  QPushButton* home = buttonWithTeamLabel(&controls, homeName);
  QPushButton* away = buttonWithTeamLabel(&controls, awayName);
  QVERIFY(home);
  QVERIFY(away);
  QVERIFY(home->text().size() < homeName.size());
  QCOMPARE(home->toolTip(), homeName);
  QVERIFY(away->text().contains(QStringLiteral("Đặng")) || away->toolTip() == awayName);
  QVERIFY(home->width() + away->width() <= controls.width());
}

int runTaggingToastTests(int argc, char** argv) {
  TaggingToastTest tests;
  return QTest::qExec(&tests, argc, argv);
}

#include "TaggingToastTests.moc"
