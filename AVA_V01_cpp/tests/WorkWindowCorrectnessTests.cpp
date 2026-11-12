#include "AppLocale.h"
#include "ClipTrimBar.h"
#include "GameControls.h"
#include "MatchNotesEditor.h"
#include "MatchSessionFile.h"
#include "PresentationQueue.h"
#include "TagSession.h"
#include "ThemeColors.h"
#include "TagsTableModel.h"
#include "TimelineBar.h"
#include "WorkWindow.h"

#include <QApplication>
#include <QAbstractItemModel>
#include <QAction>
#include <QBrush>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QItemSelectionModel>
#include <QLabel>
#include <QMediaPlayer>
#include <QMenu>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSlider>
#include <QStringList>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include <cmath>
#include <memory>

namespace {

TagSession::GameTag playTag(const QString& mainEvent, qint64 markMs) {
  TagSession::GameTag tag;
  tag.setMainEventKey(TagEvent::keyFromCanonicalName(mainEvent));
  tag.markMs = markMs;
  tag.team = TagSession::teamSideKey(TagSession::TeamSide::Home);
  return tag;
}

QString writePlaceholderVideo(QTemporaryDir& directory) {
  const QString path = directory.filePath(QStringLiteral("game.mp4"));
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly)) return {};
  if (file.write("not a real video") < 0) return {};
  file.close();
  return QFileInfo(path).absoluteFilePath();
}

QTableView* tagsTable(WorkWindow& window) {
  return window.findChild<QTableView*>(QStringLiteral("TagsTable"));
}

QPlainTextEdit* clipNotes(WorkWindow& window) {
  const auto editors = window.findChildren<QPlainTextEdit*>();
  return editors.isEmpty() ? nullptr : editors.first();
}

void showTaggingWindow(WorkWindow& window) {
  window.resize(1280, 800);
  window.show();
  window.activateWindow();
  QApplication::setActiveWindow(&window);
  QApplication::processEvents();
}

QWidget* focusForTyping(QWidget* editor) {
  editor->setFocus(Qt::OtherFocusReason);
  QApplication::processEvents();
  QWidget* focus = QApplication::focusWidget();
  return focus != nullptr ? focus : editor;
}

QPushButton* eventButton(QWidget* root, const QString& eventName) {
  const QList<QPushButton*> buttons = root->findChildren<QPushButton*>();
  for (QPushButton* button : buttons) {
    if (button->property("gameEventName").toString() == eventName) return button;
  }
  return nullptr;
}

int rowWithTime(QAbstractItemModel* model, const QString& timeText) {
  if (!model) return -1;
  for (int row = 0; row < model->rowCount(); ++row) {
    if (model->data(model->index(row, 0)).toString() == timeText) return row;
  }
  return -1;
}

int xForTrimMs(const QWidget& bar, qint64 positionMs, qint64 windowEndMs) {
  constexpr int margin = 20;
  const int trackWidth = bar.width() - (margin * 2);
  if (trackWidth <= 0 || windowEndMs <= 0) return margin;
  const double fraction = static_cast<double>(positionMs) / static_cast<double>(windowEndMs);
  return margin + static_cast<int>(std::lround(fraction * trackWidth));
}

void dragTrimHandle(ClipTrimBar& bar, const QPoint& from, const QPoint& to) {
  const QPointF fromF(from);
  const QPointF toF(to);
  QMouseEvent press(QEvent::MouseButtonPress, fromF, fromF, Qt::LeftButton, Qt::LeftButton,
                    Qt::NoModifier);
  QApplication::sendEvent(&bar, &press);
  QMouseEvent move(QEvent::MouseMove, toF, toF, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(&bar, &move);
  QMouseEvent release(QEvent::MouseButtonRelease, toF, toF, Qt::LeftButton, Qt::NoButton,
                      Qt::NoModifier);
  QApplication::sendEvent(&bar, &release);
}

QString writeNamedPlaceholder(QTemporaryDir& directory, const QString& fileName) {
  const QString path = directory.filePath(fileName);
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly)) return {};
  if (file.write("not a real video") < 0) return {};
  file.close();
  return QFileInfo(path).absoluteFilePath();
}

