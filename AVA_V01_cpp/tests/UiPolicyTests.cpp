#include "AppLocale.h"
#include "ExportOutputPath.h"
#include "GameSetupWindow.h"
#include "TeamColorPicker.h"
#include "XmlImportTimeline.h"

#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QTemporaryDir>
#include <QTest>

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
