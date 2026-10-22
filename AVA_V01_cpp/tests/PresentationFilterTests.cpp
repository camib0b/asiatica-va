#include "AppLocale.h"
#include "PresentationPanel.h"
#include "TagSession.h"

#include <QApplication>
#include <QComboBox>
#include <QFile>
#include <QLayout>
#include <QStyle>
#include <QStyleOptionComboBox>
#include <QTest>

namespace {

bool applyTheme() {
  QFile file(QStringLiteral(AVA_THEME_QSS_PATH));
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
  qApp->setStyleSheet(QString::fromUtf8(file.readAll()));
  return true;
}

QComboBox* comboNamed(QWidget* root, const char* name) {
  return root->findChild<QComboBox*>(QLatin1String(name));
}

int editFieldWidth(const QComboBox* combo) {
  QStyleOptionComboBox option;
  option.initFrom(combo);
  option.editable = combo->isEditable();
  option.subControls = QStyle::SC_All;
  return combo->style()
      ->subControlRect(QStyle::CC_ComboBox, &option, QStyle::SC_ComboBoxEditField, combo)
      .width();
}

QString fitError(const QComboBox* combo, const QString& text) {
  if (!combo) return QStringLiteral("missing combo");
  if (combo->currentText() != text) {
    return QStringLiteral("text '%1'").arg(combo->currentText());
  }
  const int advance = combo->fontMetrics().horizontalAdvance(text);
  const int editWidth = editFieldWidth(combo);
  if (advance > editWidth) {
    return QStringLiteral("%1 advance %2 edit %3 box %4").arg(text).arg(advance).arg(editWidth).arg(combo->width());
  }
  if (!combo->toolTip().isEmpty()) return QStringLiteral("tooltip '%1'").arg(combo->toolTip());
  if (combo->width() > 220) return QStringLiteral("width %1").arg(combo->width());
  if (combo->width() < combo->minimumWidth()) return QStringLiteral("below minimum");
  return QString();
}

TagSession::GameTag playTag(const QString& mainEvent, const QString& team) {
  TagSession::GameTag tag;
  tag.setMainEventKey(TagEvent::keyFromCanonicalName(mainEvent));
  tag.team = team;
  tag.markMs = 1000;
  return tag;
}

}  // namespace

class PresentationFilterTest : public QObject {
  Q_OBJECT

 private slots:
  void init();
  void cleanup();
  void spanishDefaultsUseTheFullTextLane();
  void englishDefaultsUseTheFullTextLane();
  void longNamesElideOnlyWhenTheBoxCannotGrow();

 private:
  AppLocale::Language previousLanguage_ = AppLocale::Language::English;
};

void PresentationFilterTest::init() {
  previousLanguage_ = AppLocale::currentLanguage();
  QVERIFY(applyTheme());
}

void PresentationFilterTest::cleanup() { AppLocale::setLanguage(previousLanguage_); }

void PresentationFilterTest::spanishDefaultsUseTheFullTextLane() {
  AppLocale::setLanguage(AppLocale::Language::Spanish);
  PresentationPanel panel;
  panel.resize(480, 640);
  panel.show();
  QApplication::sendPostedEvents();
  panel.applyUiStrings();
  QApplication::sendPostedEvents();

  QVERIFY2(fitError(comboNamed(&panel, "PresentationEventFilter"),
                    QStringLiteral("Todos los eventos"))
               .isEmpty(),
           qPrintable(fitError(comboNamed(&panel, "PresentationEventFilter"),
                               QStringLiteral("Todos los eventos"))));
  QVERIFY2(fitError(comboNamed(&panel, "PresentationTeamFilter"), QStringLiteral("Ambos")).isEmpty(),
           qPrintable(fitError(comboNamed(&panel, "PresentationTeamFilter"), QStringLiteral("Ambos"))));
}