QString plantSessionBytes(const QString& videoPath, const QByteArray& bytes) {
  const QString sessionPath = MatchSessionFile::sessionFilePathForSources({videoPath});
  if (sessionPath.isEmpty()) return {};
  if (!QDir().mkpath(QFileInfo(sessionPath).absolutePath())) return {};
  QFile file(sessionPath);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
  if (file.write(bytes) != bytes.size()) return {};
  file.close();
  return sessionPath;
}

class LanguageGuard {
 public:
  LanguageGuard() : previous_(AppLocale::currentLanguage()) {
    AppLocale::setLanguage(AppLocale::Language::English);
  }
  ~LanguageGuard() { AppLocale::setLanguage(previous_); }

  LanguageGuard(const LanguageGuard&) = delete;
  LanguageGuard& operator=(const LanguageGuard&) = delete;

 private:
  AppLocale::Language previous_;
};

class WarningLog {
 public:
  WarningLog() {
    active_ = this;
    previous_ = qInstallMessageHandler(&WarningLog::handle);
  }
  ~WarningLog() {
    active_ = nullptr;
    qInstallMessageHandler(previous_);
  }

  WarningLog(const WarningLog&) = delete;
  WarningLog& operator=(const WarningLog&) = delete;

  bool contains(const QString& fragment) const {
    for (const QString& line : lines_) {
      if (line.contains(fragment)) return true;
    }
    return false;
  }

 private:
  static void handle(QtMsgType type, const QMessageLogContext& context, const QString& message) {
    if (active_ != nullptr && type == QtWarningMsg) active_->lines_.append(message);
    if (active_ != nullptr && active_->previous_ != nullptr) {
      active_->previous_(type, context, message);
    }
  }

  static WarningLog* active_;
  QtMessageHandler previous_ = nullptr;
  QStringList lines_;
};

WarningLog* WarningLog::active_ = nullptr;

const QString kUnreadableEnglish =
    QStringLiteral("Couldn't read the session file. Changes to this match won't be saved.");
const QString kUnreadableSpanish =
    QStringLiteral("No se pudo leer el archivo de la sesión. Los cambios de este partido no se guardarán.");
const QString kUnsavedLog = QStringLiteral("Changes to this match will not be saved.");

void expectUnreadableWarning(WorkWindow& window, const WarningLog& log) {
  auto* warning = window.findChild<QLabel*>(QStringLiteral("SessionFileWarning"));
  QVERIFY(warning != nullptr);
  QVERIFY(!warning->isHidden());
  QCOMPARE(warning->text(), kUnreadableEnglish);
  QVERIFY(log.contains(kUnsavedLog));
}

}  // namespace

class WorkWindowCorrectnessTest : public QObject {
  Q_OBJECT

 private slots:
  void closingVideoReleasesTheFileBeforeTheTempDirectoryGoes();
  void replacingTheTempDirectoryReleasesTheOpenFileFirst();
  void tagRebuildDoesNotReenterOrSaveTheOpenDraft();
  void tagRebuildCoalescesANestedReset();
  void filterMenuKeepsTheSameMenuWhenEventsChange();
  void filterMenuDefersNewActionsUntilItCloses();
  void unreadableSessionFileWarnsAndIsLeftOnDisk();
  void tagDuringScrubUsesTheTimelineTime();
  void quarterAdvanceDuringScrubUsesTheTimelineTime();
  void playheadFollowsTheScrub();
  void backspaceInClipNotesDeletesACharacter();
  void letterMInClipNotesDoesNotChangeMode();
  void commaInClipNotesDoesNotOpenStats();
  void undoInClipNotesDoesNotRemoveTheTag();
  void backspaceInMatchNotesDeletesACharacter();
  void backspaceOnTheTagsTableStillRemovesTheTag();
  void movedMarkAppearsInTheTagsTable();
  void presentationTrimKeepsTheEventMark();
  void trimHandlesStopAtTheEventMark();
  void trimBarStaysOffTheTaggingPage();
  void addedTagsStayInMarkOrder();
};

