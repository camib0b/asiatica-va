#include "AppLocale.h"
#include "PresentationInstancesModel.h"
#include "PresentationPanel.h"
#include "StyleProps.h"
#include "TabularDigits.h"
#include "TagSession.h"
#include "ThemeColors.h"
#include "WorkDataView.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QBrush>
#include <QButtonGroup>
#include <QComboBox>
#include <QDate>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFont>
#include <QHeaderView>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalSpy>
#include <QStyleFactory>
#include <QTableView>
#include <QTest>
#include <QToolButton>
#include <QTreeWidget>

namespace {

void showPanel(PresentationPanel& panel, int width) {
  panel.setFixedSize(width, 640);
  panel.show();
  QApplication::processEvents();
}

QTableView* instancesTable(PresentationPanel& panel) {
  return panel.findChild<QTableView*>(QStringLiteral("PresentationInstancesTable"));
}

QToolButton* toolButtonByText(QWidget* parent, const QString& text) {
  for (QToolButton* button : parent->findChildren<QToolButton*>()) {
    if (button->text() == text) return button;
  }
  return nullptr;
}

QPushButton* pushButtonByText(QWidget* parent, const QString& text) {
  for (QPushButton* button : parent->findChildren<QPushButton*>()) {
    if (button->text() == text) return button;
  }
  return nullptr;
}

TagSession::GameTag playTag(const QString& mainEvent, const QString& team, qint64 markMs) {
  TagSession::GameTag tag;
  tag.mainEvent = mainEvent;
  tag.team = team;
  tag.markMs = markMs;
  tag.startMs = markMs;
  tag.endMs = markMs + 4000;
  return tag;
}

void expectColumnsFit(QTableView* table) {
  QVERIFY(table);
  QVERIFY(table->columnWidth(1) >= 48);
  QVERIFY(table->columnWidth(2) >= 48);
  const int usedWidth = table->columnWidth(0) + table->columnWidth(1) + table->columnWidth(2);
  QVERIFY(usedWidth <= table->viewport()->width() + 1);
  QCOMPARE(table->horizontalScrollBar()->maximum(), 0);
}

QPushButton* outlinePushButton(QWidget* parent) {
  for (QPushButton* button : parent->findChildren<QPushButton*>()) {
    if (button->property("variant").toString() == QStringLiteral("outline")) return button;
  }
  return nullptr;
}

int presentationTimeColumnWidth(qint64 markMs) {
  TagSession session;
  session.setGameTeams(QStringLiteral("Home Club"), QStringLiteral("Away Club"), QString(),
                       QString());
  session.addTag(playTag(QStringLiteral("Goal"), QStringLiteral("Home"), markMs));
  PresentationPanel panel;
  panel.setTagSession(&session);
  showPanel(panel, 320);
  QTableView* table = instancesTable(panel);
  return table ? table->columnWidth(0) : -1;
}

} // namespace

class WorkDataViewTest : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanup();
  void sharedChromeMatchesAcrossTheThreeLists();
  void presentationListKeepsSelectionAndFitsTheSideColumn();
  void breakUiToggleSwapsFixturesWithoutChangingSelection();
  void modelKeepsChecksHighlightAndEscapesTooltips();
  void applyLabelAbbreviationsAndSteadyTimeDigits();
};

void WorkDataViewTest::initTestCase() {
  QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
  QFont font(QStringLiteral("Inter"));
  font.setPointSize(13);
  QApplication::setFont(font);

  QFile stylesheetFile(QStringLiteral(AVA_THEME_QSS_PATH));
  QVERIFY(stylesheetFile.open(QIODevice::ReadOnly | QIODevice::Text));
  const QString stylesheet = QString::fromUtf8(stylesheetFile.readAll());
  QVERIFY(stylesheet.contains(QStringLiteral("QAbstractItemView[role=\"workList\"]")));
  QVERIFY(stylesheet.contains(QStringLiteral("QAbstractItemView[role=\"workList\"]::item:hover")));
  QVERIFY(stylesheet.contains(QStringLiteral("QAbstractItemView[role=\"workList\"]::item:selected")));
  QVERIFY(stylesheet.contains(QStringLiteral("QTableView[role=\"workList\"]::indicator")));
  QVERIFY(!stylesheet.contains(QStringLiteral("QTableView#TagsTable")));
  qApp->setStyleSheet(stylesheet);
}

