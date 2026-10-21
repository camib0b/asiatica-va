#include "AppLocale.h"
#include "ExportOutputPath.h"
#include "GameSetupWindow.h"
#include "PresentationPanel.h"
#include "TeamColorPicker.h"
#include "XmlImportTimeline.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDate>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QStyle>
#include <QStyleOptionButton>
#include <QStyleOptionComboBox>
#include <QStyleOptionSpinBox>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

class UiPolicyTest : public QObject {
    Q_OBJECT

private slots:
    void spanishKeepsSweepAndTranslatesPost();
    void secondExportSuggestsANumberedName();
    void samePathAndTempDirectoryLease();
    void eventsBeforeVideoStartKeepTheirLength();
    void partialOverlapKeepsOnlyTheVideoPart();
    void abbreviationFollowsTheTeamNameUntilTyped();
    void loadedAbbreviationStaysWhenTheNameChanges();
    void sameAbbreviationBlocksContinue();
    void matchingDerivedAbbreviationsBlockContinue();
    void applyToSelectedLabelStaysWholeInANarrowPanel();
    void hexNormalizesWhenEditingFinishes();
    void invalidHexShowsAnInlineHint();
};

void UiPolicyTest::spanishKeepsSweepAndTranslatesPost() {
    const AppLocale::Language previous = AppLocale::currentLanguage();
    AppLocale::setLanguage(AppLocale::Language::Spanish);

    QCOMPARE(AppLocale::trEvent(QStringLiteral("Sweep")), QStringLiteral("Sweep"));
    QCOMPARE(AppLocale::trEvent(QStringLiteral("Swept")), QStringLiteral("Sweep"));
    QCOMPARE(AppLocale::trEvent(QStringLiteral("Post")), QStringLiteral("Poste"));
    QCOMPARE(AppLocale::trEvent(QStringLiteral("Stick")), QStringLiteral("Palo"));
    QCOMPARE(AppLocale::trEvent(QStringLiteral("Flick")), QStringLiteral("Flick"));
    QCOMPARE(AppLocale::trEvent(QStringLiteral("Push")), QStringLiteral("Push"));
    QCOMPARE(AppLocale::trUi("export.output_exists"),
             QStringLiteral("Ya hay un video guardado con este nombre. Si exportas ahora, se reemplaza el anterior."));
    QCOMPARE(AppLocale::trUi("export.output_rename"), QStringLiteral("Cambiar nombre"));
    QCOMPARE(AppLocale::trUi("export.busy_cancel"), QStringLiteral("Cancelar exportación"));
    QCOMPARE(AppLocale::trUi("setup.color_hex_hint"),
             QStringLiteral("Usa 6 dígitos hex, con o sin #."));
    QCOMPARE(AppLocale::trUi("xml_import.adjusted_before_start_one"),
             QStringLiteral("1 evento empezaba antes del video. Lo dejamos desde el inicio, con su duración."));

    AppLocale::setLanguage(AppLocale::Language::English);
    QCOMPARE(AppLocale::trEvent(QStringLiteral("Sweep")), QStringLiteral("Sweep"));
    QCOMPARE(AppLocale::trEvent(QStringLiteral("Post")), QStringLiteral("Post"));
    QCOMPARE(AppLocale::trUi("export.busy_wait"), QStringLiteral("Wait"));

    AppLocale::setLanguage(previous);
}

void UiPolicyTest::secondExportSuggestsANumberedName() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString firstPath = directory.filePath(QStringLiteral("Final.mp4"));
    QFile firstFile(firstPath);
    QVERIFY(firstFile.open(QIODevice::WriteOnly));
    firstFile.write("first");
    firstFile.close();

    const auto pathIsTaken = [](const QString& path) { return QFileInfo::exists(path); };
    const QString secondPath = ExportOutputPath::nonCollidingFilePath(firstPath, pathIsTaken);
    QCOMPARE(QFileInfo(secondPath).fileName(), QStringLiteral("Final (2).mp4"));
    QVERIFY(!QFileInfo::exists(secondPath));

    QFile secondFile(secondPath);
    QVERIFY(secondFile.open(QIODevice::WriteOnly));
    secondFile.write("second");
    secondFile.close();

    const QString thirdPath = ExportOutputPath::nonCollidingFilePath(firstPath, pathIsTaken);
    QCOMPARE(QFileInfo(thirdPath).fileName(), QStringLiteral("Final (3).mp4"));

    const QString numberedAgain = ExportOutputPath::nonCollidingFilePath(secondPath, pathIsTaken);
    QCOMPARE(QFileInfo(numberedAgain).fileName(), QStringLiteral("Final (3).mp4"));
}