void WorkWindowCorrectnessTest::closingVideoReleasesTheFileBeforeTheTempDirectoryGoes() {
  auto directory = std::make_unique<QTemporaryDir>();
  QVERIFY(directory->isValid());
  const QString videoPath = writePlaceholderVideo(*directory);
  QVERIFY(!videoPath.isEmpty());

  WorkWindow window;
  window.setConcatenatedVideoTemporaryDirectory(std::move(directory));
  window.loadVideoFromFile(videoPath);

  auto* player = window.findChild<QMediaPlayer*>();
  QVERIFY(player);
  QCOMPARE(QFileInfo(player->source().toLocalFile()).absoluteFilePath(), videoPath);

  bool sourceClearedWhileFileExisted = false;
  connect(player, &QMediaPlayer::sourceChanged, &window,
          [&](const QUrl& source) {
            if (!source.isEmpty()) return;
            sourceClearedWhileFileExisted = QFileInfo::exists(videoPath);
          },
          Qt::DirectConnection);

  QVERIFY(QMetaObject::invokeMethod(&window, "onCloseVideo"));
  QVERIFY(sourceClearedWhileFileExisted);
  QTRY_VERIFY(!QFileInfo::exists(videoPath));
  QVERIFY(player->source().isEmpty());
}

void WorkWindowCorrectnessTest::replacingTheTempDirectoryReleasesTheOpenFileFirst() {
  auto directory = std::make_unique<QTemporaryDir>();
  QVERIFY(directory->isValid());
  const QString videoPath = writePlaceholderVideo(*directory);
  QVERIFY(!videoPath.isEmpty());

  WorkWindow window;
  window.setConcatenatedVideoTemporaryDirectory(std::move(directory));
  window.loadVideoFromFile(videoPath);

  auto* player = window.findChild<QMediaPlayer*>();
  QVERIFY(player);

  bool sourceClearedWhileFileExisted = false;
  connect(player, &QMediaPlayer::sourceChanged, &window,
          [&](const QUrl& source) {
            if (!source.isEmpty()) return;
            sourceClearedWhileFileExisted = QFileInfo::exists(videoPath);
          },
          Qt::DirectConnection);

  auto replacement = std::make_unique<QTemporaryDir>();
  QVERIFY(replacement->isValid());
  window.setConcatenatedVideoTemporaryDirectory(std::move(replacement));

  QVERIFY(sourceClearedWhileFileExisted);
  QTRY_VERIFY(!QFileInfo::exists(videoPath));
  QVERIFY(player->source().isEmpty());
}

void WorkWindowCorrectnessTest::tagRebuildDoesNotReenterOrSaveTheOpenDraft() {
  TagSession session;
  WorkWindow window;
  window.setTagSession(&session);
  window.show();
  QApplication::sendPostedEvents();

  session.addTag(playTag(QStringLiteral("Goal"), 1000));
  QCOMPARE(session.tags().size(), 1);
  const int sessionIndex = session.indexOfTagId(session.tags().constFirst().id);
  QVERIFY(session.isValidTagIndex(sessionIndex));

  QTableView* table = tagsTable(window);
  QPlainTextEdit* notes = clipNotes(window);
  QVERIFY(table);
  QVERIFY(table->selectionModel());
  QVERIFY(notes);
  table->selectRow(0);
  QVERIFY(table->selectionModel()->currentIndex().isValid());

  notes->setPlainText(QStringLiteral("still drafting"));
  QCOMPARE(session.tagNote(sessionIndex), QString());

  int selectionChangesDuringReset = 0;
  bool insideReset = false;
  connect(table->model(), &QAbstractItemModel::modelAboutToBeReset, &window,
          [&]() { insideReset = true; });
  connect(table->model(), &QAbstractItemModel::modelReset, &window,
          [&]() { insideReset = false; });
  connect(table->selectionModel(), &QItemSelectionModel::currentChanged, &window,
          [&](const QModelIndex&, const QModelIndex&) {
            if (insideReset) ++selectionChangesDuringReset;
          });

  session.addTag(playTag(QStringLiteral("Circle"), 5000));

  QCOMPARE(selectionChangesDuringReset, 0);
  QCOMPARE(notes->toPlainText(), QStringLiteral("still drafting"));
  QCOMPARE(session.tagNote(sessionIndex), QString());
  QCOMPARE(table->model()->rowCount(), 2);
}

