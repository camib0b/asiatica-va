#include "AppLocale.h"
#include "EventCatalog.h"
#include "EventDefaults.h"
#include "TagEvent.h"
#include "TagSession.h"
#include "XmlEventMappingDialog.h"

#include <QApplication>
#include <QComboBox>
#include <QDate>
#include <QDialog>
#include <QEvent>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTest>
#include <QTimer>
#include <QWheelEvent>

namespace {

constexpr int kCodeColumn = 0;
constexpr int kTeamColumn = 1;
constexpr int kEventColumn = 2;
constexpr int kCountColumn = 3;
constexpr int kImportColumn = 4;

class WarningCapture : public QObject {
public:
    QString text;
    int seen = 0;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::Show) {
            if (auto* box = qobject_cast<QMessageBox*>(watched)) {
                text = box->text();
                ++seen;
                QTimer::singleShot(0, box, &QDialog::accept);
            }
        }
        return QObject::eventFilter(watched, event);
    }
};

class InstalledWarningCapture {
public:
    explicit InstalledWarningCapture(WarningCapture* capture) : capture_(capture) {
        qApp->installEventFilter(capture_);
    }
    ~InstalledWarningCapture() { qApp->removeEventFilter(capture_); }

private:
    WarningCapture* capture_;
};

XmlImporter::ParsedInstance makeInstance(const QString& code, qint64 startMs, qint64 endMs) {
    XmlImporter::ParsedInstance instance;
    instance.code = code;
    instance.startMs = startMs;
    instance.endMs = endMs;
    return instance;
}

void setSessionAbbrevs(TagSession* session) {
    session->setGameMetadata(QDate(), QStringLiteral("HOME"), QStringLiteral("AWAY"));
}

int rowForCode(QTableWidget* table, const QString& code) {
    for (int row = 0; row < table->rowCount(); ++row) {
        const QTableWidgetItem* item = table->item(row, kCodeColumn);
        if (item && item->text() == code) return row;
    }
    return -1;
}

QComboBox* comboInCell(QTableWidget* table, int row, int column) {
    QWidget* cell = table ? table->cellWidget(row, column) : nullptr;
    return cell ? cell->findChild<QComboBox*>() : nullptr;
}

QString teamForCode(QTableWidget* table, const QString& code) {
    const int row = rowForCode(table, code);
    QComboBox* teamCombo = comboInCell(table, row, kTeamColumn);
    if (!teamCombo) return {};
    return teamCombo->currentData().toString();
}

QVector<XmlImporter::ParsedInstance> mixedCaseInstances() {
    return {
        makeInstance(QStringLiteral("HOME GOAL+"), 0, 20000),
        makeInstance(QStringLiteral("  home goal+ "), 30000, 50000),
        makeInstance(QStringLiteral("AWAY SHOT+"), 60000, 80000),
    };
}

}  // namespace

class XmlEventMappingDialogTest : public QObject {
    Q_OBJECT

private slots:
    void caseAndSurroundingWhitespaceShareOneMapping();
    void sameAbbreviationBlocksConfirm();
    void selectAllAndDeselectAllImport();
    void sortByCodeKeepsWidgetsAligned();
    void unfocusedComboIgnoresWheel();
};

void XmlEventMappingDialogTest::caseAndSurroundingWhitespaceShareOneMapping() {
    WarningCapture warnings;
    InstalledWarningCapture capture(&warnings);

    TagSession session;
    setSessionAbbrevs(&session);
    XmlEventMappingDialog dialog(mixedCaseInstances(), 0, &session);

    auto* table = dialog.findChild<QTableWidget*>(QStringLiteral("XmlEventMappingTable"));
    QVERIFY(table);
    QCOMPARE(table->rowCount(), 2);

    const int goalRow = rowForCode(table, QStringLiteral("HOME GOAL+"));
    QVERIFY(goalRow >= 0);
    QCOMPARE(rowForCode(table, QStringLiteral("home goal+")), -1);
    QCOMPARE(rowForCode(table, QStringLiteral("  home goal+ ")), -1);
    QCOMPARE(table->item(goalRow, kCountColumn)->text(), QStringLiteral("2"));
    QCOMPARE(teamForCode(table, QStringLiteral("HOME GOAL+")), QStringLiteral("Home"));
    QCOMPARE(teamForCode(table, QStringLiteral("AWAY SHOT+")), QStringLiteral("Away"));

    auto* importButton = dialog.findChild<QPushButton*>(QStringLiteral("XmlMappingImportButton"));
    QVERIFY(importButton);
    QVERIFY(importButton->isEnabled());
    importButton->click();

    QCOMPARE(warnings.seen, 0);
    QCOMPARE(dialog.result(), static_cast<int>(QDialog::Accepted));

    const XmlEventMappingDialog::ImportMappingResult imported = dialog.importResult();
    QCOMPARE(imported.skippedInstanceCount, 0);
    QCOMPARE(imported.tags.size(), 3);
    QCOMPARE(imported.tags.at(0).mainEventId, EventCatalog::EventId::Goal);
    QCOMPARE(imported.tags.at(0).team, QStringLiteral("Home"));
    QCOMPARE(imported.tags.at(0).startMs, qint64{0});
    QCOMPARE(imported.tags.at(1).mainEventId, EventCatalog::EventId::Goal);
    QCOMPARE(imported.tags.at(1).team, QStringLiteral("Home"));
    QCOMPARE(imported.tags.at(1).startMs, qint64{30000});
    QCOMPARE(imported.tags.at(2).mainEventId, EventCatalog::EventId::Shot);
    QCOMPARE(imported.tags.at(2).team, QStringLiteral("Away"));
    QCOMPARE(imported.tags.at(2).startMs, qint64{60000});
}

