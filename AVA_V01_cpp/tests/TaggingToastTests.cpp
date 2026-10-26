#include "AppLocale.h"
#include "EventCatalog.h"
#include "GameControls.h"
#include "TagSession.h"
#include "TagToastOverlay.h"
#include "TextFit.h"
#include "WorkWindow.h"

#include <QApplication>
#include <QFile>
#include <QFontMetrics>
#include <QHash>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QSet>
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

bool textIsWellFormed(const QString& text) {
  const int count = text.size();
  for (int index = 0; index < count; ++index) {
    const QChar character = text.at(index);
    if (character.isHighSurrogate()) {
      if (index + 1 >= count || !text.at(index + 1).isLowSurrogate()) return false;
      ++index;
      continue;
    }
    if (character.isLowSurrogate()) return false;
  }
  return true;
}

QLabel* stackedTitle(const QPushButton* button) {
  if (!button) return nullptr;
  for (QLabel* label : button->findChildren<QLabel*>(Qt::FindDirectChildrenOnly)) {
    if (!label->property("gameEventName").toString().isEmpty() || label->property("fullLabel").isValid()) {
      return label;
    }
  }
  return nullptr;
}

QLabel* stackedShortcut(const QPushButton* button) {
  if (!button) return nullptr;
  for (QLabel* label : button->findChildren<QLabel*>(Qt::FindDirectChildrenOnly)) {
    if (label->property("gameEventName").toString().isEmpty() && !label->property("fullLabel").isValid()) {
      return label;
    }
  }
  return nullptr;
}

QString labelFitFailure(const QLabel* label, int maximumLines) {
  if (!label) return QStringLiteral("missing label");
  if (!textIsWellFormed(label->text())) return QStringLiteral("broken surrogate in ") + label->text();
  const QRect box = label->contentsRect();
  if (box.width() <= 0 || box.height() <= 0) {
    return QStringLiteral("empty text box %1x%2").arg(label->width()).arg(label->height());
  }
  const QStringList lines = label->text().split(QLatin1Char('\n'));
  if (lines.size() > maximumLines) return QStringLiteral("too many lines: ") + label->text();
  const QFontMetrics metrics = label->fontMetrics();
  const int lineBudget = metrics.lineSpacing() * maximumLines;
  const QRect needed = metrics.boundingRect(QRect(0, 0, box.width(), lineBudget + metrics.lineSpacing()),
                                            Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextWordWrap,
                                            label->text());
  if (needed.width() > box.width() + 1) {
    return QStringLiteral("wider than box: ") + label->text();
  }
  if (needed.height() > lineBudget + 1) {
    return QStringLiteral("uses more than %1 lines: %2").arg(maximumLines).arg(label->text());
  }
  if (needed.height() > box.height() + 1) {
    return QStringLiteral("text taller than box (%1 > %2): %3")
        .arg(needed.height())
        .arg(box.height())
        .arg(label->text());
  }
  return {};
}

QString fittedTitleFailure(const QLabel* title, const QString& fullText, int maximumLines) {
  if (!title) return QStringLiteral("missing title");
  if (title->property("fullLabel").toString() != fullText) {
    return QStringLiteral("fullLabel \"%1\" != \"%2\"")
        .arg(title->property("fullLabel").toString(), fullText);
  }
  const QString fitFailure = labelFitFailure(title, maximumLines);
  if (!fitFailure.isEmpty()) return fitFailure;
  const QString collapsed = QString(title->text()).replace(QLatin1Char('\n'), QLatin1Char(' '));
  const bool showsFullText = title->text() == fullText || collapsed == fullText;
  if (showsFullText) {
    if (!title->toolTip().isEmpty()) return QStringLiteral("tooltip set for a full label");
    if (title->parentWidget() && !title->parentWidget()->toolTip().isEmpty()) {
      return QStringLiteral("button tooltip set for a full label");
    }
    return {};
  }
  if (!title->text().contains(QChar(0x2026))) {
    return QStringLiteral("elided text has no ellipsis: ") + title->text();
  }
  if (title->toolTip() != fullText) return QStringLiteral("tooltip is not the full label");
  return {};
}