void WorkWindowCorrectnessTest::tagRebuildCoalescesANestedReset() {
  TagSession session;
  WorkWindow window;
  window.setTagSession(&session);
  session.addTag(playTag(QStringLiteral("Goal"), 1000));
  session.addTag(playTag(QStringLiteral("Pass"), 5000));

  QTableView* table = tagsTable(window);
  QVERIFY(table);
  QAbstractItemModel* model = table->model();
  QVERIFY(model);
  QCOMPARE(model->rowCount(), 2);

  int depth = 0;
  int maxDepth = 0;
  int injectedAdds = 0;
  connect(model, &QAbstractItemModel::modelAboutToBeReset, &window, [&]() {
    ++depth;
    if (depth > maxDepth) maxDepth = depth;
    if (depth == 1 && injectedAdds == 0) {
      ++injectedAdds;
      session.addTag(playTag(QStringLiteral("Shot"), 8000));
    }
  });
  connect(model, &QAbstractItemModel::modelReset, &window, [&]() { --depth; });

  QVERIFY(session.removeTag(0));

  QCOMPARE(maxDepth, 1);
  QCOMPARE(depth, 0);
  QCOMPARE(injectedAdds, 1);
  QCOMPARE(model->rowCount(), 2);
}

void WorkWindowCorrectnessTest::trimBarStaysOffTheTaggingPage() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString videoPath = writePlaceholderVideo(directory);
  QVERIFY(!videoPath.isEmpty());

  TagSession session;
  WorkWindow window;
  window.setTagSession(&session);
  showTaggingWindow(window);
  window.showTeamSetupForVideo(videoPath, {videoPath});
  auto* continueButton = window.findChild<QPushButton*>(QStringLiteral("GameSetupContinueButton"));
  QVERIFY(continueButton);
  continueButton->click();
  QApplication::processEvents();
  continueButton->click();
  QApplication::processEvents();

  session.setGameTeams(QStringLiteral("Home Club"), QStringLiteral("Away Club"),
                       QStringLiteral("#111111"), QStringLiteral("#222222"));
  session.addTag(playTag(QStringLiteral("50-yd"), 10000));
  window.setMode(WorkWindow::Mode::Presenting);
  QApplication::processEvents();

  QAbstractButton* selectAll = nullptr;
  const QList<QAbstractButton*> buttons = window.findChildren<QAbstractButton*>();
  for (QAbstractButton* button : buttons) {
    if (button->isVisible() && button->text() == QStringLiteral("Select all")) {
      selectAll = button;
      break;
    }
  }
  QVERIFY(selectAll);
  selectAll->click();
  QApplication::processEvents();

  ClipTrimBar* trimBar = window.findChild<ClipTrimBar*>();
  QVERIFY(trimBar);
  QVERIFY(trimBar->isVisible());

  window.setMode(WorkWindow::Mode::Tagging);
  QApplication::processEvents();
  QVERIFY2(!trimBar->isVisible(), "trim bar stayed visible after leaving Presentation");

  session.setGameTeams(QStringLiteral("Aleksandra Wiśniewska-Kowalczyk"), QStringLiteral("Jo"),
                       QStringLiteral("#111111"), QStringLiteral("#222222"));
  QApplication::processEvents();
  QVERIFY2(!trimBar->isVisible(), "a team rename brought the trim bar back onto the tagging page");

  session.addTag(playTag(QStringLiteral("50-yd"), 4000));
  QApplication::processEvents();
  QVERIFY2(!trimBar->isVisible(), "adding a tag brought the trim bar back onto the tagging page");
}

void WorkWindowCorrectnessTest::addedTagsStayInMarkOrder() {
  TagSession session;
  WorkWindow window;
  window.setTagSession(&session);
  session.addTag(playTag(QStringLiteral("Goal"), 30000));
  session.addTag(playTag(QStringLiteral("Pass"), 10000));
  session.addTag(playTag(QStringLiteral("Shot"), 10000));

  auto* model = qobject_cast<TagsTableModel*>(tagsTable(window)->model());
  QVERIFY(model);
  QCOMPARE(model->rowCount(), 3);
  QCOMPARE(model->tagSessionIndexAt(0), 1);
  QCOMPARE(model->tagSessionIndexAt(1), 2);
  QCOMPARE(model->tagSessionIndexAt(2), 0);
  QCOMPARE(model->markMsAt(0), static_cast<qint64>(10000));
  QCOMPARE(model->markMsAt(1), static_cast<qint64>(10000));
  QCOMPARE(model->markMsAt(2), static_cast<qint64>(30000));
}