void XmlEventMappingDialogTest::sameAbbreviationBlocksConfirm() {
    WarningCapture warnings;
    InstalledWarningCapture capture(&warnings);

    TagSession session;
    setSessionAbbrevs(&session);
    XmlEventMappingDialog dialog(mixedCaseInstances(), 0, &session);

    auto* homeCombo = dialog.findChild<QComboBox*>(QStringLiteral("XmlHomeAbbrevCombo"));
    auto* awayCombo = dialog.findChild<QComboBox*>(QStringLiteral("XmlAwayAbbrevCombo"));
    auto* conflictLabel = dialog.findChild<QLabel*>(QStringLiteral("XmlAbbrevConflictLabel"));
    auto* importButton = dialog.findChild<QPushButton*>(QStringLiteral("XmlMappingImportButton"));
    auto* table = dialog.findChild<QTableWidget*>(QStringLiteral("XmlEventMappingTable"));
    QVERIFY(homeCombo);
    QVERIFY(awayCombo);
    QVERIFY(conflictLabel);
    QVERIFY(importButton);
    QVERIFY(table);

    QVERIFY(importButton->isEnabled());
    QVERIFY(conflictLabel->text().isEmpty());
    QCOMPARE(teamForCode(table, QStringLiteral("HOME GOAL+")), QStringLiteral("Home"));

    const int awayHomeIndex = awayCombo->findText(QStringLiteral("HOME"));
    QVERIFY(awayHomeIndex >= 0);
    awayCombo->setItemText(awayHomeIndex, QStringLiteral(" home "));
    awayCombo->setCurrentIndex(awayHomeIndex);

    const QString expectedMessage = AppLocale::trUi("xml_import.mapping_same_abbrev");
    QCOMPARE(AppLocale::trUiForLanguage("xml_import.mapping_same_abbrev",
                                       AppLocale::Language::Spanish),
             QStringLiteral(
                 "El local y el visitante no pueden usar la misma abreviatura para el XML."));
    QCOMPARE(conflictLabel->text(), expectedMessage);
    QVERIFY(!importButton->isEnabled());
    QCOMPARE(teamForCode(table, QStringLiteral("HOME GOAL+")), QStringLiteral("Home"));
    QCOMPARE(teamForCode(table, QStringLiteral("AWAY SHOT+")), QStringLiteral("Away"));

    importButton->setEnabled(true);
    importButton->click();

    QCOMPARE(warnings.seen, 1);
    QCOMPARE(warnings.text, expectedMessage);
    QCOMPARE(dialog.result(), static_cast<int>(QDialog::Rejected));
    QVERIFY(dialog.importResult().tags.isEmpty());
    QVERIFY(!importButton->isEnabled());

    const int awayIndex = awayCombo->findText(QStringLiteral("AWAY"));
    QVERIFY(awayIndex >= 0);
    awayCombo->setCurrentIndex(awayIndex);

    QVERIFY(importButton->isEnabled());
    QVERIFY(conflictLabel->text().isEmpty());

    warnings.seen = 0;
    warnings.text.clear();
    importButton->click();

    QCOMPARE(warnings.seen, 0);
    QCOMPARE(dialog.result(), static_cast<int>(QDialog::Accepted));
    const XmlEventMappingDialog::ImportMappingResult imported = dialog.importResult();
    QCOMPARE(imported.tags.size(), 3);
    QCOMPARE(imported.tags.at(0).team, QStringLiteral("Home"));
    QCOMPARE(imported.tags.at(1).team, QStringLiteral("Home"));
    QCOMPARE(imported.tags.at(2).team, QStringLiteral("Away"));
}