void WorkDataViewTest::cleanup() {
  qunsetenv("AVA_BREAK_UI");
  AppLocale::setLanguage(AppLocale::Language::English);
}

void WorkDataViewTest::sharedChromeMatchesAcrossTheThreeLists() {
  QTableView taggingLog;
  taggingLog.setObjectName(QStringLiteral("TagsTable"));
  Style::configureWorkDataView(&taggingLog);

  QTreeWidget statsTree;
  statsTree.setObjectName(QStringLiteral("StatsTree"));
  statsTree.setColumnCount(2);
  Style::configureWorkDataView(&statsTree);

  QTableView presentationList;
  presentationList.setObjectName(QStringLiteral("PresentationInstancesTable"));
  Style::configureWorkDataView(&presentationList);

  const QList<QAbstractItemView*> views = {&taggingLog, &statsTree, &presentationList};
  for (QAbstractItemView* view : views) {
    QCOMPARE(view->property("role").toString(), QStringLiteral("workList"));
    QVERIFY(view->alternatingRowColors());
    QCOMPARE(view->selectionBehavior(), QAbstractItemView::SelectRows);
    QCOMPARE(view->selectionMode(), QAbstractItemView::SingleSelection);
    QCOMPARE(view->editTriggers(), QAbstractItemView::NoEditTriggers);
    QCOMPARE(view->textElideMode(), Qt::ElideRight);
  }
  QVERIFY(!taggingLog.wordWrap());
  QVERIFY(!statsTree.wordWrap());
  QVERIFY(!presentationList.wordWrap());

  QCOMPARE(taggingLog.horizontalHeader()->defaultAlignment(), Qt::AlignLeft | Qt::AlignVCenter);
  QCOMPARE(statsTree.header()->defaultAlignment(), Qt::AlignLeft | Qt::AlignVCenter);
  QCOMPARE(presentationList.horizontalHeader()->defaultAlignment(),
           taggingLog.horizontalHeader()->defaultAlignment());
  QVERIFY(!taggingLog.showGrid());
  QVERIFY(taggingLog.verticalHeader()->isHidden());
  QVERIFY(statsTree.uniformRowHeights());

  const int taggingRowHeight = taggingLog.verticalHeader()->defaultSectionSize();
  const int presentationRowHeight = presentationList.verticalHeader()->defaultSectionSize();
  QCOMPARE(presentationRowHeight, taggingRowHeight);
  QVERIFY(taggingRowHeight >= 24);
  QVERIFY(taggingRowHeight <= 48);
  QCOMPARE(taggingLog.verticalHeader()->sectionResizeMode(0), QHeaderView::Fixed);
}