void WorkWindowCorrectnessTest::filterMenuKeepsTheSameMenuWhenEventsChange() {
  TagSession session;
  WorkWindow window;
  window.setTagSession(&session);

  auto* menu = window.findChild<QMenu*>(QStringLiteral("TagsFilterMenu"));
  QVERIFY(menu);
  const QList<QAction*> originalActions = menu->actions();
  QVERIFY(originalActions.size() >= 2);

  session.addTag(playTag(QStringLiteral("Goal"), 1000));

  QCOMPARE(window.findChild<QMenu*>(QStringLiteral("TagsFilterMenu")), menu);
  QVERIFY(menu->actions().contains(originalActions.at(0)));
  QVERIFY(menu->actions().contains(originalActions.at(1)));

  bool sawGoal = false;
  for (QAction* action : menu->actions()) {
    const TagEvent::MainEventKey key = action->data().value<TagEvent::MainEventKey>();
    if (key.id == EventCatalog::EventId::Goal) sawGoal = true;
  }
  QVERIFY(sawGoal);
}

void WorkWindowCorrectnessTest::filterMenuDefersNewActionsUntilItCloses() {
  TagSession session;
  WorkWindow window;
  window.setTagSession(&session);
  session.addTag(playTag(QStringLiteral("Goal"), 1000));

  auto* menu = window.findChild<QMenu*>(QStringLiteral("TagsFilterMenu"));
  QVERIFY(menu);
  const int actionsBeforeOpen = menu->actions().size();

  window.resize(900, 600);
  window.show();
  QApplication::sendPostedEvents();
  menu->popup(window.mapToGlobal(QPoint(30, 30)));
  QApplication::sendPostedEvents();

  session.addTag(playTag(QStringLiteral("Circle"), 4000));

  if (menu->isVisible()) {
    QCOMPARE(menu->actions().size(), actionsBeforeOpen);
    menu->close();
    QTest::qWait(30);
    QVERIFY(!menu->isVisible());
  }

  bool sawCircle = false;
  for (QAction* action : menu->actions()) {
    const TagEvent::MainEventKey key = action->data().value<TagEvent::MainEventKey>();
    if (key.id == EventCatalog::EventId::None && key.unlistedName == QStringLiteral("Circle")) {
      sawCircle = true;
    }
  }
  QVERIFY(sawCircle);
}

void WorkWindowCorrectnessTest::unreadableSessionFileWarnsAndIsLeftOnDisk() {
  LanguageGuard language;
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  TagSession session;

  const QByteArray newerBytes = QByteArrayLiteral(
      "{\"format\":\"ava-match-session\",\"version\":3,\"tags\":[],\"future\":true}\n");
  const QString newerVideo = writeNamedPlaceholder(directory, QStringLiteral("newer.mp4"));
  QVERIFY(!newerVideo.isEmpty());
  const QString newerSession = plantSessionBytes(newerVideo, newerBytes);
  QVERIFY(!newerSession.isEmpty());

  {
    WarningLog log;
    WorkWindow window;
    window.setTagSession(&session);
    window.loadVideoFromFile(newerVideo);
    expectUnreadableWarning(window, log);

    AppLocale::setLanguage(AppLocale::Language::Spanish);
    auto* warning = window.findChild<QLabel*>(QStringLiteral("SessionFileWarning"));
    QVERIFY(warning != nullptr);
    QCOMPARE(warning->text(), kUnreadableSpanish);
    QCOMPARE(AppLocale::trUi("session.unreadable"), kUnreadableSpanish);

    QVERIFY(QMetaObject::invokeMethod(&window, "onCloseVideo"));
    QVERIFY(warning->isHidden());
  }
  QCOMPARE(QFile(newerSession).exists(), true);
  QFile newerFile(newerSession);
  QVERIFY(newerFile.open(QIODevice::ReadOnly));
  QCOMPARE(newerFile.readAll(), newerBytes);
  newerFile.close();
  QVERIFY(QFile::remove(newerSession));

  AppLocale::setLanguage(AppLocale::Language::English);
  const QByteArray corruptBytes = QByteArrayLiteral("{");
  const QString corruptVideo = writeNamedPlaceholder(directory, QStringLiteral("corrupt.mp4"));
  QVERIFY(!corruptVideo.isEmpty());
  const QString corruptSession = plantSessionBytes(corruptVideo, corruptBytes);
  QVERIFY(!corruptSession.isEmpty());

  {
    WarningLog log;
    WorkWindow window;
    window.setTagSession(&session);
    window.loadVideoFromFile(corruptVideo);
    expectUnreadableWarning(window, log);
    QVERIFY(QMetaObject::invokeMethod(&window, "onCloseVideo"));
    auto* warning = window.findChild<QLabel*>(QStringLiteral("SessionFileWarning"));
    QVERIFY(warning != nullptr);
    QVERIFY(warning->isHidden());
  }
  QFile corruptFile(corruptSession);
  QVERIFY(corruptFile.open(QIODevice::ReadOnly));
  QCOMPARE(corruptFile.readAll(), corruptBytes);
  corruptFile.close();
  QVERIFY(QFile::remove(corruptSession));

  const QString freshVideo = writeNamedPlaceholder(directory, QStringLiteral("fresh.mp4"));
  QVERIFY(!freshVideo.isEmpty());
  const QString freshSession = MatchSessionFile::sessionFilePathForSources({freshVideo});
  QVERIFY(!freshSession.isEmpty());
  QVERIFY(!QFile::exists(freshSession));
  {
    WarningLog log;
    WorkWindow window;
    window.setTagSession(&session);
    window.loadVideoFromFile(freshVideo);
    auto* warning = window.findChild<QLabel*>(QStringLiteral("SessionFileWarning"));
    QVERIFY(warning != nullptr);
    QVERIFY(warning->isHidden());
    QVERIFY(!log.contains(kUnsavedLog));
    QVERIFY(QMetaObject::invokeMethod(&window, "onCloseVideo"));
  }
  QFile::remove(freshSession);
}