void UiPolicyTest::samePathAndTempDirectoryLease() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString sourcePath = directory.filePath(QStringLiteral("source.mp4"));
    QFile sourceFile(sourcePath);
    QVERIFY(sourceFile.open(QIODevice::WriteOnly));
    sourceFile.write("source");
    sourceFile.close();

    QVERIFY(ExportOutputPath::pathsReferToSameFile(sourcePath, QFileInfo(sourcePath).absoluteFilePath()));
    QVERIFY(!ExportOutputPath::pathsReferToSameFile(sourcePath, directory.filePath(QStringLiteral("other.mp4"))));
    QVERIFY(ExportOutputPath::pathIsInsideDirectory(sourcePath, directory.path()));
    QVERIFY(!ExportOutputPath::pathIsInsideDirectory(sourcePath, directory.path() + QStringLiteral("-other")));

    const auto blocksSource = [&sourcePath](const QString& path) {
        return ExportOutputPath::pathsReferToSameFile(path, sourcePath) || QFileInfo::exists(path);
    };
    const QString renamed = ExportOutputPath::nonCollidingFilePath(sourcePath, blocksSource);
    QCOMPARE(QFileInfo(renamed).fileName(), QStringLiteral("source (2).mp4"));
    QVERIFY(!ExportOutputPath::pathsReferToSameFile(renamed, sourcePath));

    const QString directoryPath = QDir(directory.path()).absolutePath();
    QVERIFY(ExportOutputPath::directoryIsNeededByExport(
        directoryPath, {directoryPath}, {}));
    QVERIFY(ExportOutputPath::directoryIsNeededByExport(
        directoryPath, {}, {sourcePath}));
    QVERIFY(!ExportOutputPath::directoryIsNeededByExport(
        directory.filePath(QStringLiteral("elsewhere")), {}, {sourcePath}));
}

void UiPolicyTest::eventsBeforeVideoStartKeepTheirLength() {
    const ImportedClipInterval entirelyBefore =
        clipIntervalOnVideoTimeline(0, 3500, -10000);
    QVERIFY(entirelyBefore.adjustedBeforeVideoStart);
    QCOMPARE(entirelyBefore.startMs, qint64{0});
    QCOMPARE(entirelyBefore.endMs, qint64{3500});

    const ImportedClipInterval insideVideo =
        clipIntervalOnVideoTimeline(12000, 15000, -10000);
    QVERIFY(!insideVideo.adjustedBeforeVideoStart);
    QCOMPARE(insideVideo.startMs, qint64{2000});
    QCOMPARE(insideVideo.endMs, qint64{5000});
}

void UiPolicyTest::partialOverlapKeepsOnlyTheVideoPart() {
    const ImportedClipInterval overlap =
        clipIntervalOnVideoTimeline(8000, 12000, -10000);
    QVERIFY(overlap.adjustedBeforeVideoStart);
    QCOMPARE(overlap.startMs, qint64{0});
    QCOMPARE(overlap.endMs, qint64{2000});

    const ImportedClipInterval endsAtStart =
        clipIntervalOnVideoTimeline(7000, 10000, -10000);
    QVERIFY(endsAtStart.adjustedBeforeVideoStart);
    QCOMPARE(endsAtStart.startMs, qint64{0});
    QCOMPARE(endsAtStart.endMs, qint64{3000});
}

