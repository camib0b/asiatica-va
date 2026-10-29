#include "AppLocale.h"
#include "PresentationInstancesModel.h"
#include "PresentationPanel.h"
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
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalSpy>
#include <QStyleFactory>
#include <QStyleOptionViewItem>
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

TagSession::GameTag playTag(const QString& mainEvent, const QString& team, qint64 markMs) {
  TagSession::GameTag tag;
  tag.setMainEventKey(TagEvent::keyFromCanonicalName(mainEvent));
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

QLabel* applyToAllLabel(QWidget* parent) {
  return parent->findChild<QLabel*>(QStringLiteral("ApplyToAllButtonLabel"));
}

void expectRgb(const QColor& actual, const QColor& expected, const QString& description) {
  const QString message = QStringLiteral("%1 rgb %2,%3,%4 alpha %5, expected %6,%7,%8")
                              .arg(description)
                              .arg(actual.red())
                              .arg(actual.green())
                              .arg(actual.blue())
                              .arg(actual.alpha())
                              .arg(expected.red())
                              .arg(expected.green())
                              .arg(expected.blue());
  QVERIFY2(actual.red() == expected.red() && actual.green() == expected.green()
               && actual.blue() == expected.blue(),
           qPrintable(message));
}

QImage paintClipCell(QTableView* table, const QModelIndex& index, QStyle::State extraState) {
  const QSize cellSize = table->visualRect(index).size().expandedTo(QSize(1, 1));
  QImage image(cellSize, QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::white);
  QPainter painter(&image);
  QStyleOptionViewItem option;
  option.initFrom(table);
  option.widget = table;
  option.rect = QRect(QPoint(0, 0), image.size());
  option.state = QStyle::State_Enabled | QStyle::State_Active | extraState;
  table->itemDelegateForIndex(index)->paint(&painter, option, index);
  painter.end();
  return image;
}

QImage renderClipViewport(QTableView* table) {
  QImage image(table->viewport()->size(), QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::white);
  QPainter painter(&image);
  table->viewport()->render(&painter);
  painter.end();
  return image;
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
  void presentationSelectionIsDistinctFromHoverAndFocus();
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
  QVERIFY(stylesheet.contains(QStringLiteral("background: #f4f4f5")));
  QVERIFY(stylesheet.contains(QStringLiteral("QTableView#PresentationInstancesTable::item:selected")));
  QVERIFY(stylesheet.contains(QStringLiteral("background: #93C5FD")));
  QVERIFY(stylesheet.contains(QStringLiteral("QTableView#PresentationInstancesTable::item:hover")));
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
    if (combo->findData(static_cast<int>(EventCatalog::EventId::CircleEntry)) >= 0) eventFilter = combo;
    if (combo->findData(QStringLiteral("Home")) >= 0) teamFilter = combo;
  }
  QVERIFY(eventFilter);
  QVERIFY(teamFilter);
  QCOMPARE(eventFilter->property("role").toString(), QString());
  QCOMPARE(teamFilter->property("role").toString(), QString());

  QPushButton* applyButton = panel.findChild<QPushButton*>(QStringLiteral("ApplyToAllButton"));
  QLabel* applyLabel = applyToAllLabel(&panel);
  QVERIFY(applyButton);
  QVERIFY(applyLabel);
  QCOMPARE(applyButton->property("variant").toString(), QStringLiteral("outline"));
  QCOMPARE(applyButton->text(), QString());
  QCOMPARE(applyLabel->text(), AppLocale::trUi("presentation.apply_to_all"));
  QVERIFY(applyLabel->wordWrap());

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

  const int goalIndex = eventFilter->findData(static_cast<int>(EventCatalog::EventId::Goal));
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

void WorkDataViewTest::presentationSelectionIsDistinctFromHoverAndFocus() {
  qunsetenv("AVA_BREAK_UI");
  AppLocale::setLanguage(AppLocale::Language::English);

  TagSession session;
  session.setGameTeams(QStringLiteral("Home Club"), QStringLiteral("Away Club"), QString(),
                       QString());
  session.addTag(playTag(QStringLiteral("Circle Entry"), QStringLiteral("Home"), 125000));
  session.addTag(playTag(QStringLiteral("Goal"), QStringLiteral("Away"), 8000));

  PresentationPanel panel;
  panel.setTagSession(&session);
  showPanel(panel, 320);

  QTableView* table = instancesTable(panel);
  QVERIFY(table);
  QVERIFY(table->itemDelegate() != nullptr);
  QVERIFY(table->itemDelegateForColumn(0) != table->itemDelegate());
  QCOMPARE(table->itemDelegateForIndex(table->model()->index(0, 1)), table->itemDelegate());
  QCOMPARE(table->itemDelegateForIndex(table->model()->index(0, 2)), table->itemDelegate());

  int homeRow = -1;
  int awayRow = -1;
  for (int row = 0; row < table->model()->rowCount(); ++row) {
    const QString team =
        table->model()->data(table->model()->index(row, 1), Qt::DisplayRole).toString();
    if (team == QStringLiteral("Home Club")) homeRow = row;
    if (team == QStringLiteral("Away Club")) awayRow = row;
    QVERIFY(table->model()->flags(table->model()->index(row, 1)).testFlag(Qt::ItemIsEnabled));
  }
  QVERIFY(homeRow >= 0);
  QVERIFY(awayRow >= 0);
  QVERIFY(homeRow != awayRow);

  const QModelIndex awayEvent = table->model()->index(awayRow, 2);
  const QModelIndex awayTime = table->model()->index(awayRow, 0);
  const QColor playhead = Style::ThemeColors::playheadHighlight();
  const QColor ring = Style::ThemeColors::ring();

  const QImage hoverOnly = paintClipCell(table, awayEvent, QStyle::State_MouseOver);
  const QColor hoverInterior = hoverOnly.pixelColor(QPoint(hoverOnly.width() - 3, 4));
  QVERIFY(hoverInterior != playhead);
  QVERIFY(hoverInterior.red() > 180);

  const QImage focusOnly = paintClipCell(table, awayEvent, QStyle::State_HasFocus);
  expectRgb(focusOnly.pixelColor(QPoint(0, focusOnly.height() / 2)), ring,
            QStringLiteral("focus ring"));
  const QColor focusInterior = focusOnly.pixelColor(QPoint(focusOnly.width() - 3, 4));
  QVERIFY(focusInterior != playhead);
  QVERIFY(focusInterior != ring);

  const QImage selected = paintClipCell(table, awayEvent, QStyle::State_Selected);
  expectRgb(selected.pixelColor(QPoint(selected.width() - 3, 4)), playhead,
            QStringLiteral("selected fill"));
  const QImage selectedTime = paintClipCell(table, awayTime, QStyle::State_Selected);
  expectRgb(selectedTime.pixelColor(QPoint(1, 4)), ring, QStringLiteral("selected accent"));

  const QImage selectedHover =
      paintClipCell(table, awayEvent, QStyle::State_Selected | QStyle::State_MouseOver);
  expectRgb(selectedHover.pixelColor(QPoint(selectedHover.width() - 3, 4)), playhead,
            QStringLiteral("selected hover fill"));

  const QImage selectedFocus =
      paintClipCell(table, awayEvent, QStyle::State_Selected | QStyle::State_HasFocus);
  expectRgb(selectedFocus.pixelColor(QPoint(0, selectedFocus.height() / 2)), ring,
            QStringLiteral("selected focus ring"));
  expectRgb(selectedFocus.pixelColor(QPoint(selectedFocus.width() - 3, 4)), playhead,
            QStringLiteral("selected focus fill"));

  panel.setCurrentClip(0, 3000, 5000);
  QVERIFY(!table->selectionModel()->isRowSelected(homeRow, QModelIndex()));
  const QModelIndex homeEvent = table->model()->index(homeRow, 2);
  const QModelIndex homeTime = table->model()->index(homeRow, 0);
  const QImage currentClip = paintClipCell(table, homeEvent, QStyle::State_None);
  expectRgb(currentClip.pixelColor(QPoint(currentClip.width() - 3, 4)), playhead,
            QStringLiteral("current clip fill"));
  const QImage currentTime = paintClipCell(table, homeTime, QStyle::State_None);
  expectRgb(currentTime.pixelColor(QPoint(1, 4)), ring, QStringLiteral("current clip accent"));

  const QImage currentRendered = renderClipViewport(table);
  const QRect homeEventRect = table->visualRect(homeEvent);
  const QRect homeTimeRect = table->visualRect(homeTime);
  QVERIFY(homeEventRect.width() > 8);
  QVERIFY(homeTimeRect.width() > 8);
  expectRgb(currentRendered.pixelColor(homeEventRect.topLeft() + QPoint(homeEventRect.width() - 3, 4)),
            playhead, QStringLiteral("rendered current clip"));
  expectRgb(currentRendered.pixelColor(homeTimeRect.topLeft() + QPoint(1, 4)), ring,
            QStringLiteral("rendered current accent"));
  const QRect awayEventRect = table->visualRect(awayEvent);
  const QColor renderedAway =
      currentRendered.pixelColor(awayEventRect.topLeft() + QPoint(awayEventRect.width() - 3, 4));
  QVERIFY(renderedAway != playhead);

  panel.clearCurrentClip();
  table->clearSelection();
  table->selectRow(awayRow);
  QVERIFY(table->selectionModel()->isRowSelected(awayRow, QModelIndex()));
  QVERIFY(!table->selectionModel()->isRowSelected(homeRow, QModelIndex()));
  const QImage selectedRendered = renderClipViewport(table);
  expectRgb(selectedRendered.pixelColor(awayEventRect.topLeft() + QPoint(awayEventRect.width() - 3, 4)),
            playhead, QStringLiteral("rendered selected row"));
  expectRgb(selectedRendered.pixelColor(table->visualRect(awayTime).topLeft() + QPoint(1, 4)), ring,
            QStringLiteral("rendered selected accent"));
  const QColor renderedHome =
      selectedRendered.pixelColor(homeEventRect.topLeft() + QPoint(homeEventRect.width() - 3, 4));
  QVERIFY(renderedHome != playhead);
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
    if (combo->findData(static_cast<int>(EventCatalog::EventId::CircleEntry)) >= 0) eventFilter = combo;
    if (combo->findData(QStringLiteral("Home")) >= 0) teamFilter = combo;
  }
  QVERIFY(eventFilter);
  QVERIFY(teamFilter);
  QCOMPARE(eventFilter->objectName(), QStringLiteral("PresentationEventFilter"));
  QCOMPARE(teamFilter->objectName(), QStringLiteral("PresentationTeamFilter"));
  QCOMPARE(eventFilter->sizeAdjustPolicy(), QComboBox::AdjustToMinimumContentsLengthWithIcon);
  QCOMPARE(teamFilter->sizeAdjustPolicy(), QComboBox::AdjustToMinimumContentsLengthWithIcon);
  QCOMPARE(teamFilter->maximumWidth(), 220);
  QVERIFY(eventFilter->mapTo(&panel, QPoint(0, 0)).y() !=
          teamFilter->mapTo(&panel, QPoint(0, 0)).y());
  auto* leadSpin = panel.findChild<QDoubleSpinBox*>(QStringLiteral("LeadSpinBox"));
  auto* lagSpin = panel.findChild<QDoubleSpinBox*>(QStringLiteral("LagSpinBox"));
  QVERIFY(leadSpin);
  QVERIFY(lagSpin);
  QVERIFY(leadSpin->mapTo(&panel, QPoint(0, 0)).y() != lagSpin->mapTo(&panel, QPoint(0, 0)).y());
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
  QLabel* applyLabel = applyToAllLabel(&panel);
  QVERIFY(applyButton);
  QVERIFY(applyLabel);
  QCOMPARE(applyButton->property("variant").toString(), QStringLiteral("outline"));
  QCOMPARE(applyButton->text(), QString());
  QCOMPARE(applyLabel->text(), QStringLiteral("Apply to all selected clips"));
  QCOMPARE(applyButton->accessibleName(), applyLabel->text());
  QCOMPARE(applyButton->toolTip(), AppLocale::trUi("presentation.apply_to_all_tooltip"));

  QVERIFY(table->model()->setData(table->model()->index(0, 0), Qt::Checked, Qt::CheckStateRole));
  QCOMPARE(panel.selectedTagIndexes().size(), 1);
  QCOMPARE(applyLabel->text(), QStringLiteral("Apply to the selected clip"));
  QCOMPARE(applyButton->accessibleName(), applyLabel->text());
  QCOMPARE(applyButton->toolTip(), AppLocale::trUi("presentation.apply_to_one_tooltip"));
  const int singularButtonWidth = applyButton->width();

  QVERIFY(table->model()->setData(table->model()->index(1, 0), Qt::Checked, Qt::CheckStateRole));
  QCOMPARE(panel.selectedTagIndexes().size(), 2);
  QCOMPARE(applyLabel->text(), QStringLiteral("Apply to all selected clips"));
  QCOMPARE(applyButton->width(), singularButtonWidth);

  AppLocale::setLanguage(AppLocale::Language::Spanish);
  panel.applyUiStrings();
  showPanel(panel, 260);
  QCOMPARE(panel.selectedTagIndexes().size(), 2);
  QCOMPARE(applyLabel->text(), QStringLiteral("Aplicar a todos los clips seleccionados"));
  QCOMPARE(applyButton->toolTip(), AppLocale::trUi("presentation.apply_to_all_tooltip"));

  QVERIFY(table->model()->setData(table->model()->index(0, 0), Qt::Unchecked, Qt::CheckStateRole));
  QCOMPARE(panel.selectedTagIndexes().size(), 1);
  QCOMPARE(applyLabel->text(), QStringLiteral("Aplicar al clip seleccionado"));
  QCOMPARE(applyButton->toolTip(), AppLocale::trUi("presentation.apply_to_one_tooltip"));
  QVERIFY(applyLabel->wordWrap());
  QVERIFY(applyButton->rect().contains(applyLabel->geometry()));

  QVERIFY(table->model()->setData(table->model()->index(1, 0), Qt::Unchecked, Qt::CheckStateRole));
  QCOMPARE(panel.selectedTagIndexes().size(), 0);
  QCOMPARE(applyLabel->text(), QStringLiteral("Aplicar a todos los clips seleccionados"));
  showPanel(panel, 260);
  const int pluralAdvance = applyLabel->fontMetrics().horizontalAdvance(applyLabel->text());
  const QRect pluralTextRect = applyLabel->contentsRect();
  QVERIFY2(pluralAdvance > pluralTextRect.width(),
           qPrintable(QStringLiteral("plural advance %1 contents %2 button %3")
                          .arg(pluralAdvance)
                          .arg(pluralTextRect.width())
                          .arg(applyButton->width())));
  const QRect pluralNeeded = applyLabel->fontMetrics().boundingRect(
      QRect(0, 0, pluralTextRect.width(), 10000),
      Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextWordWrap, applyLabel->text());
  QVERIFY2(pluralNeeded.width() <= pluralTextRect.width() + 1 &&
               pluralNeeded.height() <= pluralTextRect.height() + 1,
           qPrintable(QStringLiteral("plural needs %1x%2 in %3x%4")
                          .arg(pluralNeeded.width())
                          .arg(pluralNeeded.height())
                          .arg(pluralTextRect.width())
                          .arg(pluralTextRect.height())));
  QVERIFY(pluralTextRect.height() > applyLabel->fontMetrics().height());
  QVERIFY(applyButton->width() <= 260);
  QVERIFY(applyButton->rect().contains(applyLabel->geometry()));

  const QString longCode = QStringLiteral("ALEKSANDRA WISNIEWSKA-KOWALCZYK");
  session.setGameMetadata(QDate(), longCode, QStringLiteral("JO"));
  showPanel(panel, 260);
  const int longHomeIndex = teamFilter->findData(QStringLiteral("Home"));
  QCOMPARE(teamFilter->itemText(longHomeIndex), longCode);
  QCOMPARE(teamFilter->itemText(teamFilter->findData(QStringLiteral("Away"))), QStringLiteral("JO"));
  QCOMPARE(teamFilter->itemData(longHomeIndex, Qt::ToolTipRole).toString(), homeName);
  teamFilter->setCurrentIndex(longHomeIndex);
  showPanel(panel, 260);
  QCOMPARE(teamFilter->toolTip(), homeName);
  QVERIFY(teamFilter->width() <= 220);
  const int longAbbreviationAdvance = teamFilter->fontMetrics().horizontalAdvance(longCode);
  QVERIFY(longAbbreviationAdvance > teamFilter->width());
  teamFilter->setCurrentIndex(0);
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