void WorkWindowCorrectnessTest::tagDuringScrubUsesTheTimelineTime() {
  TagSession session;
  session.setGameTeams(QStringLiteral("Las Leonas"), QStringLiteral("Red Lions"),
                       QStringLiteral("#7f1d1d"), QStringLiteral("#1e3a8a"));
  WorkWindow window;
  window.setTagSession(&session);
  showTaggingWindow(window);

  auto* controls = window.findChild<GameControls*>();
  QVERIFY(controls);
  controls->setTeamSide(TagSession::TeamSide::Home);

  TimelineBar* bar = window.findChild<TimelineBar*>();
  QVERIFY(bar);
  bar->setDurationMs(100000);
  bar->setPositionMs(0);
  auto* slider = bar->findChild<QSlider*>(QStringLiteral("TimelineSlider"));
  auto* label = bar->findChild<QLabel*>();
  QVERIFY(slider);
  QVERIFY(label);
  slider->setSliderDown(true);
  slider->setSliderPosition(80000);
  QCOMPARE(label->text(), QStringLiteral("01:20 / 01:40"));

  QPushButton* fiftyYard = eventButton(&window, QStringLiteral("50-yd"));
  QVERIFY(fiftyYard);
  fiftyYard->click();

  QCOMPARE(session.tags().size(), 1);
  QCOMPARE(session.tags().at(0).markMs, static_cast<qint64>(80000));
}

void WorkWindowCorrectnessTest::quarterAdvanceDuringScrubUsesTheTimelineTime() {
  TagSession session;
  WorkWindow window;
  window.setTagSession(&session);

  TimelineBar* bar = window.findChild<TimelineBar*>();
  QVERIFY(bar);
  bar->setDurationMs(100000);
  bar->setPositionMs(0);
  QVERIFY(QMetaObject::invokeMethod(&window, "onGameStartRequested"));
  QCOMPARE(session.tags().size(), 1);
  QCOMPARE(session.tags().at(0).markMs, static_cast<qint64>(0));

  auto* slider = bar->findChild<QSlider*>(QStringLiteral("TimelineSlider"));
  QVERIFY(slider);
  slider->setSliderDown(true);
  slider->setSliderPosition(80000);

  QVERIFY(QMetaObject::invokeMethod(&window, "onNextQuarterRequested"));
  QCOMPARE(session.tags().size(), 2);
  QCOMPARE(session.tags().at(1).endMs, static_cast<qint64>(80000));
}

void WorkWindowCorrectnessTest::playheadFollowsTheScrub() {
  TagSession session;
  WorkWindow window;
  window.setTagSession(&session);
  session.addTag(playTag(QStringLiteral("Goal"), 80000));
  session.addTag(playTag(QStringLiteral("Pass"), 10000));

  QAbstractItemModel* model = tagsTable(window)->model();
  QVERIFY(model);
  const int farRow = rowWithTime(model, QStringLiteral("01:20"));
  QVERIFY(farRow >= 0);
  QVERIFY(!model->data(model->index(farRow, 0), Qt::BackgroundRole).isValid());

  TimelineBar* bar = window.findChild<TimelineBar*>();
  QVERIFY(bar);
  bar->setDurationMs(100000);
  auto* slider = bar->findChild<QSlider*>(QStringLiteral("TimelineSlider"));
  QVERIFY(slider);
  slider->setSliderDown(true);
  slider->setSliderPosition(80000);

  const QVariant background = model->data(model->index(farRow, 0), Qt::BackgroundRole);
  QCOMPARE(background.value<QBrush>().color(), Style::ThemeColors::playheadHighlight());
}