void UiPolicyTest::abbreviationFollowsTheTeamNameUntilTyped() {
    GameSetupWindow window;
    auto* homeName = window.findChild<QLineEdit*>(QStringLiteral("HomeTeamNameEdit"));
    auto* homeAbbrev = window.findChild<QLineEdit*>(QStringLiteral("HomeTeamAbbrevEdit"));
    auto* awayName = window.findChild<QLineEdit*>(QStringLiteral("AwayTeamNameEdit"));
    auto* awayAbbrev = window.findChild<QLineEdit*>(QStringLiteral("AwayTeamAbbrevEdit"));
    QVERIFY(homeName);
    QVERIFY(homeAbbrev);
    QVERIFY(awayName);
    QVERIFY(awayAbbrev);

    homeName->setText(QStringLiteral("Old Girls"));
    emit homeName->editingFinished();
    QCOMPARE(homeAbbrev->text(), QStringLiteral("OGI"));

    homeName->setText(QStringLiteral("Old Gabs"));
    emit homeName->editingFinished();
    QCOMPARE(homeAbbrev->text(), QStringLiteral("OGA"));

    homeAbbrev->setText(QStringLiteral("ZZZ"));
    emit homeAbbrev->textEdited(homeAbbrev->text());
    homeName->setText(QStringLiteral("River Plate"));
    emit homeName->editingFinished();
    QCOMPARE(homeAbbrev->text(), QStringLiteral("ZZZ"));

    homeAbbrev->clear();
    emit homeAbbrev->textEdited(homeAbbrev->text());
    QCOMPARE(homeAbbrev->text(), QStringLiteral("RPL"));

    awayName->setText(QStringLiteral("New Club"));
    emit awayName->editingFinished();
    QCOMPARE(awayAbbrev->text(), QStringLiteral("NCL"));
    awayName->setText(QStringLiteral("New City"));
    emit awayName->editingFinished();
    QCOMPARE(awayAbbrev->text(), QStringLiteral("NCI"));
}

void UiPolicyTest::loadedAbbreviationStaysWhenTheNameChanges() {
    GameSetupWindow window;
    auto* homeName = window.findChild<QLineEdit*>(QStringLiteral("HomeTeamNameEdit"));
    auto* homeAbbrev = window.findChild<QLineEdit*>(QStringLiteral("HomeTeamAbbrevEdit"));
    QVERIFY(homeName);
    QVERIFY(homeAbbrev);

    window.setTeamDefaults(QStringLiteral("Old Girls"), QStringLiteral("Old Gabs"), QString(), QString());
    window.setMetadataDefaults(QDate::currentDate(), QStringLiteral("ABC"), QString());
    homeName->setText(QStringLiteral("Universidad Catolica"));
    emit homeName->editingFinished();
    QCOMPARE(homeAbbrev->text(), QStringLiteral("ABC"));
}

void UiPolicyTest::sameAbbreviationBlocksContinue() {
    const AppLocale::Language previous = AppLocale::currentLanguage();
    AppLocale::setLanguage(AppLocale::Language::Spanish);

    QCOMPARE(AppLocale::trUiForLanguage("setup.same_abbrev", AppLocale::Language::English),
             QStringLiteral("Home and away can't use the same abbreviation."));
    QCOMPARE(AppLocale::trUiForLanguage("setup.same_abbrev", AppLocale::Language::Spanish),
             QStringLiteral("El local y el visitante no pueden usar la misma abreviatura."));

    GameSetupWindow window;
    window.setVideoPath(QStringLiteral("/tmp/match.mp4"));
    window.setTeamDefaults(QStringLiteral("Universidad Catolica"), QStringLiteral("Old Girls"),
                           QStringLiteral("#0000FF"), QStringLiteral("#FF0000"));

    auto* homeAbbrev = window.findChild<QLineEdit*>(QStringLiteral("HomeTeamAbbrevEdit"));
    auto* awayAbbrev = window.findChild<QLineEdit*>(QStringLiteral("AwayTeamAbbrevEdit"));
    auto* conflict = window.findChild<QLabel*>(QStringLiteral("SetupAbbrevConflictLabel"));
    auto* continueButton = window.findChild<QPushButton*>(QStringLiteral("GameSetupContinueButton"));
    QVERIFY(homeAbbrev);
    QVERIFY(awayAbbrev);
    QVERIFY(conflict);
    QVERIFY(continueButton);
    QVERIFY(conflict->wordWrap());
    QVERIFY(conflict->isHidden());
    QVERIFY(continueButton->isEnabled());

    homeAbbrev->setText(QStringLiteral("abc"));
    emit homeAbbrev->textEdited(homeAbbrev->text());
    QVERIFY(conflict->isHidden());

    awayAbbrev->setText(QStringLiteral("ABC"));
    emit awayAbbrev->textEdited(awayAbbrev->text());
    QCOMPARE(conflict->text(), AppLocale::trUi("setup.same_abbrev"));
    QVERIFY(!conflict->isHidden());
    QVERIFY(!continueButton->isEnabled());
    QCOMPARE(continueButton->toolTip(), conflict->text());

    QSignalSpy confirmed(&window, &GameSetupWindow::gameSetupConfirmed);
    continueButton->setEnabled(true);
    continueButton->click();
    QCOMPARE(confirmed.count(), 0);
    QVERIFY(!continueButton->isEnabled());

    homeAbbrev->setText(QStringLiteral(" AB"));
    emit homeAbbrev->textEdited(homeAbbrev->text());
    awayAbbrev->setText(QStringLiteral("ab"));
    emit awayAbbrev->textEdited(awayAbbrev->text());
    QCOMPARE(conflict->text(), AppLocale::trUi("setup.same_abbrev"));
    QVERIFY(!continueButton->isEnabled());

    awayAbbrev->setText(QStringLiteral("ax"));
    emit awayAbbrev->textEdited(awayAbbrev->text());
    QVERIFY(conflict->isHidden());
    QVERIFY(conflict->text().isEmpty());
    QVERIFY(continueButton->isEnabled());

    continueButton->click();
    QCOMPARE(confirmed.count(), 1);
    const QList<QVariant> arguments = confirmed.takeFirst();
    QCOMPARE(arguments.at(6).toString(), QStringLiteral("AB"));
    QCOMPARE(arguments.at(7).toString(), QStringLiteral("AX"));

    AppLocale::setLanguage(previous);
}