class PanelThemeGuard {
 public:
  PanelThemeGuard()
      : previousLanguage_(AppLocale::currentLanguage()), previousStyleSheet_(qApp->styleSheet()) {
    QFile themeFile(QStringLiteral(AVA_THEME_QSS_PATH));
    loaded_ = themeFile.open(QIODevice::ReadOnly | QIODevice::Text);
    if (loaded_) qApp->setStyleSheet(QString::fromUtf8(themeFile.readAll()));
  }

  ~PanelThemeGuard() {
    qApp->setStyleSheet(previousStyleSheet_);
    AppLocale::setLanguage(previousLanguage_);
    qunsetenv("AVA_BREAK_UI");
  }

  bool loaded() const { return loaded_; }

 private:
  AppLocale::Language previousLanguage_;
  QString previousStyleSheet_;
  bool loaded_ = false;
};

void showTaggingPanel(QWidget* host, GameControls* controls, int width, int height) {
  host->setObjectName(QStringLiteral("TaggingRightCol"));
  host->resize(width, height);
  controls->setGeometry(0, 0, width, height);
  host->show();
  settleLayout(host);
  settleLayout(controls);
  controls->setGeometry(0, 0, width, height);
  settleLayout(controls);
}

QList<QPushButton*> visibleFollowUpButtons(QWidget* root) {
  QList<QPushButton*> buttons;
  if (!root) return buttons;
  for (QPushButton* button : root->findChildren<QPushButton*>()) {
    if (button->property("gameEventKey").toString().isEmpty() || !button->isVisible()) continue;
    buttons.append(button);
  }
  return buttons;
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
  void fitButtonTitleKeepsWordsInsideTheLine();
  void spanishFollowUpLabelsFitAtPanelExtremes();
  void renamedLabelsKeepBadgesAndDoNotClip();
  void breakUiFixturesCoverFollowUpExtremes();
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
    if (!title) return false;
    const QRect box = title->contentsRect();
    if (box.width() <= 0 || box.height() <= 0) return false;
    const QStringList lines = title->text().split(QLatin1Char('\n'));
    if (lines.size() > 2) return false;
    const QFontMetrics metrics = title->fontMetrics();
    const QRect needed = metrics.boundingRect(QRect(0, 0, box.width(), box.height()),
                                              Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextWordWrap,
                                              title->text());
    if (needed.width() > box.width() + 1 || needed.height() > box.height() + 1) return false;
    if (title->text() == fullText) return true;
    const QString collapsed = QString(title->text()).replace(QLatin1Char('\n'), QLatin1Char(' '));
    if (collapsed == fullText) return true;
    return title->text().contains(QChar(0x2026)) && title->toolTip() == fullText;
  };
  QVERIFY2(shownFits(circleTitle, AppLocale::trEvent(QStringLiteral("Circle Entry"))),
           qPrintable(circleTitle->text()));
  QVERIFY2(shownFits(turnoverTitle, AppLocale::trEvent(QStringLiteral("Turnover"))),
           qPrintable(turnoverTitle->text()));
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