void WorkWindowCorrectnessTest::backspaceInClipNotesDeletesACharacter() {
  TagSession session;
  WorkWindow window;
  window.setTagSession(&session);
  showTaggingWindow(window);
  session.addTag(playTag(QStringLiteral("Goal"), 1000));

  QTableView* table = tagsTable(window);
  QPlainTextEdit* notes = clipNotes(window);
  QVERIFY(table);
  QVERIFY(notes);
  table->selectRow(0);
  notes->setPlainText(QStringLiteral("hello"));
  notes->moveCursor(QTextCursor::End);
  QTest::keyClick(focusForTyping(notes), Qt::Key_Backspace);

  QCOMPARE(session.tags().size(), 1);
  QCOMPARE(notes->toPlainText(), QStringLiteral("hell"));
}

void WorkWindowCorrectnessTest::letterMInClipNotesDoesNotChangeMode() {
  TagSession session;
  WorkWindow window;
  window.setTagSession(&session);
  showTaggingWindow(window);
  session.addTag(playTag(QStringLiteral("Goal"), 1000));

  QTableView* table = tagsTable(window);
  QPlainTextEdit* notes = clipNotes(window);
  QVERIFY(table);
  QVERIFY(notes);
  table->selectRow(0);
  notes->setPlainText(QStringLiteral("hello"));
  notes->moveCursor(QTextCursor::End);
  QTest::keyClick(focusForTyping(notes), Qt::Key_M);

  QCOMPARE(window.mode(), WorkWindow::Mode::Tagging);
  QCOMPARE(notes->toPlainText(), QStringLiteral("hellom"));
  QCOMPARE(session.tags().size(), 1);
}

void WorkWindowCorrectnessTest::commaInClipNotesDoesNotOpenStats() {
  TagSession session;
  WorkWindow window;
  window.setTagSession(&session);
  showTaggingWindow(window);
  session.addTag(playTag(QStringLiteral("Goal"), 1000));

  QTableView* table = tagsTable(window);
  QPlainTextEdit* notes = clipNotes(window);
  QVERIFY(table);
  QVERIFY(notes);
  table->selectRow(0);
  notes->setPlainText(QStringLiteral("hello"));
  notes->moveCursor(QTextCursor::End);
  QTest::keyClick(focusForTyping(notes), Qt::Key_Comma);

  QCOMPARE(notes->toPlainText(), QStringLiteral("hello,"));
  QCOMPARE(session.tags().size(), 1);
  const QList<QDialog*> dialogs = window.findChildren<QDialog*>();
  for (QDialog* dialog : dialogs) {
    QVERIFY(!dialog->isVisible());
  }
}

void WorkWindowCorrectnessTest::undoInClipNotesDoesNotRemoveTheTag() {
  TagSession session;
  WorkWindow window;
  window.setTagSession(&session);
  showTaggingWindow(window);
  session.addTag(playTag(QStringLiteral("Goal"), 1000));

  QTableView* table = tagsTable(window);
  QPlainTextEdit* notes = clipNotes(window);
  QVERIFY(table);
  QVERIFY(notes);
  table->selectRow(0);
  notes->clear();
  QWidget* focus = focusForTyping(notes);
  QTest::keyClicks(focus, "a");
  QCOMPARE(notes->toPlainText(), QStringLiteral("a"));
  QTest::keyClick(focus, Qt::Key_Z, Qt::ControlModifier);

  QCOMPARE(session.tags().size(), 1);
  QCOMPARE(notes->toPlainText(), QString());
}

