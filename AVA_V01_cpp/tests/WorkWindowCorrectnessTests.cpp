#include "AppLocale.h"
#include "MatchSessionFile.h"
#include "TagSession.h"
#include "WorkWindow.h"

#include <QApplication>
#include <QAbstractItemModel>
#include <QAction>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QItemSelectionModel>
#include <QLabel>
#include <QMediaPlayer>
#include <QMenu>
#include <QMetaObject>
#include <QPlainTextEdit>
#include <QStringList>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

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

  QTableView* table = tagsTable(window);
  QVERIFY(table);
  QAbstractItemModel* model = table->model();
  QVERIFY(model);

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

  session.addTag(playTag(QStringLiteral("Goal"), 1000));

  QCOMPARE(maxDepth, 1);
  QCOMPARE(depth, 0);
  QCOMPARE(model->rowCount(), 2);
  QCOMPARE(injectedAdds, 1);
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

int runWorkWindowCorrectnessTests(int argc, char** argv) {
  WorkWindowCorrectnessTest tests;
  return QTest::qExec(&tests, argc, argv);
}

#include "WorkWindowCorrectnessTests.moc"