void TaggingToastTest::fitButtonTitleKeepsWordsInsideTheLine() {
  const QFontMetrics metrics(QApplication::font());
  const QString interception = QStringLiteral("Intercepción");
  const int fullWidth = metrics.horizontalAdvance(interception);
  QVERIFY(fullWidth > 8);

  const Style::FittedButtonTitle tooNarrow = Style::fitButtonTitle(metrics, interception, fullWidth - 1, 2);
  QVERIFY(tooNarrow.elided);
  QVERIFY(tooNarrow.shown.contains(QChar(0x2026)));
  QVERIFY(textIsWellFormed(tooNarrow.shown));
  QVERIFY(tooNarrow.shown.split(QLatin1Char('\n')).size() <= 2);
  for (const QString& line : tooNarrow.shown.split(QLatin1Char('\n'))) {
    QVERIFY(metrics.horizontalAdvance(line) <= fullWidth - 1);
  }

  const Style::FittedButtonTitle wideEnough = Style::fitButtonTitle(metrics, interception, fullWidth, 2);
  QVERIFY(!wideEnough.elided);
  QCOMPARE(wideEnough.shown, interception);

  const QString letter = QStringLiteral("J");
  const Style::FittedButtonTitle oneLetter = Style::fitButtonTitle(metrics, letter, fullWidth, 2);
  QVERIFY(!oneLetter.elided);
  QCOMPARE(oneLetter.shown, letter);

  const QString emojiName = QStringLiteral("🦊 Fox");
  const Style::FittedButtonTitle narrowEmoji = Style::fitButtonTitle(metrics, emojiName, 1, 1);
  QVERIFY(textIsWellFormed(narrowEmoji.shown));
  const Style::FittedButtonTitle wideEmoji =
      Style::fitButtonTitle(metrics, emojiName, metrics.horizontalAdvance(emojiName), 1);
  QVERIFY(!wideEmoji.elided);
  QCOMPARE(wideEmoji.shown, emojiName);

  const QString threeLines = QStringLiteral("Line one\nLine two\nLine three");
  const Style::FittedButtonTitle clamped = Style::fitButtonTitle(metrics, threeLines, 400, 2);
  QVERIFY(clamped.shown.split(QLatin1Char('\n')).size() <= 2);
  QVERIFY(textIsWellFormed(clamped.shown));

  const auto flexibleLabelsFit = [&](const QStringList& labels, int availableWidth, int columnCount) {
    const int gaps = 8 * qMax(0, columnCount - 1);
    const int textWidth = columnCount <= 0 ? 0 : (availableWidth - gaps) / columnCount - 4;
    const int singleColumnWidth = availableWidth - 4;
    for (const QString& label : labels) {
      const bool fitsOnOneColumn = !Style::fitButtonTitle(metrics, label, singleColumnWidth, 2).elided;
      if (!fitsOnOneColumn) continue;
      if (Style::fitButtonTitle(metrics, label, textWidth, 2).elided) return false;
    }
    return true;
  };

  const QStringList spanishFollowUps = {interception, QStringLiteral("Quite"), QStringLiteral("Presión"),
                                        QStringLiteral("Error")};
  const int spanishColumns = Style::fittingColumnCount(255, 4, 4, 8, metrics, spanishFollowUps, 2);
  QVERIFY(spanishColumns >= 1 && spanishColumns <= 4);
  QVERIFY(flexibleLabelsFit(spanishFollowUps, 255, spanishColumns));
  if (spanishColumns < 4) QVERIFY(!flexibleLabelsFit(spanishFollowUps, 255, spanishColumns + 1));

  const QString unboundedWord(80, QLatin1Char('W'));
  const QStringList mixed = {unboundedWord, QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")};
  QVERIFY(Style::fitButtonTitle(metrics, unboundedWord, 220 - 4, 2).elided);
  const int mixedColumns = Style::fittingColumnCount(220, 4, 4, 8, metrics, mixed, 2);
  QVERIFY(flexibleLabelsFit(mixed, 220, mixedColumns));
  QCOMPARE(mixedColumns, 4);
}

void TaggingToastTest::spanishFollowUpLabelsFitAtPanelExtremes() {
  PanelThemeGuard theme;
  QVERIFY(theme.loaded());
  AppLocale::setLanguage(AppLocale::Language::Spanish);

  QWidget host;
  GameControls controls(&host);
  QVERIFY(controls.findChild<QWidget*>(QStringLiteral("TaggingBreakUiToggle")) == nullptr);

  const auto checkWidth = [&](int panelWidth) {
    showTaggingPanel(&host, &controls, panelWidth, 900);
    controls.setTeamSide(TagSession::TeamSide::Home);
    QPushButton* turnover = buttonNamed(&controls, "Turnover");
    QVERIFY(turnover);
    turnover->click();
    settleLayout(&controls);

    QVERIFY2(controls.width() <= panelWidth + 2, qPrintable(QString::number(controls.width())));
    QVERIFY(controls.width() >= GameControls::kMinimumPanelWidthPx);

    const QList<QPushButton*> followUps = visibleFollowUpButtons(&controls);
    QCOMPARE(followUps.size(), 4);
    QPushButton* interceptionButton = nullptr;
    QSet<int> followUpX;
    QHash<int, int> shortcutYByButtonY;
    for (QPushButton* button : followUps) {
      QVERIFY(button->mapTo(&controls, QPoint(button->width() - 1, 0)).x() < controls.width());
      followUpX.insert(button->mapTo(&controls, QPoint(0, 0)).x());
      QLabel* title = stackedTitle(button);
      const QString fullText = title ? title->property("fullLabel").toString() : QString();
      const QString failure = fittedTitleFailure(title, fullText, 2);
      QVERIFY2(failure.isEmpty(), qPrintable(failure + QStringLiteral(" / ") + fullText));
      QLabel* shortcut = stackedShortcut(button);
      QVERIFY(shortcut);
      QVERIFY(button->rect().contains(shortcut->geometry()));
      QVERIFY(shortcut->fontMetrics().horizontalAdvance(shortcut->text()) <= shortcut->width() + 1);
      const int buttonY = button->mapTo(&controls, QPoint(0, 0)).y();
      const int shortcutY = shortcut->mapTo(&controls, QPoint(0, 0)).y();
      const auto previous = shortcutYByButtonY.constFind(buttonY);
      if (previous == shortcutYByButtonY.cend()) shortcutYByButtonY.insert(buttonY, shortcutY);
      else QCOMPARE(previous.value(), shortcutY);
      if (button->property("gameEventKey").toString() == QLatin1String("Interception")) {
        interceptionButton = button;
      }
    }
    QVERIFY(interceptionButton);
    QLabel* interceptionTitle = stackedTitle(interceptionButton);
    QCOMPARE(interceptionTitle->text(), QStringLiteral("Intercepción"));
    QCOMPARE(interceptionTitle->toolTip(), QString());
    if (panelWidth == GameControls::kMinimumPanelWidthPx) {
      QVERIFY(followUpX.size() < followUps.size());
    } else {
      QCOMPARE(followUpX.size(), followUps.size());
    }

    for (QPushButton* button : controls.findChildren<QPushButton*>()) {
      QLabel* title = stackedTitle(button);
      if (!title || title == interceptionTitle) continue;
      if (button->property("gameEventName").toString().isEmpty() &&
          button->property("gameEventKey").toString().isEmpty()) {
        continue;
      }
      const QString failure =
          fittedTitleFailure(title, title->property("fullLabel").toString(),
                             button->property("gameEventKey").toString().isEmpty() ? 2 : 2);
      QVERIFY2(failure.isEmpty(), qPrintable(button->property("gameEventName").toString() + failure));
    }

    const QString periodText = AppLocale::trUi("gamecontrols.start_game");
    QLabel* periodTitle = nullptr;
    for (QLabel* label : controls.findChildren<QLabel*>()) {
      if (label->property("fullLabel").toString() == periodText) periodTitle = label;
    }
    QVERIFY(periodTitle);
    const QString periodFailure = fittedTitleFailure(periodTitle, periodText, 1);
    QVERIFY2(periodFailure.isEmpty(), qPrintable(periodFailure));
    QLabel* periodShortcut = stackedShortcut(qobject_cast<QPushButton*>(periodTitle->parentWidget()));
    QVERIFY(periodShortcut);
    QCOMPARE(periodShortcut->text(), QStringLiteral("G"));
    QVERIFY(periodShortcut->parentWidget()->rect().contains(periodShortcut->geometry()));
  };

  checkWidth(GameControls::kMinimumPanelWidthPx);
  if (QTest::currentTestFailed()) return;
  checkWidth(WorkWindow::kMaximumTaggingSidePanelWidthPx);
}

void TaggingToastTest::renamedLabelsKeepBadgesAndDoNotClip() {
  PanelThemeGuard theme;
  QVERIFY(theme.loaded());
  AppLocale::setLanguage(AppLocale::Language::English);

  TagSession session;
  QWidget host;
  GameControls controls(&host);
  controls.setTagSession(&session);

  const QString polish = QStringLiteral("Aleksandra Wiśniewska-Kowalczyk");
  const QString vietnamese = QStringLiteral("Đặng Thị Ngọc Hân");
  const QString emoji = QStringLiteral("🦊 Fox");
  struct Rename {
    EventCatalog::EventId id;
    const char* text;
  };
  const Rename renamed[] = {
      {EventCatalog::EventId::Shot, "Aleksandra Wiśniewska-Kowalczyk"},
      {EventCatalog::EventId::Goal, "J"},
      {EventCatalog::EventId::Pass, "Jo"},
      {EventCatalog::EventId::CircleEntry, "Đặng Thị Ngọc Hân"},
      {EventCatalog::EventId::Card, "Benachrichtigungseinstellungen"},
      {EventCatalog::EventId::PenaltyCorner, "customer-feedback-from-enterprise-onboarding"},
      {EventCatalog::EventId::Turnover, "🦊 Fox"},
      {EventCatalog::EventId::Special, "<script>alert(1)</script>"},
      {EventCatalog::EventId::PenaltyCornerFoul, "María José de la Cruz y Fernández"},
      {EventCatalog::EventId::FiftyYard, "王秀英"},
      {EventCatalog::EventId::SeventyFiveYard, "Senior Product Design Engineer, Platform Infrastructure"},
      {EventCatalog::EventId::PenaltyStroke, "Ólafur Darri"},
      {EventCatalog::EventId::Shootout, "Seán O'Brien"},
      {EventCatalog::EventId::Interception, "Intercepción"},
      {EventCatalog::EventId::Tackle, "J"},
      {EventCatalog::EventId::Pressure, "Presión"},
      {EventCatalog::EventId::UnforcedError, "Konstantin Oberhauser-Wettstein"},
  };
  for (const Rename& entry : renamed) {
    const EventLabelOverrides::Result result =
        session.setEventLabel(entry.id, QString::fromUtf8(entry.text));
    QVERIFY2(result.accepted, entry.text);
  }

  showTaggingPanel(&host, &controls, GameControls::kMinimumPanelWidthPx, 980);
  controls.setTeamSide(TagSession::TeamSide::Home);
  controls.setSessionTeamNames(polish, vietnamese, QStringLiteral("#fde68a"), QStringLiteral("#1d4ed8"));
  QPushButton* turnover = buttonNamed(&controls, "Turnover");
  QVERIFY(turnover);
  turnover->click();
  settleLayout(&controls);

  QVERIFY2(controls.width() <= GameControls::kMinimumPanelWidthPx + 2,
           qPrintable(QString::number(controls.width())));
  QCOMPARE(visibleFollowUpButtons(&controls).size(), 4);

  QPushButton* goal = buttonNamed(&controls, "Goal");
  QPushButton* tackle = nullptr;
  for (QPushButton* button : visibleFollowUpButtons(&controls)) {
    if (button->property("gameEventKey").toString() == QLatin1String("Tackle")) tackle = button;
    QLabel* shortcut = stackedShortcut(button);
    QVERIFY(shortcut);
    const QPoint topLeft = button->mapTo(&controls, QPoint(0, 0));
    QVERIFY(QRect(QPoint(0, 0), controls.size()).contains(QRect(topLeft, button->size())));
    QVERIFY(button->rect().contains(shortcut->geometry()));
  }
  QVERIFY(goal);
  QVERIFY(tackle);
  QCOMPARE(stackedTitle(goal)->text(), QStringLiteral("J"));
  QCOMPARE(stackedTitle(tackle)->text(), QStringLiteral("J"));

  QLabel* specialTitle = stackedTitle(buttonNamed(&controls, "Special"));
  QVERIFY(specialTitle);
  QVERIFY(specialTitle->text().contains(QStringLiteral("script")) ||
          specialTitle->toolTip().contains(QStringLiteral("<script>alert(1)</script>")));
  QVERIFY(!specialTitle->text().contains(QStringLiteral("<b>")));

  QLabel* turnoverTitle = stackedTitle(turnover);
  QVERIFY(turnoverTitle);
  QVERIFY(textIsWellFormed(turnoverTitle->text()));
  QVERIFY(turnoverTitle->text().contains(QStringLiteral("🦊")) ||
          turnoverTitle->toolTip() == emoji);

  for (QPushButton* button : controls.findChildren<QPushButton*>()) {
    QLabel* title = stackedTitle(button);
    if (!title) continue;
    const QString fullText = title->property("fullLabel").toString();
    if (fullText.isEmpty()) continue;
    const int maximumLines = title->wordWrap() ? 2 : 1;
    const QString failure = fittedTitleFailure(title, fullText, maximumLines);
    QVERIFY2(failure.isEmpty(), qPrintable(fullText + QStringLiteral(" -> ") + title->text() + failure));
    QLabel* shortcut = stackedShortcut(button);
    if (!shortcut) continue;
    QVERIFY(button->rect().contains(shortcut->geometry()));
    QVERIFY(shortcut->fontMetrics().horizontalAdvance(shortcut->text()) <= shortcut->width() + 1);
  }

  QPushButton* home = buttonWithTeamLabel(&controls, polish);
  QPushButton* away = buttonWithTeamLabel(&controls, vietnamese);
  QVERIFY(home);
  QVERIFY(away);
  QVERIFY(home->text().size() < polish.size());
  QCOMPARE(home->toolTip(), polish);
  QVERIFY(home->fontMetrics().horizontalAdvance(home->text()) <=
          Style::availableButtonTextWidth(home) + 1);
  QVERIFY(away->height() >= away->fontMetrics().height());
  QVERIFY(textIsWellFormed(away->text()));
}

void TaggingToastTest::breakUiFixturesCoverFollowUpExtremes() {
  qputenv("AVA_BREAK_UI", QByteArrayLiteral("1"));
  PanelThemeGuard theme;
  QVERIFY(theme.loaded());
  AppLocale::setLanguage(AppLocale::Language::English);

  QWidget host;
  GameControls controls(&host);
  QSignalSpy committed(&controls, &GameControls::tagCommitted);
  QSignalSpy dismissed(&controls, &GameControls::tagSequenceDismissed);
  showTaggingPanel(&host, &controls, GameControls::kMinimumPanelWidthPx, 980);

  QWidget* toggle = controls.findChild<QWidget*>(QStringLiteral("TaggingBreakUiToggle"));
  QVERIFY(toggle);
  QCOMPARE(toggle->findChildren<QPushButton*>().size(), 5);
  QVERIFY(controls.width() <= GameControls::kMinimumPanelWidthPx + 2);

  auto clickFixture = [&](const char* objectName) {
    QPushButton* button = controls.findChild<QPushButton*>(QLatin1String(objectName));
    QVERIFY(button);
    button->click();
    settleLayout(&controls);
  };

  clickFixture("TaggingBreakUiWorst");
  if (QTest::currentTestFailed()) return;
  QCOMPARE(committed.count(), 0);
  QCOMPARE(dismissed.count(), 0);
  QVERIFY(controls.width() <= GameControls::kMinimumPanelWidthPx + 2);
  QLabel* shotTitle = stackedTitle(buttonNamed(&controls, "Shot"));
  QVERIFY(shotTitle);
  QVERIFY(shotTitle->text().contains(QStringLiteral("Aleksandra")) ||
          shotTitle->toolTip().contains(QStringLiteral("Aleksandra")));
  QCOMPARE(visibleFollowUpButtons(&controls).size(), 4);
  QPushButton* oneLetterFollowUp = nullptr;
  for (QPushButton* button : visibleFollowUpButtons(&controls)) {
    QLabel* title = stackedTitle(button);
    const QString failure = fittedTitleFailure(title, title->property("fullLabel").toString(), 2);
    QVERIFY2(failure.isEmpty(), qPrintable(failure));
    if (title->property("fullLabel").toString() == QLatin1String("J")) oneLetterFollowUp = button;
  }
  QVERIFY(oneLetterFollowUp);
  QCOMPARE(stackedTitle(oneLetterFollowUp)->text(), QStringLiteral("J"));

  showTaggingPanel(&host, &controls, WorkWindow::kMaximumTaggingSidePanelWidthPx, 980);
  QLabel* wideShot = stackedTitle(buttonNamed(&controls, "Shot"));
  QVERIFY(wideShot->text().contains(QStringLiteral("Aleksandra")) ||
          wideShot->toolTip().contains(QStringLiteral("Aleksandra")));
  QVERIFY(controls.width() <= WorkWindow::kMaximumTaggingSidePanelWidthPx + 2);

  showTaggingPanel(&host, &controls, GameControls::kMinimumPanelWidthPx, 980);
  clickFixture("TaggingBreakUiEmpty");
  if (QTest::currentTestFailed()) return;
  QCOMPARE(visibleFollowUpButtons(&controls).size(), 0);
  QCOMPARE(committed.count(), 0);

  clickFixture("TaggingBreakUiOne");
  if (QTest::currentTestFailed()) return;
  QCOMPARE(visibleFollowUpButtons(&controls).size(), 1);
  QLabel* onlyTitle = stackedTitle(visibleFollowUpButtons(&controls).first());
  QVERIFY(onlyTitle->text().contains(QStringLiteral("Intercep")) ||
          onlyTitle->toolTip().contains(QStringLiteral("Intercep")));
  QCOMPARE(stackedShortcut(visibleFollowUpButtons(&controls).first())->text(), QStringLiteral("1"));

  clickFixture("TaggingBreakUiMany");
  if (QTest::currentTestFailed()) return;
  QCOMPARE(visibleFollowUpButtons(&controls).size(), 4);
  for (QPushButton* button : visibleFollowUpButtons(&controls)) {
    const QString failure =
        fittedTitleFailure(stackedTitle(button), stackedTitle(button)->property("fullLabel").toString(), 2);
    QVERIFY2(failure.isEmpty(), qPrintable(failure));
  }
  QVERIFY(controls.width() <= GameControls::kMinimumPanelWidthPx + 2);

  clickFixture("TaggingBreakUiDemo");
  if (QTest::currentTestFailed()) return;
  QCOMPARE(visibleFollowUpButtons(&controls).size(), 0);
  QLabel* restoredShot = stackedTitle(buttonNamed(&controls, "Shot"));
  QCOMPARE(restoredShot->property("fullLabel").toString(), QStringLiteral("Shot"));
  QCOMPARE(committed.count(), 0);
  QCOMPARE(dismissed.count(), 0);
}

int runTaggingToastTests(int argc, char** argv) {
  TaggingToastTest tests;
  return QTest::qExec(&tests, argc, argv);
}

#include "TaggingToastTests.moc"