void WorkDataViewTest::presentationListKeepsSelectionAndFitsTheSideColumn() {
  qunsetenv("AVA_BREAK_UI");
  AppLocale::setLanguage(AppLocale::Language::English);

  TagSession session;
  session.setGameTeams(QStringLiteral("Aleksandra Wiśniewska-Kowalczyk"), QStringLiteral("Jo"),
                       QString(), QString());
  session.addTag(playTag(QStringLiteral("Circle Entry"), QStringLiteral("Home"), 125000));
  session.addTag(playTag(QStringLiteral("Goal"), QStringLiteral("Away"), 8000));

  PresentationPanel panel;
  panel.setTagSession(&session);
  QVERIFY(panel.findChild<QWidget*>(QStringLiteral("PresentationListDataToggle")) == nullptr);

  QTableView* table = instancesTable(panel);
  QVERIFY(table);
  QCOMPARE(table->property("role").toString(), QStringLiteral("workList"));
  QCOMPARE(table->model()->rowCount(), 2);

  QComboBox* eventFilter = nullptr;
  QComboBox* teamFilter = nullptr;
  const QList<QComboBox*> combos = panel.findChildren<QComboBox*>();
  QCOMPARE(combos.size(), 2);
  for (QComboBox* combo : combos) {
    if (combo->findData(QStringLiteral("Circle Entry")) >= 0) eventFilter = combo;
    if (combo->findData(QStringLiteral("Home")) >= 0) teamFilter = combo;
  }
  QVERIFY(eventFilter);
  QVERIFY(teamFilter);
  QCOMPARE(eventFilter->property("role").toString(), QString());
  QCOMPARE(teamFilter->property("role").toString(), QString());

  QPushButton* applyButton = pushButtonByText(&panel, AppLocale::trUi("presentation.apply_to_all"));
  QVERIFY(applyButton);
  QCOMPARE(applyButton->property("variant").toString(), QStringLiteral("outline"));

  showPanel(panel, 320);
  expectColumnsFit(table);
  const QString homeName = QStringLiteral("Aleksandra Wiśniewska-Kowalczyk");
  int homeRow = -1;
  for (int row = 0; row < table->model()->rowCount(); ++row) {
    if (table->model()->data(table->model()->index(row, 1), Qt::DisplayRole).toString() == homeName) {
      homeRow = row;
    }
  }
  QVERIFY(homeRow >= 0);
  const QModelIndex homeTeam = table->model()->index(homeRow, 1);
  QCOMPARE(table->model()->data(homeTeam, Qt::ToolTipRole).toString(), homeName);
  QVERIFY(qAbs(table->columnWidth(1) - table->columnWidth(2)) <= 8);

  showPanel(panel, 260);
  expectColumnsFit(table);
  showPanel(panel, 1200);
  expectColumnsFit(table);

  const int goalIndex = eventFilter->findData(QStringLiteral("Goal"));
  QVERIFY(goalIndex >= 0);
  eventFilter->setCurrentIndex(goalIndex);
  QCOMPARE(table->model()->rowCount(), 1);
  eventFilter->setCurrentIndex(0);
  QCOMPARE(table->model()->rowCount(), 2);

  teamFilter->setCurrentIndex(teamFilter->findData(QStringLiteral("Away")));
  QCOMPARE(table->model()->rowCount(), 1);
  teamFilter->setCurrentIndex(0);
  QCOMPARE(table->model()->rowCount(), 2);

  QToolButton* selectAll = toolButtonByText(&panel, AppLocale::trUi("presentation.select_all"));
  QToolButton* selectNone = toolButtonByText(&panel, AppLocale::trUi("presentation.select_none"));
  QVERIFY(selectAll);
  QVERIFY(selectNone);
  QTest::mouseClick(selectAll, Qt::LeftButton);
  QCOMPARE(panel.selectedTagIndexes().size(), 2);
  QTest::mouseClick(selectNone, Qt::LeftButton);
  QCOMPARE(panel.selectedTagIndexes().size(), 0);
  QTest::mouseClick(selectAll, Qt::LeftButton);
  QCOMPARE(panel.selectedTagIndexes().size(), 2);

  panel.setCurrentClip(0, 3000, 5000);
  auto* leadSpin = panel.findChild<QDoubleSpinBox*>(QStringLiteral("LeadSpinBox"));
  QVERIFY(leadSpin);
  QCOMPARE(leadSpin->value(), 3.0);
  QVERIFY(applyButton->isEnabled());
  const QVariant highlight =
      table->model()->data(table->model()->index(homeRow, 2), Qt::BackgroundRole);
  QVERIFY(highlight.canConvert<QBrush>());
  QCOMPARE(highlight.value<QBrush>().color(), Style::ThemeColors::playheadHighlight());
}