void XmlEventMappingDialogTest::selectAllAndDeselectAllImport() {
    TagSession session;
    setSessionAbbrevs(&session);
    XmlEventMappingDialog dialog(mixedCaseInstances(), 0, &session);

    auto* table = dialog.findChild<QTableWidget*>(QStringLiteral("XmlEventMappingTable"));
    auto* deselectAll =
        dialog.findChild<QPushButton*>(QStringLiteral("XmlMappingDeselectAllImportButton"));
    auto* selectAll =
        dialog.findChild<QPushButton*>(QStringLiteral("XmlMappingSelectAllImportButton"));
    QVERIFY(table);
    QVERIFY(deselectAll);
    QVERIFY(selectAll);

    deselectAll->click();
    for (int row = 0; row < table->rowCount(); ++row) {
        const QTableWidgetItem* importItem = table->item(row, kImportColumn);
        QVERIFY(importItem);
        QCOMPARE(importItem->checkState(), Qt::Unchecked);
    }

    selectAll->click();
    for (int row = 0; row < table->rowCount(); ++row) {
        const QTableWidgetItem* importItem = table->item(row, kImportColumn);
        QVERIFY(importItem);
        QCOMPARE(importItem->checkState(), Qt::Checked);
    }
}

void XmlEventMappingDialogTest::sortByCodeKeepsWidgetsAligned() {
    TagSession session;
    setSessionAbbrevs(&session);
    const QVector<XmlImporter::ParsedInstance> instances = {
        makeInstance(QStringLiteral("HOME GOAL+"), 0, 1000),
        makeInstance(QStringLiteral("AWAY SHOT+"), 2000, 3000),
    };
    XmlEventMappingDialog dialog(instances, 0, &session);

    auto* table = dialog.findChild<QTableWidget*>(QStringLiteral("XmlEventMappingTable"));
    QVERIFY(table);
    QCOMPARE(table->rowCount(), 2);

    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));

    QHeaderView* header = table->horizontalHeader();
    QVERIFY(header);
    const int sectionIndex = header->visualIndex(kCodeColumn);
    const QPoint headerPoint(header->sectionViewportPosition(sectionIndex) + 8,
                             header->height() / 2);
    QCOMPARE(header->sortIndicatorSection(), kCodeColumn);
    QCOMPARE(header->sortIndicatorOrder(), Qt::AscendingOrder);
    QCOMPARE(table->item(0, kCodeColumn)->text(), QStringLiteral("AWAY SHOT+"));

    QTest::mouseClick(header->viewport(), Qt::LeftButton, {}, headerPoint);
    QCOMPARE(header->sortIndicatorOrder(), Qt::DescendingOrder);

    QCOMPARE(table->item(0, kCodeColumn)->text(), QStringLiteral("HOME GOAL+"));
    QComboBox* firstEventCombo = comboInCell(table, 0, kEventColumn);
    QVERIFY(firstEventCombo);
    QCOMPARE(firstEventCombo->currentText(),
             TagEvent::canonicalName(TagEvent::catalogKey(EventCatalog::EventId::Goal)));
    QCOMPARE(teamForCode(table, QStringLiteral("HOME GOAL+")), QStringLiteral("Home"));
}

void XmlEventMappingDialogTest::unfocusedComboIgnoresWheel() {
    TagSession session;
    setSessionAbbrevs(&session);
    XmlEventMappingDialog dialog(mixedCaseInstances(), 0, &session);

    auto* table = dialog.findChild<QTableWidget*>(QStringLiteral("XmlEventMappingTable"));
    QVERIFY(table);
    const int goalRow = rowForCode(table, QStringLiteral("HOME GOAL+"));
    QVERIFY(goalRow >= 0);

    QComboBox* eventCombo = comboInCell(table, goalRow, kEventColumn);
    QVERIFY(eventCombo);
    QVERIFY(!eventCombo->hasFocus());
    const int initialIndex = eventCombo->currentIndex();

    const QPoint localPoint = eventCombo->rect().center();
    QWheelEvent wheel(localPoint, eventCombo->mapToGlobal(localPoint), QPoint(), QPoint(0, 120),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(eventCombo, &wheel);

    QCOMPARE(eventCombo->currentIndex(), initialIndex);
}

int runXmlEventMappingDialogTests(int argc, char** argv) {
    XmlEventMappingDialogTest tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "XmlEventMappingDialogTests.moc"