void UiPolicyTest::matchingDerivedAbbreviationsBlockContinue() {
    GameSetupWindow window;
    window.setVideoPath(QStringLiteral("/tmp/match.mp4"));
    window.setTeamDefaults(QStringLiteral("Old Girls"), QStringLiteral("Old Girls"),
                           QStringLiteral("#0000FF"), QStringLiteral("#FF0000"));

    auto* homeName = window.findChild<QLineEdit*>(QStringLiteral("HomeTeamNameEdit"));
    auto* awayName = window.findChild<QLineEdit*>(QStringLiteral("AwayTeamNameEdit"));
    auto* homeAbbrev = window.findChild<QLineEdit*>(QStringLiteral("HomeTeamAbbrevEdit"));
    auto* awayAbbrev = window.findChild<QLineEdit*>(QStringLiteral("AwayTeamAbbrevEdit"));
    auto* conflict = window.findChild<QLabel*>(QStringLiteral("SetupAbbrevConflictLabel"));
    auto* continueButton = window.findChild<QPushButton*>(QStringLiteral("GameSetupContinueButton"));
    QVERIFY(homeName);
    QVERIFY(awayName);
    QVERIFY(homeAbbrev);
    QVERIFY(awayAbbrev);
    QVERIFY(conflict);
    QVERIFY(continueButton);

    emit homeName->editingFinished();
    emit awayName->editingFinished();
    QCOMPARE(homeAbbrev->text(), QStringLiteral("OGI"));
    QCOMPARE(awayAbbrev->text(), QStringLiteral("OGI"));
    QVERIFY(!conflict->isHidden());
    QVERIFY(!continueButton->isEnabled());

    QSignalSpy confirmed(&window, &GameSetupWindow::gameSetupConfirmed);
    continueButton->setEnabled(true);
    continueButton->click();
    QCOMPARE(confirmed.count(), 0);

    awayName->setText(QStringLiteral("Old Gabs"));
    emit awayName->editingFinished();
    QCOMPARE(awayAbbrev->text(), QStringLiteral("OGA"));
    QVERIFY(conflict->isHidden());
    QVERIFY(continueButton->isEnabled());
}