void WorkDataViewTest::breakUiToggleSwapsFixturesWithoutChangingSelection() {
  qputenv("AVA_BREAK_UI", QByteArray("1"));
  AppLocale::setLanguage(AppLocale::Language::English);

  TagSession session;
  session.setGameTeams(QStringLiteral("Home Club"), QStringLiteral("Away Club"), QString(),
                       QString());
  session.addTag(playTag(QStringLiteral("Circle Entry"), QStringLiteral("Home"), 4000));
  session.addTag(playTag(QStringLiteral("Goal"), QStringLiteral("Away"), 9000));

  PresentationPanel panel;
  panel.setTagSession(&session);
  showPanel(panel, 260);

  QWidget* toggle = panel.findChild<QWidget*>(QStringLiteral("PresentationListDataToggle"));
  QVERIFY(toggle);
  QVERIFY(toggle->isVisible());
  QVERIFY(panel.findChild<QButtonGroup*>());

  QTableView* table = instancesTable(panel);
  QVERIFY(table);
  QCOMPARE(table->model()->rowCount(), 2);

  QToolButton* selectAll = toolButtonByText(&panel, AppLocale::trUi("presentation.select_all"));
  QVERIFY(selectAll);
  QTest::mouseClick(selectAll, Qt::LeftButton);
  QCOMPARE(panel.selectedTagIndexes().size(), 2);

  QSignalSpy selectionSpy(&panel, &PresentationPanel::selectedTagIndexesChanged);

  const char* fixtureNames[] = {"PresentationListDataDemo", "PresentationListDataWorst",
                                "PresentationListDataEmpty", "PresentationListDataOne",
                                "PresentationListDataThousand"};
  for (const char* fixtureName : fixtureNames) {
    auto* button = toggle->findChild<QToolButton*>(QString::fromLatin1(fixtureName));
    QVERIFY(button);
    const int textWidth = button->fontMetrics().horizontalAdvance(button->text());
    QVERIFY2(button->width() + 1 >= textWidth, qPrintable(button->text()));
  }

  auto* worstCaseButton = toggle->findChild<QToolButton*>(QStringLiteral("PresentationListDataWorst"));
  auto* emptyButton = toggle->findChild<QToolButton*>(QStringLiteral("PresentationListDataEmpty"));
  auto* oneButton = toggle->findChild<QToolButton*>(QStringLiteral("PresentationListDataOne"));
  auto* thousandButton =
      toggle->findChild<QToolButton*>(QStringLiteral("PresentationListDataThousand"));
  auto* demoButton = toggle->findChild<QToolButton*>(QStringLiteral("PresentationListDataDemo"));
  QVERIFY(worstCaseButton);
  QVERIFY(emptyButton);
  QVERIFY(oneButton);
  QVERIFY(thousandButton);
  QVERIFY(demoButton);

  QTest::mouseClick(worstCaseButton, Qt::LeftButton);
  QVERIFY(table->model()->rowCount() >= 12);
  bool sawEmail = false;
  bool sawScript = false;
  bool sawDash = false;
  bool sawLongDuration = false;
  bool sawNegativeSign = false;
  for (int row = 0; row < table->model()->rowCount(); ++row) {
    const QString team = table->model()->data(table->model()->index(row, 1), Qt::DisplayRole).toString();
    const QString event = table->model()->data(table->model()->index(row, 2), Qt::DisplayRole).toString();
    const QString time = table->model()->data(table->model()->index(row, 0), Qt::DisplayRole).toString();
    const QString eventTip =
        table->model()->data(table->model()->index(row, 2), Qt::ToolTipRole).toString();
    if (team.contains(QStringLiteral("bartholomew.fitzgerald@"))) sawEmail = true;
    if (event.contains(QStringLiteral("<script>alert(1)</script>"))) {
      sawScript = true;
      QVERIFY(eventTip.contains(QStringLiteral("&lt;script&gt;")));
    }
    if (team == QStringLiteral("—")) sawDash = true;
    if (time == QStringLiteral("1284:00:00")) {
      sawLongDuration = true;
      const int timeWidth = table->fontMetrics().horizontalAdvance(time);
      QVERIFY(table->columnWidth(0) >= timeWidth);
    }
    if (time.startsWith(QLatin1Char('-'))) sawNegativeSign = true;
    QCOMPARE(table->verticalHeader()->sectionSize(row),
             table->verticalHeader()->defaultSectionSize());
  }
  QVERIFY(sawEmail);
  QVERIFY(sawScript);
  QVERIFY(sawDash);
  QVERIFY(sawLongDuration);
  QVERIFY(!sawNegativeSign);
  expectColumnsFit(table);
  QCOMPARE(panel.selectedTagIndexes().size(), 2);
  QCOMPARE(selectionSpy.count(), 0);

  QTest::mouseClick(emptyButton, Qt::LeftButton);
  QCOMPARE(table->model()->rowCount(), 0);
  QCOMPARE(panel.selectedTagIndexes().size(), 2);

  QTest::mouseClick(oneButton, Qt::LeftButton);
  QCOMPARE(table->model()->rowCount(), 1);
  QCOMPARE(table->model()->data(table->model()->index(0, 1), Qt::DisplayRole).toString(),
           QStringLiteral("Jo"));
  QCOMPARE(panel.selectedTagIndexes().size(), 2);

  QTest::mouseClick(thousandButton, Qt::LeftButton);
  QCOMPARE(table->model()->rowCount(), 1000);
  QCOMPARE(panel.selectedTagIndexes().size(), 2);
  QCOMPARE(selectionSpy.count(), 0);

  QTest::mouseClick(demoButton, Qt::LeftButton);
  QCOMPARE(table->model()->rowCount(), 2);
  QCOMPARE(panel.selectedTagIndexes().size(), 2);
  QCOMPARE(selectionSpy.count(), 0);
}