void WorkWindowCorrectnessTest::backspaceInMatchNotesDeletesACharacter() {
  TagSession session;
  WorkWindow window;
  window.setTagSession(&session);
  showTaggingWindow(window);
  session.addTag(playTag(QStringLiteral("Goal"), 1000));

  QTableView* table = tagsTable(window);
  auto* matchNotes = window.findChild<MatchNotesEditor*>();
  QVERIFY(table);
  QVERIFY(matchNotes);
  table->selectRow(0);
  matchNotes->clear();
  QWidget* focus = focusForTyping(matchNotes);
  QTest::keyClicks(focus, "hi");
  QTest::keyClick(focus, Qt::Key_Backspace);

  QCOMPARE(session.tags().size(), 1);
  QCOMPARE(matchNotes->toPlainText(), QStringLiteral("h"));
}

void WorkWindowCorrectnessTest::backspaceOnTheTagsTableStillRemovesTheTag() {
  TagSession session;
  WorkWindow window;
  window.setTagSession(&session);
  showTaggingWindow(window);
  session.addTag(playTag(QStringLiteral("Goal"), 1000));

  QTableView* table = tagsTable(window);
  QVERIFY(table);
  table->selectRow(0);
  table->setFocus(Qt::OtherFocusReason);
  QApplication::processEvents();
  QWidget* focus = QApplication::focusWidget();
  QTest::keyClick(focus != nullptr ? focus : static_cast<QWidget*>(table), Qt::Key_Backspace);

  QCOMPARE(session.tags().size(), 0);
}

void WorkWindowCorrectnessTest::movedMarkAppearsInTheTagsTable() {
  TagSession session;
  WorkWindow window;
  window.setTagSession(&session);

  TagSession::GameTag tag = playTag(QStringLiteral("Goal"), 10000);
  tag.startMs = 1000;
  tag.endMs = 20000;
  session.addTag(tag);

  QAbstractItemModel* model = tagsTable(window)->model();
  QVERIFY(model);
  QCOMPARE(model->data(model->index(0, 0)).toString(), QStringLiteral("00:10"));
  QVERIFY(session.setTagInterval(0, 12000, 15000));
  QCOMPARE(session.tags().at(0).markMs, static_cast<qint64>(12000));
  QCOMPARE(model->data(model->index(0, 0)).toString(), QStringLiteral("00:12"));
}

void WorkWindowCorrectnessTest::presentationTrimKeepsTheEventMark() {
  TagSession session;
  TagSession::GameTag tag = playTag(QStringLiteral("Goal"), 10000);
  tag.startMs = 0;
  tag.endMs = 20000;
  session.addTag(tag);

  PresentationQueue queue;
  queue.setTagSession(&session);
  queue.setVideoDurationMs(600000);
  queue.setSelectedTagIndexes({0});
  queue.setClipInterval(0, 0, 4000);

  QCOMPARE(session.tags().at(0).markMs, static_cast<qint64>(10000));
  QVERIFY(session.tags().at(0).startMs <= session.tags().at(0).markMs);
  QVERIFY(session.tags().at(0).endMs >= session.tags().at(0).markMs);
}

void WorkWindowCorrectnessTest::trimHandlesStopAtTheEventMark() {
  constexpr qint64 windowEndMs = 60000;
  constexpr qint64 markMs = 10000;

  ClipTrimBar endBar;
  endBar.resize(620, 80);
  endBar.show();
  QApplication::processEvents();
  QVERIFY(endBar.width() > 100);
  endBar.configure(markMs, 0, 20000, 0, windowEndMs);
  dragTrimHandle(endBar, QPoint(xForTrimMs(endBar, 20000, windowEndMs), 10),
                 QPoint(xForTrimMs(endBar, 0, windowEndMs), 10));
  QVERIFY(endBar.clipEndMs() >= markMs);
  QVERIFY(endBar.clipStartMs() <= markMs);

  ClipTrimBar startBar;
  startBar.resize(620, 80);
  startBar.show();
  QApplication::processEvents();
  startBar.configure(markMs, 0, 20000, 0, windowEndMs);
  dragTrimHandle(startBar, QPoint(xForTrimMs(startBar, 0, windowEndMs), 10),
                 QPoint(xForTrimMs(startBar, windowEndMs, windowEndMs), 10));
  QVERIFY(startBar.clipStartMs() <= markMs);
  QVERIFY(startBar.clipEndMs() >= markMs);
}

int runWorkWindowCorrectnessTests(int argc, char** argv) {
  WorkWindowCorrectnessTest tests;
  return QTest::qExec(&tests, argc, argv);
}

#include "WorkWindowCorrectnessTests.moc"