namespace {

QString loadThemeStyleSheet() {
    QFile themeFile(QStringLiteral(AVA_THEME_QSS_PATH));
    if (!themeFile.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return QString::fromUtf8(themeFile.readAll());
}

QRect labelTextRect(const QLabel* label) {
    const QRect contents = label->contentsRect();
    return contents.isValid() ? contents : label->rect();
}

QString wrappedLabelClip(const QLabel* label) {
    if (!label) return QStringLiteral("missing label");
    const QRect textRect = labelTextRect(label);
    if (textRect.width() <= 0 || textRect.height() <= 0) {
        return QStringLiteral("label size %1x%2").arg(label->width()).arg(label->height());
    }
    const QRect needed = label->fontMetrics().boundingRect(
        QRect(0, 0, textRect.width(), 10000),
        Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextWordWrap, label->text());
    if (needed.width() > textRect.width() || needed.height() > textRect.height()) {
        return QStringLiteral("'%1' needs %2x%3 in %4x%5")
            .arg(label->text())
            .arg(needed.width())
            .arg(needed.height())
            .arg(textRect.width())
            .arg(textRect.height());
    }
    return {};
}

QString nativeButtonClip(const QPushButton* button) {
    if (!button) return QStringLiteral("missing button");
    QStyleOptionButton option;
    option.initFrom(button);
    option.text = button->text();
    const QRect contents =
        button->style()->subElementRect(QStyle::SE_PushButtonContents, &option, button);
    const int advance = button->fontMetrics().horizontalAdvance(button->text().remove(QLatin1Char('&')));
    if (advance <= contents.width()) return {};
    return QStringLiteral("'%1' advance %2 contents %3 button %4")
        .arg(button->text())
        .arg(advance)
        .arg(contents.width())
        .arg(button->width());
}

void settlePanel(QWidget& panel) {
    if (panel.layout()) panel.layout()->activate();
    QApplication::sendPostedEvents();
    if (panel.layout()) panel.layout()->activate();
}

QString labelClip(const QLabel* label) {
    if (!label) return QStringLiteral("missing label");
    if (!label->isVisible()) return {};
    if (label->width() <= 0 || label->height() <= 0) {
        return QStringLiteral("'%1' size %2x%3")
            .arg(label->text())
            .arg(label->width())
            .arg(label->height());
    }
    const QRect textRect = labelTextRect(label);
    if (label->wordWrap()) {
        const int flags = static_cast<int>(label->alignment()) | Qt::TextWordWrap;
        const QRect needed = label->fontMetrics().boundingRect(
            QRect(0, 0, textRect.width(), 10000), flags, label->text());
        if (needed.width() > textRect.width() + 1 || needed.height() > textRect.height() + 1) {
            return QStringLiteral("'%1' needs %2x%3 in %4x%5")
                .arg(label->text())
                .arg(needed.width())
                .arg(needed.height())
                .arg(textRect.width())
                .arg(textRect.height());
        }
        return {};
    }
    const int advance = label->fontMetrics().horizontalAdvance(label->text());
    if (advance > textRect.width()) {
        return QStringLiteral("'%1' advance %2 width %3")
            .arg(label->text())
            .arg(advance)
            .arg(textRect.width());
    }
    return {};
}

QString toolButtonClip(const QToolButton* button) {
    if (!button) return QStringLiteral("missing tool button");
    if (button->width() + 1 < button->sizeHint().width()) {
        return QStringLiteral("'%1' width %2 hint %3")
            .arg(button->text())
            .arg(button->width())
            .arg(button->sizeHint().width());
    }
    return {};
}

QString checkBoxClip(const QCheckBox* box) {
    if (!box || !box->isVisible()) return {};
    QStyleOptionButton option;
    option.initFrom(box);
    option.text = box->text();
    const QRect contents = box->style()->subElementRect(QStyle::SE_CheckBoxContents, &option, box);
    const int advance = box->fontMetrics().horizontalAdvance(box->text());
    if (advance > contents.width()) {
        return QStringLiteral("'%1' advance %2 contents %3 box %4")
            .arg(box->text())
            .arg(advance)
            .arg(contents.width())
            .arg(box->width());
    }
    return {};
}

int comboEditFieldWidth(const QComboBox* combo) {
    QStyleOptionComboBox option;
    option.initFrom(combo);
    option.subControls = QStyle::SC_All;
    return combo->style()
        ->subControlRect(QStyle::CC_ComboBox, &option, QStyle::SC_ComboBoxEditField, combo)
        .width();
}

QString comboTextClip(const QComboBox* combo) {
    if (!combo) return QStringLiteral("missing combo");
    const int advance = combo->fontMetrics().horizontalAdvance(combo->currentText());
    const int editWidth = comboEditFieldWidth(combo);
    if (advance > editWidth) {
        return QStringLiteral("'%1' advance %2 edit %3 box %4")
            .arg(combo->currentText())
            .arg(advance)
            .arg(editWidth)
            .arg(combo->width());
    }
    if (!combo->toolTip().isEmpty()) {
        return QStringLiteral("unexpected tooltip '%1'").arg(combo->toolTip());
    }
    return {};
}

QString spinValueClip(const QDoubleSpinBox* spinBox, const QString& sample) {
    if (!spinBox) return QStringLiteral("missing spin");
    QStyleOptionSpinBox option;
    option.initFrom(spinBox);
    option.subControls = QStyle::SC_All;
    option.frame = spinBox->hasFrame();
    option.buttonSymbols = spinBox->buttonSymbols();
    const QRect editField = spinBox->style()->subControlRect(
        QStyle::CC_SpinBox, &option, QStyle::SC_SpinBoxEditField, spinBox);
    const int advance = spinBox->fontMetrics().horizontalAdvance(sample);
    if (advance > editField.width()) {
        return QStringLiteral("'%1' advance %2 edit %3 spin %4")
            .arg(sample)
            .arg(advance)
            .arg(editField.width())
            .arg(spinBox->width());
    }
    return {};
}

QString widgetPastPanel(const QWidget* panel, const QWidget* widget) {
    if (!panel || !widget || !widget->isVisible()) return {};
    const QRect placed(widget->mapTo(panel, QPoint(0, 0)), widget->size());
    if (placed.left() >= -1 && placed.right() <= panel->width() + 1 && placed.top() >= -1 &&
        placed.bottom() <= panel->height() + 1) {
        return {};
    }
    return QStringLiteral("%1 %2,%3 %4x%5 panel %6x%7")
        .arg(widget->objectName().isEmpty() ? QString::fromLatin1(widget->metaObject()->className())
                                            : widget->objectName())
        .arg(placed.x())
        .arg(placed.y())
        .arg(placed.width())
        .arg(placed.height())
        .arg(panel->width())
        .arg(panel->height());
}

QString narrowPanelClipReport(const PresentationPanel& panel) {
    const auto labelNamed = [&panel](const char* name) {
        return panel.findChild<QLabel*>(QLatin1String(name));
    };
    QString report;
    const auto note = [&report](const QString& piece) {
        if (!piece.isEmpty()) report += piece + QStringLiteral("; ");
    };

    const QLabel* labels[] = {
        labelNamed("PresentationPanelTitle"),
        labelNamed("PresentationEventFilterLabel"),
        labelNamed("PresentationTeamFilterLabel"),
        labelNamed("PresentationSelectionSummary"),
        labelNamed("PresentationCurrentClipTitle"),
        labelNamed("PresentationLeadLabel"),
        labelNamed("PresentationLagLabel"),
        labelNamed("PresentationKeyboardHint"),
        labelNamed("ApplyToAllButtonLabel"),
    };
    for (const QLabel* label : labels) note(labelClip(label));

    note(toolButtonClip(panel.findChild<QToolButton*>(QStringLiteral("PresentationSelectAllButton"))));
    note(toolButtonClip(panel.findChild<QToolButton*>(QStringLiteral("PresentationSelectNoneButton"))));
    note(nativeButtonClip(panel.findChild<QPushButton*>(QStringLiteral("PresentationExportButton"))));
    note(checkBoxClip(panel.findChild<QCheckBox*>(QStringLiteral("PresentationShowNotesCheck"))));
    note(comboTextClip(panel.findChild<QComboBox*>(QStringLiteral("PresentationEventFilter"))));
    note(comboTextClip(panel.findChild<QComboBox*>(QStringLiteral("PresentationTeamFilter"))));
    note(spinValueClip(panel.findChild<QDoubleSpinBox*>(QStringLiteral("LeadSpinBox")), QStringLiteral("60.0 s")));
    note(spinValueClip(panel.findChild<QDoubleSpinBox*>(QStringLiteral("LagSpinBox")), QStringLiteral("60.0 s")));

    const QWidget* placed[] = {
        labelNamed("PresentationPanelTitle"),
        labelNamed("PresentationEventFilterLabel"),
        labelNamed("PresentationTeamFilterLabel"),
        labelNamed("PresentationSelectionSummary"),
        labelNamed("PresentationCurrentClipTitle"),
        labelNamed("PresentationLeadLabel"),
        labelNamed("PresentationLagLabel"),
        labelNamed("PresentationKeyboardHint"),
        panel.findChild<QToolButton*>(QStringLiteral("PresentationSelectAllButton")),
        panel.findChild<QToolButton*>(QStringLiteral("PresentationSelectNoneButton")),
        panel.findChild<QPushButton*>(QStringLiteral("PresentationExportButton")),
        panel.findChild<QPushButton*>(QStringLiteral("ApplyToAllButton")),
        panel.findChild<QCheckBox*>(QStringLiteral("PresentationShowNotesCheck")),
        panel.findChild<QComboBox*>(QStringLiteral("PresentationEventFilter")),
        panel.findChild<QComboBox*>(QStringLiteral("PresentationTeamFilter")),
        panel.findChild<QDoubleSpinBox*>(QStringLiteral("LeadSpinBox")),
        panel.findChild<QDoubleSpinBox*>(QStringLiteral("LagSpinBox")),
    };
    for (const QWidget* widget : placed) note(widgetPastPanel(&panel, widget));
    return report;
}

}  // namespace

struct PanelThemeGuard {
    AppLocale::Language previousLanguage = AppLocale::currentLanguage();
    QString previousStyleSheet = qApp->styleSheet();
    ~PanelThemeGuard() {
        AppLocale::setLanguage(previousLanguage);
        qApp->setStyleSheet(previousStyleSheet);
    }
};

void UiPolicyTest::applyToSelectedLabelStaysWholeInANarrowPanel() {
    const QString theme = loadThemeStyleSheet();
    QVERIFY(!theme.isEmpty());
    PanelThemeGuard themeGuard;
    qApp->setStyleSheet(theme);
    AppLocale::setLanguage(AppLocale::Language::Spanish);

    PresentationPanel panel;
    panel.setFixedWidth(255);
    panel.resize(255, 720);
    panel.show();
    settlePanel(panel);

    auto* applyButton = panel.findChild<QPushButton*>(QStringLiteral("ApplyToAllButton"));
    auto* applyLabel = panel.findChild<QLabel*>(QStringLiteral("ApplyToAllButtonLabel"));
    auto* exportButton = panel.findChild<QPushButton*>(QStringLiteral("PresentationExportButton"));
    auto* notesCheck = panel.findChild<QCheckBox*>(QStringLiteral("PresentationShowNotesCheck"));
    QVERIFY(applyButton);
    QVERIFY(applyLabel);
    QVERIFY(exportButton);
    QVERIFY(notesCheck);

    const QString spanishLabel = QStringLiteral("Aplicar a todos los clips seleccionados");
    QCOMPARE(applyLabel->text(), spanishLabel);
    QCOMPARE(applyButton->text(), QString());
    QCOMPARE(applyButton->accessibleName(), spanishLabel);
    QVERIFY(applyLabel->wordWrap());
    const QString geometryReport = QStringLiteral("button %1x%2 label %3,%4 %5x%6 parent %7")
                                       .arg(applyButton->width())
                                       .arg(applyButton->height())
                                       .arg(applyLabel->geometry().x())
                                       .arg(applyLabel->geometry().y())
                                       .arg(applyLabel->width())
                                       .arg(applyLabel->height())
                                       .arg(applyLabel->parentWidget() == applyButton);
    QVERIFY2(applyButton->rect().contains(applyLabel->geometry()), qPrintable(geometryReport));
    QVERIFY2(wrappedLabelClip(applyLabel).isEmpty(), qPrintable(wrappedLabelClip(applyLabel) + geometryReport));
    QVERIFY(applyButton->width() <= 255);
    QVERIFY(applyLabel->fontMetrics().horizontalAdvance(spanishLabel) > applyLabel->contentsRect().width());
    QVERIFY(applyLabel->contentsRect().height() > applyLabel->fontMetrics().height());

    notesCheck->setVisible(true);
    auto* summary = panel.findChild<QLabel*>(QStringLiteral("PresentationSelectionSummary"));
    QVERIFY(summary);
    QVERIFY(summary->wordWrap());
    const QString longSummary =
        AppLocale::trUi("presentation.selection_summary").arg(400).arg(400).arg(400);
    summary->setText(longSummary);
    summary->updateGeometry();
    settlePanel(panel);
    QVERIFY2(narrowPanelClipReport(panel).isEmpty(), qPrintable(narrowPanelClipReport(panel)));
    QCOMPARE(summary->text(), longSummary);

    panel.setFixedWidth(720);
    panel.resize(720, 720);
    settlePanel(panel);
    QVERIFY(applyButton->rect().contains(applyLabel->geometry()));
    QVERIFY2(wrappedLabelClip(applyLabel).isEmpty(), qPrintable(wrappedLabelClip(applyLabel)));
    QVERIFY(applyLabel->fontMetrics().horizontalAdvance(spanishLabel) <= applyLabel->contentsRect().width());
    QVERIFY(applyLabel->contentsRect().height() <=
            applyLabel->fontMetrics().lineSpacing() + applyLabel->fontMetrics().height());

    const QString longerCopy = QStringLiteral(
        "Aplicar la anticipación y el retardo a todos los clips seleccionados ahora");
    applyLabel->setText(longerCopy);
    panel.setFixedWidth(255);
    panel.resize(255, 720);
    settlePanel(panel);
    applyButton->updateGeometry();
    settlePanel(panel);
    QVERIFY2(wrappedLabelClip(applyLabel).isEmpty(), qPrintable(wrappedLabelClip(applyLabel)));

    AppLocale::setLanguage(AppLocale::Language::English);
    panel.applyUiStrings();
    panel.setFixedWidth(255);
    settlePanel(panel);
    QCOMPARE(applyLabel->text(), QStringLiteral("Apply to all selected clips"));
    QVERIFY(applyButton->rect().contains(applyLabel->geometry()));
    QVERIFY2(wrappedLabelClip(applyLabel).isEmpty(), qPrintable(wrappedLabelClip(applyLabel)));
    notesCheck->setVisible(true);
    settlePanel(panel);
    QVERIFY2(narrowPanelClipReport(panel).isEmpty(), qPrintable(narrowPanelClipReport(panel)));
    QCOMPARE(panel.findChild<QComboBox*>(QStringLiteral("PresentationEventFilter"))->currentText(),
             QStringLiteral("All events"));
    QCOMPARE(panel.findChild<QComboBox*>(QStringLiteral("PresentationTeamFilter"))->currentText(),
             QStringLiteral("Both"));
}

void UiPolicyTest::hexNormalizesWhenEditingFinishes() {
    TeamColorPicker picker;
    picker.setColor(QStringLiteral("#112233"));
    auto* hexEdit = picker.findChild<QLineEdit*>(QStringLiteral("TeamColorHexEdit"));
    QVERIFY(hexEdit);

    hexEdit->setText(QStringLiteral("dc2626"));
    QCOMPARE(hexEdit->text(), QStringLiteral("dc2626"));
    QCOMPARE(picker.color(), QStringLiteral("#112233"));
    QCOMPARE(picker.displayedHex(), QStringLiteral("#DC2626"));

    hexEdit->setText(QStringLiteral("#aabbcc"));
    QCOMPARE(hexEdit->text(), QStringLiteral("#aabbcc"));
    QCOMPARE(picker.displayedHex(), QStringLiteral("#AABBCC"));

    emit hexEdit->editingFinished();
    QCOMPARE(hexEdit->text(), QStringLiteral("#AABBCC"));
    QCOMPARE(picker.color(), QStringLiteral("#AABBCC"));
    QCOMPARE(picker.displayedHex(), QStringLiteral("#AABBCC"));
}

void UiPolicyTest::invalidHexShowsAnInlineHint() {
    TeamColorPicker picker;
    picker.setColor(QStringLiteral("00ff00"));
    auto* hexEdit = picker.findChild<QLineEdit*>(QStringLiteral("TeamColorHexEdit"));
    auto* hint = picker.findChild<QLabel*>(QStringLiteral("TeamColorHexHint"));
    QVERIFY(hexEdit);
    QVERIFY(hint);

    hexEdit->setText(QStringLiteral("abc"));
    QVERIFY(hint->isHidden());
    QCOMPARE(picker.color(), QStringLiteral("#00FF00"));
    QCOMPARE(picker.displayedHex(), QStringLiteral("#00FF00"));

    hexEdit->setText(QStringLiteral("nope"));
    QVERIFY(!hint->isHidden());
    QVERIFY(hint->text().contains(QStringLiteral("#")));
    QCOMPARE(picker.color(), QStringLiteral("#00FF00"));
    QCOMPARE(picker.displayedHex(), QStringLiteral("#00FF00"));
    QCOMPARE(hexEdit->text(), QStringLiteral("nope"));

    emit hexEdit->editingFinished();
    QCOMPARE(hexEdit->text(), QStringLiteral("#00FF00"));
    QVERIFY(hint->isHidden());
    QCOMPARE(picker.color(), QStringLiteral("#00FF00"));
}

int runUiPolicyTests(int argc, char** argv) {
    UiPolicyTest tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "UiPolicyTests.moc"