void WorkDataViewTest::modelKeepsChecksHighlightAndEscapesTooltips() {
  PresentationInstancesModel model;
  PresentationInstancesModel::Row row;
  row.tagId = 5;
  row.tagSessionIndex = 0;
  row.markMs = 1500;
  row.teamDisplay = QStringLiteral("Jo");
  row.eventLine = QStringLiteral("<script>alert(1)</script>");
  model.setRows({row});
  model.setSelectedTagIds({quint64{5}});

  QCOMPARE(model.data(model.index(0, 0), Qt::DisplayRole).toString(), QStringLiteral("00:01"));
  QCOMPARE(model.data(model.index(0, 0), Qt::CheckStateRole).toInt(), static_cast<int>(Qt::Checked));
  QCOMPARE(model.data(model.index(0, 2), Qt::ToolTipRole).toString(),
           QStringLiteral("&lt;script&gt;alert(1)&lt;/script&gt;"));

  model.setCurrentTagId(5);
  const QVariant background = model.data(model.index(0, 1), Qt::BackgroundRole);
  QVERIFY(background.canConvert<QBrush>());
  QCOMPARE(background.value<QBrush>().color(), Style::ThemeColors::playheadHighlight());
}

void WorkDataViewTest::applyLabelAbbreviationsAndSteadyTimeDigits() {
  qunsetenv("AVA_BREAK_UI");
  AppLocale::setLanguage(AppLocale::Language::English);

  const QString homeName = QStringLiteral("Aleksandra Wiśniewska-Kowalczyk");
  const QString awayName = QStringLiteral("AT&T United");
  TagSession session;
  session.setGameTeams(homeName, awayName, QString(), QString());
  session.addTag(playTag(QStringLiteral("Circle Entry"), QStringLiteral("Home"), 125000));
  session.addTag(playTag(QStringLiteral("Goal"), QStringLiteral("Away"), 8000));

  PresentationPanel panel;
  panel.setTagSession(&session);
  showPanel(panel, 260);

  QTableView* table = instancesTable(panel);
  QVERIFY(table);
  QVERIFY(table->itemDelegateForColumn(0) != nullptr);
  QVERIFY(table->itemDelegateForColumn(0) != table->itemDelegate());
  QCOMPARE(table->itemDelegateForColumn(1), nullptr);
  QCOMPARE(table->itemDelegateForColumn(2), nullptr);

  const QFontMetrics tableMetrics = table->fontMetrics();
  QCOMPARE(Style::tabularTextWidth(tableMetrics, QStringLiteral("09:59")),
           Style::tabularTextWidth(tableMetrics, QStringLiteral("10:00")));
  QCOMPARE(Style::tabularTextWidth(tableMetrics, QStringLiteral("11:11")),
           Style::tabularTextWidth(tableMetrics, QStringLiteral("00:00")));
  QCOMPARE(Style::tabularTextWidth(tableMetrics, QStringLiteral("1284:11:11")),
           Style::tabularTextWidth(tableMetrics, QStringLiteral("1284:00:00")));
  const int narrowDigitWidth = tableMetrics.horizontalAdvance(QChar(u'1'));
  const int wideDigitWidth = tableMetrics.horizontalAdvance(QChar(u'0'));
  if (narrowDigitWidth != wideDigitWidth) {
    QVERIFY(tableMetrics.horizontalAdvance(QStringLiteral("11:11"))
            < tableMetrics.horizontalAdvance(QStringLiteral("00:00")));
  }

  const int zeroMinutesWidth = presentationTimeColumnWidth(0);
  const int elevenMinutesWidth = presentationTimeColumnWidth((11 * 60 + 11) * 1000LL);
  QCOMPARE(zeroMinutesWidth, elevenMinutesWidth);
  QVERIFY(zeroMinutesWidth > 48);
  const qint64 longHoursMs = 1284LL * 3600LL * 1000LL;
  QCOMPARE(presentationTimeColumnWidth(longHoursMs),
           presentationTimeColumnWidth(longHoursMs + (11 * 60 + 11) * 1000LL));

  QComboBox* eventFilter = nullptr;
  QComboBox* teamFilter = nullptr;
  for (QComboBox* combo : panel.findChildren<QComboBox*>()) {
    if (combo->findData(QStringLiteral("Circle Entry")) >= 0) eventFilter = combo;
    if (combo->findData(QStringLiteral("Home")) >= 0) teamFilter = combo;
  }
  QVERIFY(eventFilter);
  QVERIFY(teamFilter);
  QCOMPARE(eventFilter->sizeAdjustPolicy(), QComboBox::AdjustToContentsOnFirstShow);
  QCOMPARE(teamFilter->sizeAdjustPolicy(), QComboBox::AdjustToMinimumContentsLengthWithIcon);
  QCOMPARE(teamFilter->minimumContentsLength(), 4);
  QVERIFY(teamFilter->view());
  QVERIFY(teamFilter->view()->hasMouseTracking());

  const int homeIndex = teamFilter->findData(QStringLiteral("Home"));
  const int awayIndex = teamFilter->findData(QStringLiteral("Away"));
  QVERIFY(homeIndex >= 0);
  QVERIFY(awayIndex >= 0);
  QCOMPARE(teamFilter->itemText(homeIndex), QStringLiteral("AWK"));
  QCOMPARE(teamFilter->itemText(awayIndex), QStringLiteral("ATU"));
  QCOMPARE(teamFilter->itemData(homeIndex, Qt::ToolTipRole).toString(), homeName);
  QCOMPARE(teamFilter->itemData(awayIndex, Qt::ToolTipRole).toString(),
           QStringLiteral("AT&amp;T United"));
  QCOMPARE(teamFilter->toolTip(), QString());
  teamFilter->setCurrentIndex(homeIndex);
  QCOMPARE(teamFilter->toolTip(), homeName);
  teamFilter->setCurrentIndex(0);
  QCOMPARE(table->model()->rowCount(), 2);

  int homeRow = -1;
  for (int row = 0; row < table->model()->rowCount(); ++row) {
    if (table->model()->data(table->model()->index(row, 1), Qt::DisplayRole).toString() == homeName) {
      homeRow = row;
    }
  }
  QVERIFY(homeRow >= 0);

  QPushButton* applyButton = outlinePushButton(&panel);
  QVERIFY(applyButton);
  QCOMPARE(applyButton->property("variant").toString(), QStringLiteral("outline"));
  QCOMPARE(applyButton->text(), QStringLiteral("Apply to all selected clips"));
  QCOMPARE(applyButton->toolTip(), AppLocale::trUi("presentation.apply_to_all_tooltip"));

  QVERIFY(table->model()->setData(table->model()->index(0, 0), Qt::Checked, Qt::CheckStateRole));
  QCOMPARE(panel.selectedTagIndexes().size(), 1);
  QCOMPARE(applyButton->text(), QStringLiteral("Apply to the selected clip"));
  QCOMPARE(applyButton->toolTip(), AppLocale::trUi("presentation.apply_to_one_tooltip"));
  const int singularButtonWidth = applyButton->width();

  QVERIFY(table->model()->setData(table->model()->index(1, 0), Qt::Checked, Qt::CheckStateRole));
  QCOMPARE(panel.selectedTagIndexes().size(), 2);
  QCOMPARE(applyButton->text(), QStringLiteral("Apply to all selected clips"));
  QCOMPARE(applyButton->width(), singularButtonWidth);

  AppLocale::setLanguage(AppLocale::Language::Spanish);
  panel.applyUiStrings();
  showPanel(panel, 260);
  QCOMPARE(panel.selectedTagIndexes().size(), 2);
  QCOMPARE(applyButton->text(), QStringLiteral("Aplicar a todos los clips seleccionados"));
  QCOMPARE(applyButton->toolTip(), AppLocale::trUi("presentation.apply_to_all_tooltip"));

  QVERIFY(table->model()->setData(table->model()->index(0, 0), Qt::Unchecked, Qt::CheckStateRole));
  QCOMPARE(panel.selectedTagIndexes().size(), 1);
  QCOMPARE(applyButton->text(), QStringLiteral("Aplicar al clip seleccionado"));
  QCOMPARE(applyButton->toolTip(), AppLocale::trUi("presentation.apply_to_one_tooltip"));
  QVERIFY2(applyButton->width() + 1 >= Style::pushButtonMinimumWidth(applyButton),
           qPrintable(QStringLiteral("singular width %1 minimum %2")
                          .arg(applyButton->width())
                          .arg(Style::pushButtonMinimumWidth(applyButton))));

  QVERIFY(table->model()->setData(table->model()->index(1, 0), Qt::Unchecked, Qt::CheckStateRole));
  QCOMPARE(panel.selectedTagIndexes().size(), 0);
  QCOMPARE(applyButton->text(), QStringLiteral("Aplicar a todos los clips seleccionados"));
  const int pluralMinimumWidth = Style::pushButtonMinimumWidth(applyButton);
  showPanel(panel, qMax(260, pluralMinimumWidth));
  QVERIFY2(applyButton->width() + 1 >= pluralMinimumWidth,
           qPrintable(QStringLiteral("plural width %1 minimum %2")
                          .arg(applyButton->width())
                          .arg(pluralMinimumWidth)));
  showPanel(panel, 260);

  session.setGameMetadata(QDate(), QStringLiteral("WISNIEWSKA-KOWALCZYK"), QStringLiteral("JO"));
  showPanel(panel, 260);
  QCOMPARE(teamFilter->itemText(teamFilter->findData(QStringLiteral("Home"))),
           QStringLiteral("WISNIEWSKA-KOWALCZYK"));
  QCOMPARE(teamFilter->itemText(teamFilter->findData(QStringLiteral("Away"))), QStringLiteral("JO"));
  QCOMPARE(teamFilter->itemData(teamFilter->findData(QStringLiteral("Home")), Qt::ToolTipRole)
               .toString(),
           homeName);
  const int longAbbreviationAdvance =
      teamFilter->fontMetrics().horizontalAdvance(QStringLiteral("WISNIEWSKA-KOWALCZYK"));
  QVERIFY(teamFilter->sizeHint().width() < longAbbreviationAdvance);
  QVERIFY(teamFilter->width() < longAbbreviationAdvance);
  QVERIFY(eventFilter->width() >= 48);
  QVERIFY(eventFilter->isVisible());
  expectColumnsFit(table);

  AppLocale::setLanguage(AppLocale::Language::English);
  TagSession unnamedSession;
  PresentationPanel unnamedPanel;
  unnamedPanel.setTagSession(&unnamedSession);
  QComboBox* unnamedTeamFilter = nullptr;
  for (QComboBox* combo : unnamedPanel.findChildren<QComboBox*>()) {
    if (combo->findData(QStringLiteral("Home")) >= 0) unnamedTeamFilter = combo;
  }
  QVERIFY(unnamedTeamFilter);
  QCOMPARE(unnamedTeamFilter->itemText(unnamedTeamFilter->findData(QStringLiteral("Home"))),
           QStringLiteral("Home"));
  QCOMPARE(unnamedTeamFilter->itemText(unnamedTeamFilter->findData(QStringLiteral("Away"))),
           QStringLiteral("Away"));
}

int runWorkDataViewTests(int argc, char** argv) {
  WorkDataViewTest tests;
  return QTest::qExec(&tests, argc, argv);
}

#include "WorkDataViewTests.moc"
