#include "AppLocale.h"
#include "ExportClipBuilder.h"
#include "GameControls.h"
#include "ReplaceVideoConfirmation.h"
#include "SessionNotesWriter.h"
#include "TagSession.h"
#include "XmlExporter.h"

#include <QApplication>
#include <QDate>
#include <QFile>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>

namespace {

class ModalWatchdog {
public:
    ModalWatchdog() {
        timer_.setSingleShot(true);
        QObject::connect(&timer_, &QTimer::timeout, &timer_, [] {
            if (QWidget* modal = QApplication::activeModalWidget()) {
                modal->close();
            }
        });
        timer_.start(2000);
    }

    ~ModalWatchdog() { timer_.stop(); }

private:
    QTimer timer_;
};

struct DialogSnapshot {
    QString title;
    QString text;
    QStringList buttonTexts;
    bool foundButton = false;
};

void scheduleDialogClick(const QString& buttonText, DialogSnapshot* snapshot) {
    QTimer::singleShot(0, [buttonText, snapshot] {
        auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (!box || !snapshot) {
            return;
        }
        snapshot->title = box->windowTitle();
        snapshot->text = box->text();
        const QList<QPushButton*> buttons = box->findChildren<QPushButton*>();
        for (QPushButton* button : buttons) {
            snapshot->buttonTexts.append(button->text());
            if (button->text() == buttonText) {
                snapshot->foundButton = true;
                button->click();
            }
        }
    });
}

TagSession::GameTag playTag(const QString& mainEvent, qint64 markMs) {
    TagSession::GameTag tag;
    tag.setMainEventKey(TagEvent::keyFromCanonicalName(mainEvent));
    tag.markMs = markMs;
    tag.startMs = markMs;
    tag.endMs = markMs + 1000;
    tag.team = QStringLiteral("Home");
    tag.intervalManuallyEdited = true;
    return tag;
}

PresentationQueue::Clip clipFrom(const TagSession::GameTag& tag) {
    PresentationQueue::Clip clip;
    clip.tagId = tag.id;
    clip.markMs = tag.markMs;
    clip.startMs = tag.startMs;
    clip.endMs = tag.endMs;
    clip.mainEvent = tag.mainEventKey();
    clip.team = tag.team;
    clip.note = tag.note;
    return clip;
}

void showAndFocus(QWidget* window, QWidget* field) {
    window->show();
    window->raise();
    window->activateWindow();
    QApplication::setActiveWindow(window);
    QApplication::processEvents();
    field->setFocus(Qt::OtherFocusReason);
    QApplication::processEvents();
}

}  // namespace

class InteractionGuardTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();
    void typingInTextFieldsDoesNotTag();
    void quarterAdvanceIgnoresAutoRepeat();
    void percentPlaceholdersInNamesStayLiteral();
    void replaceVideoDialogOnlyWhenTagsOrNotesExist();
    void replaceVideoDialogReturnsTheClickedChoice();
};

void InteractionGuardTest::initTestCase() {
    QCoreApplication::setOrganizationName(QStringLiteral("AsiaticaVA"));
    QCoreApplication::setApplicationName(QStringLiteral("AVATests"));
}

void InteractionGuardTest::cleanup() {
    AppLocale::setLanguage(AppLocale::Language::English);
}

void InteractionGuardTest::typingInTextFieldsDoesNotTag() {
    QWidget window;
    auto* layout = new QVBoxLayout(&window);
    auto* controls = new GameControls(&window);
    auto* lineEdit = new QLineEdit(&window);
    auto* textEdit = new QTextEdit(&window);
    auto* plainEdit = new QPlainTextEdit(&window);
    textEdit->setMinimumHeight(48);
    plainEdit->setMinimumHeight(48);
    layout->addWidget(controls);
    layout->addWidget(lineEdit);
    layout->addWidget(textEdit);
    layout->addWidget(plainEdit);
    window.resize(900, 800);
    controls->setTeamSide(TagSession::TeamSide::Home);

    showAndFocus(&window, lineEdit);
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QVERIFY(QTest::qWaitFor([&] { return lineEdit->hasFocus(); }));

    QSignalSpy captured(controls, &GameControls::mainEventTimestampCaptured);
    QTest::keyClick(lineEdit, Qt::Key_A);
    QCOMPARE(captured.count(), 0);
    QCOMPARE(lineEdit->text(), QStringLiteral("a"));

    textEdit->setFocus(Qt::OtherFocusReason);
    QVERIFY(QTest::qWaitFor([&] { return textEdit->hasFocus(); }));
    QTest::keyClick(textEdit, Qt::Key_A);
    QCOMPARE(captured.count(), 0);
    QVERIFY(textEdit->toPlainText().contains(QLatin1Char('a')));

    plainEdit->setFocus(Qt::OtherFocusReason);
    QVERIFY(QTest::qWaitFor([&] { return plainEdit->hasFocus(); }));
    QTest::keyClick(plainEdit, Qt::Key_A);
    QCOMPARE(captured.count(), 0);
    QCOMPARE(plainEdit->toPlainText(), QStringLiteral("a"));

    lineEdit->setFocus(Qt::OtherFocusReason);
    QVERIFY(QTest::qWaitFor([&] { return lineEdit->hasFocus(); }));
    const QString textBeforeRepeat = lineEdit->text();
    QSignalSpy gameStarted(controls, &GameControls::gameStartRequested);
    QKeyEvent repeatedQuarter(QEvent::KeyPress, Qt::Key_G, Qt::NoModifier, QStringLiteral("g"), true);
    QApplication::sendEvent(lineEdit, &repeatedQuarter);
    QCOMPARE(gameStarted.count(), 0);
    QCOMPARE(lineEdit->text(), textBeforeRepeat + QStringLiteral("g"));

    controls->setFocus(Qt::OtherFocusReason);
    QVERIFY(QTest::qWaitFor([&] { return controls->hasFocus(); }));
    QTest::keyClick(controls, Qt::Key_A);
    QCOMPARE(captured.count(), 1);
    QCOMPARE(captured.at(0).at(0).toString(), QStringLiteral("Circle Entry"));
}