void PresentationFilterTest::englishDefaultsUseTheFullTextLane() {
  AppLocale::setLanguage(AppLocale::Language::English);
  PresentationPanel panel;
  panel.resize(480, 640);
  panel.show();
  QApplication::sendPostedEvents();
  panel.applyUiStrings();
  QApplication::sendPostedEvents();

  QVERIFY2(fitError(comboNamed(&panel, "PresentationEventFilter"), QStringLiteral("All events")).isEmpty(),
           qPrintable(fitError(comboNamed(&panel, "PresentationEventFilter"), QStringLiteral("All events"))));
  QVERIFY2(fitError(comboNamed(&panel, "PresentationTeamFilter"), QStringLiteral("Both")).isEmpty(),
           qPrintable(fitError(comboNamed(&panel, "PresentationTeamFilter"), QStringLiteral("Both"))));
}

void PresentationFilterTest::longNamesElideOnlyWhenTheBoxCannotGrow() {
  AppLocale::setLanguage(AppLocale::Language::Spanish);
  const QString longEvent = QStringLiteral("Ingreso al círculo por la banda izquierda con arrastre");
  const QString longTeam = QStringLiteral(
      "Selección femenina de hockey sobre césped de los Países Bajos — Aleksandra Wiśniewska-Kowalczyk");
  const QString shortTeam = QStringLiteral("J");

  TagSession session;
  session.setGameTeams(longTeam, shortTeam, QStringLiteral("#1d4ed8"), QStringLiteral("#fde68a"));
  session.addTag(playTag(longEvent, QStringLiteral("Home")));
  session.addTag(playTag(QStringLiteral("<b>Shot</b> &amp; **goal**"), QStringLiteral("Away")));

  PresentationPanel panel;
  panel.setTagSession(&session);
  panel.resize(360, 640);
  panel.show();
  if (panel.layout()) panel.layout()->activate();
  QApplication::sendPostedEvents();

  QComboBox* events = comboNamed(&panel, "PresentationEventFilter");
  QComboBox* teams = comboNamed(&panel, "PresentationTeamFilter");
  QVERIFY(events && teams);

  const int longEventIndex = events->findData(longEvent);
  QVERIFY(longEventIndex >= 0);
  events->setCurrentIndex(longEventIndex);
  if (panel.layout()) panel.layout()->activate();
  QApplication::sendPostedEvents();
  QVERIFY(events->width() <= 220);
  const QString eventLabel = events->currentText();
  QVERIFY(eventLabel.startsWith(QStringLiteral("Ingreso al círculo")));
  QVERIFY2(events->fontMetrics().horizontalAdvance(eventLabel) > editFieldWidth(events),
           qPrintable(QStringLiteral("event box %1 edit %2").arg(events->width()).arg(editFieldWidth(events))));
  QCOMPARE(events->toolTip(), eventLabel);

  teams->setCurrentIndex(teams->findData(QStringLiteral("Home")));
  if (panel.layout()) panel.layout()->activate();
  QApplication::sendPostedEvents();
  // The closed team box shows the abbreviation. The club name stays on the tooltip.
  QCOMPARE(teams->currentText(), QStringLiteral("SFD"));
  QVERIFY(teams->width() <= 220);
  QVERIFY(teams->fontMetrics().horizontalAdvance(teams->currentText()) <= editFieldWidth(teams));
  QCOMPARE(teams->toolTip(), longTeam.toHtmlEscaped());

  teams->setCurrentIndex(teams->findData(QStringLiteral("Away")));
  QApplication::sendPostedEvents();
  QCOMPARE(teams->currentText(), shortTeam);
  QCOMPARE(teams->toolTip(), shortTeam.toHtmlEscaped());
  QVERIFY(teams->fontMetrics().horizontalAdvance(shortTeam) <= editFieldWidth(teams));

  const int markupIndex = events->findData(QStringLiteral("<b>Shot</b> &amp; **goal**"));
  QVERIFY(markupIndex >= 0);
  events->setCurrentIndex(markupIndex);
  QCOMPARE(events->currentText(), QStringLiteral("<b>Shot</b> &amp; **goal**  (1)"));
}

int runPresentationFilterTests(int argc, char** argv) {
  PresentationFilterTest tests;
  return QTest::qExec(&tests, argc, argv);
}

#include "PresentationFilterTests.moc"