void InteractionGuardTest::quarterAdvanceIgnoresAutoRepeat() {
    QWidget window;
    auto* layout = new QVBoxLayout(&window);
    auto* controls = new GameControls(&window);
    layout->addWidget(controls);
    window.resize(900, 500);
    window.show();
    window.activateWindow();
    QApplication::setActiveWindow(&window);
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    controls->setFocus(Qt::OtherFocusReason);
    QVERIFY(QTest::qWaitFor([&] { return controls->hasFocus(); }));

    QSignalSpy gameStarted(controls, &GameControls::gameStartRequested);
    QKeyEvent repeatedStart(QEvent::KeyPress, Qt::Key_G, Qt::NoModifier, QStringLiteral("g"), true);
    QApplication::sendEvent(controls, &repeatedStart);
    QCOMPARE(gameStarted.count(), 0);

    QKeyEvent startOnce(QEvent::KeyPress, Qt::Key_G, Qt::NoModifier, QStringLiteral("g"), false);
    QApplication::sendEvent(controls, &startOnce);
    QCOMPARE(gameStarted.count(), 1);

    QSignalSpy nextQuarter(controls, &GameControls::nextQuarterRequested);
    QKeyEvent repeatedAdvance(QEvent::KeyPress, Qt::Key_H, Qt::NoModifier, QStringLiteral("h"), true);
    QApplication::sendEvent(controls, &repeatedAdvance);
    QCOMPARE(nextQuarter.count(), 0);

    QKeyEvent advanceOnce(QEvent::KeyPress, Qt::Key_H, Qt::NoModifier, QStringLiteral("h"), false);
    QApplication::sendEvent(controls, &advanceOnce);
    QCOMPARE(nextQuarter.count(), 1);
}

void InteractionGuardTest::percentPlaceholdersInNamesStayLiteral() {
    AppLocale::setLanguage(AppLocale::Language::English);

    TagSession session;
    session.setGameTeams(QStringLiteral("%1 United"), QStringLiteral("City"),
                         QStringLiteral("#112233"), QStringLiteral("#445566"));
    session.setGameMetadata(QDate(2026, 5, 1), QStringLiteral("%1"), QStringLiteral("%2"));
    session.setMatchNote(QStringLiteral("<p>score %1</p>"));
    session.addTag(playTag(QStringLiteral("Goal"), 1800));
    session.addTag(playTag(QStringLiteral("%1"), 3500));
    TagSession::GameTag goal = session.tags().at(0);
    QVERIFY(session.setTagNote(session.indexOfTagId(goal.id), QStringLiteral("note %1")));

    goal = session.tags().at(session.indexOfTagId(goal.id));
    TagSession::GameTag literalEvent = session.tags().at(1);
    literalEvent.team = QStringLiteral("Away");

    PresentationQueue::Clip literalClip = clipFrom(literalEvent);

    ExportClipBuilder::OverlayOptions options;
    options.language = AppLocale::Language::English;
    options.includeBottomOverlay = true;
    options.includeScoreboardOverlay = false;
    options.includeNotesOverlay = false;
    const QVector<ClipSegment> segments =
        ExportClipBuilder::buildClipSegments(&session, {clipFrom(goal), literalClip}, options);
    QCOMPARE(segments.size(), 2);
    QCOMPARE(segments.at(0).overlayText, QStringLiteral("%1 United - Goal  1 / 2"));
    QCOMPARE(segments.at(1).overlayText, QStringLiteral("City - %1  2 / 2"));

    const QVector<XmlExporter::ExportedXmlInstance> catalog =
        XmlExporter::buildExportedInstances(&session);
    bool sawResultado = false;
    for (const XmlExporter::ExportedXmlInstance& instance : catalog) {
        for (const XmlExporter::ExportedXmlLabel& label : instance.labels) {
            QVERIFY(label.group != QStringLiteral("COMPETICION"));
            if (label.group == QStringLiteral("RESULTADO")) {
                sawResultado = true;
                QCOMPARE(label.text, QStringLiteral("%1 1 - 0 %2"));
            }
        }
    }
    QVERIFY(sawResultado);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString notesPath = directory.filePath(QStringLiteral("match.notes.txt"));
    QString writeError;
    QVERIFY2(SessionNotesWriter::writeFile(session, notesPath, &writeError), qPrintable(writeError));
    QFile notesFile(notesPath);
    QVERIFY(notesFile.open(QIODevice::ReadOnly));
    const QString notesText = QString::fromUtf8(notesFile.readAll());
    QVERIFY(notesText.contains(QStringLiteral("score %1")));
    QVERIFY(notesText.contains(QStringLiteral("note %1")));
    QVERIFY(notesText.contains(QStringLiteral("%1 United")));
}

void InteractionGuardTest::replaceVideoDialogOnlyWhenTagsOrNotesExist() {
    QVERIFY(!replaceVideoWouldDiscardWork(nullptr));
    {
        ModalWatchdog watchdog;
        QCOMPARE(chooseReplaceVideoAction(nullptr, nullptr), ReplaceVideoChoice::ContinueWithoutExport);
    }

    TagSession empty;
    QVERIFY(!replaceVideoWouldDiscardWork(&empty));
    {
        ModalWatchdog watchdog;
        QCOMPARE(chooseReplaceVideoAction(&empty, nullptr), ReplaceVideoChoice::ContinueWithoutExport);
    }

    empty.setMatchNote(QStringLiteral("   "));
    QVERIFY(!replaceVideoWouldDiscardWork(&empty));
    empty.setMatchNote(QStringLiteral("<p><br></p>"));
    QVERIFY(!replaceVideoWouldDiscardWork(&empty));
    {
        ModalWatchdog watchdog;
        QCOMPARE(chooseReplaceVideoAction(&empty, nullptr), ReplaceVideoChoice::ContinueWithoutExport);
    }

    empty.setMatchNote(QStringLiteral("<p>nota del partido</p>"));
    QVERIFY(replaceVideoWouldDiscardWork(&empty));

    TagSession tagged;
    tagged.addTag(playTag(QStringLiteral("Goal"), 1000));
    QVERIFY(replaceVideoWouldDiscardWork(&tagged));
}

void InteractionGuardTest::replaceVideoDialogReturnsTheClickedChoice() {
    AppLocale::setLanguage(AppLocale::Language::Spanish);

    TagSession tagged;
    tagged.addTag(playTag(QStringLiteral("Shot"), 1000));

    DialogSnapshot cancelSnapshot;
    scheduleDialogClick(AppLocale::trUi("replace_video.cancel"), &cancelSnapshot);
    {
        ModalWatchdog watchdog;
        QCOMPARE(chooseReplaceVideoAction(&tagged, nullptr), ReplaceVideoChoice::Cancel);
    }
    QVERIFY(cancelSnapshot.foundButton);

    #ifndef Q_OS_MACOS
    // macOS message boxes have no window title (Apple HIG), so Qt drops it there.
    QCOMPARE(cancelSnapshot.title, QStringLiteral("Sustituir video"));
    #endif

    QCOMPARE(cancelSnapshot.text, QStringLiteral("Sustituir el video borra todas las marcas y las notas."));
    QVERIFY(cancelSnapshot.buttonTexts.contains(QStringLiteral("Exportar XML y notas")));
    QVERIFY(cancelSnapshot.buttonTexts.contains(QStringLiteral("Sustituir sin exportar")));
    QVERIFY(cancelSnapshot.buttonTexts.contains(QStringLiteral("Cancelar")));

    TagSession noted;
    noted.setMatchNote(QStringLiteral("<p>solo una nota</p>"));
    DialogSnapshot replaceSnapshot;
    scheduleDialogClick(AppLocale::trUi("replace_video.replace"), &replaceSnapshot);
    {
        ModalWatchdog watchdog;
        QCOMPARE(chooseReplaceVideoAction(&noted, nullptr), ReplaceVideoChoice::ContinueWithoutExport);
    }
    QVERIFY(replaceSnapshot.foundButton);

    DialogSnapshot exportSnapshot;
    scheduleDialogClick(AppLocale::trUi("replace_video.export"), &exportSnapshot);
    {
        ModalWatchdog watchdog;
        QCOMPARE(chooseReplaceVideoAction(&tagged, nullptr), ReplaceVideoChoice::ExportThenContinue);
    }
    QVERIFY(exportSnapshot.foundButton);
}

int runInteractionGuardTests(int argc, char** argv) {
    InteractionGuardTest tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "InteractionGuardTests.moc"
