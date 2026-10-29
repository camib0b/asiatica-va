#include "TagSession.h"
#include "ThemeColors.h"
#include "VideoPlayer.h"
#include "WorkWindow.h"
#include "XmlDropHighlight.h"
#include "XmlImportDrop.h"
#include "XmlImporter.h"
#include "XmlSyncDialog.h"

#include <QApplication>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QMimeData>
#include <QPalette>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTextEdit>
#include <QTimer>
#include <QUrl>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace {

QString writeBytes(const QString& path, const QByteArray& bytes) {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
  if (file.write(bytes) != bytes.size()) return {};
  file.close();
  return QFileInfo(path).absoluteFilePath();
}

QString writePlaceholderVideo(QTemporaryDir& directory) {
  return writeBytes(directory.filePath(QStringLiteral("game.mp4")), "not a real video");
}

QString writeXmlFile(QTemporaryDir& directory, const QString& fileName, const QString& body) {
  return writeBytes(directory.filePath(fileName), body.toUtf8());
}

const QString kFirstEventXml = QStringLiteral(
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<ALL_INSTANCES>\n"
    "  <instance>\n"
    "    <ID>1</ID>\n"
    "    <start>12.5</start>\n"
    "    <end>14</end>\n"
    "    <code>Shot</code>\n"
    "  </instance>\n"
    "</ALL_INSTANCES>\n");

TagSession::GameTag playTag() {
  TagSession::GameTag tag;
  tag.setMainEventKey(TagEvent::keyFromCanonicalName(QStringLiteral("Goal")));
  tag.markMs = 1000;
  tag.team = TagSession::teamSideKey(TagSession::TeamSide::Home);
  return tag;
}

double linearChannel(int component) {
  const double encoded = component / 255.0;
  if (encoded <= 0.04045) return encoded / 12.92;
  return std::pow((encoded + 0.055) / 1.055, 2.4);
}

double contrastRatio(const QColor& first, const QColor& second) {
  const auto luminance = [](const QColor& color) {
    return 0.2126 * linearChannel(color.red()) + 0.7152 * linearChannel(color.green()) +
           0.0722 * linearChannel(color.blue());
  };
  const double firstLuminance = luminance(first);
  const double secondLuminance = luminance(second);
  const double lighter = std::max(firstLuminance, secondLuminance);
  const double darker = std::min(firstLuminance, secondLuminance);
  return (lighter + 0.05) / (darker + 0.05);
}

QColor pixelAt(const QImage& image, int logicalX, int logicalY) {
  const qreal ratio = image.devicePixelRatio() > 0 ? image.devicePixelRatio() : 1.0;
  const int x = std::clamp(static_cast<int>(std::lround(logicalX * ratio)), 0, image.width() - 1);
  const int y = std::clamp(static_cast<int>(std::lround(logicalY * ratio)), 0, image.height() - 1);
  return image.pixelColor(x, y);
}

class ModalWatchdog {
 public:
  ModalWatchdog() {
    timer_.setSingleShot(true);
    QObject::connect(&timer_, &QTimer::timeout, &timer_, [] {
      if (QWidget* modal = QApplication::activeModalWidget()) modal->close();
    });
    timer_.start(3000);
  }
  ~ModalWatchdog() { timer_.stop(); }

  ModalWatchdog(const ModalWatchdog&) = delete;
  ModalWatchdog& operator=(const ModalWatchdog&) = delete;

 private:
  QTimer timer_;
};

bool sendDragEnter(QWidget* target, const QMimeData* mimeData) {
  QDragEnterEvent event(QPoint(40, 28), Qt::CopyAction, mimeData, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(target, &event);
  return event.isAccepted();
}

bool sendDragMove(QWidget* target, const QMimeData* mimeData) {
  QDragMoveEvent event(QPoint(48, 36), Qt::CopyAction, mimeData, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(target, &event);
  return event.isAccepted();
}

bool sendDrop(QWidget* target, const QMimeData* mimeData) {
  QDropEvent event(QPointF(40, 28), Qt::CopyAction, mimeData, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(target, &event);
  return event.isAccepted();
}

void sendDragLeave(QWidget* target) {
  QDragLeaveEvent event;
  QApplication::sendEvent(target, &event);
}

QWidget* dropHighlight(WorkWindow& window) {
  return window.findChild<QWidget*>(QStringLiteral("XmlDropHighlight"), Qt::FindDirectChildrenOnly);
}

bool highlightVisible(WorkWindow& window) {
  QWidget* highlight = dropHighlight(window);
  return highlight != nullptr && highlight->isVisible();
}

QTextEdit* matchNotes(WorkWindow& window) {
  return window.findChild<QTextEdit*>();
}

void showWorkSurface(WorkWindow& window) {
  QFile themeFile(QStringLiteral(AVA_THEME_QSS_PATH));
  if (themeFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    window.setStyleSheet(QString::fromUtf8(themeFile.readAll()));
  }
  auto* setupPage = window.findChild<QWidget*>(QStringLiteral("AppRoot"));
  if (auto* stack = qobject_cast<QStackedWidget*>(setupPage ? setupPage->parentWidget() : nullptr)) {
    if (stack->count() > 1) stack->setCurrentIndex(1);
  }
  window.resize(960, 640);
  window.show();
  QApplication::processEvents();
}

}  // namespace

class XmlFileDropTest : public QObject {
  Q_OBJECT

 private slots:
  void sessionAllowsDropOnlyBeforeTheFirstTag();
  void singleXmlFileIsAcceptedAndOtherPayloadsAreNot();
  void syncOffsetPlacesTheAnchorOnThePlayhead();
  void syncDialogProposesTheCurrentPlayhead();
  void dropRingMeetsContrastOnAppSurfaces();
  void dropIsIgnoredWithoutAVideo();
  void dropIsIgnoredOnceATagExists();
  void dropIgnoresOtherFiles();
  void emptySessionDropProposesThePlayheadForTheFirstEvent();
  void closedVideoIgnoresALaterDrop();
};

void XmlFileDropTest::sessionAllowsDropOnlyBeforeTheFirstTag() {
  QVERIFY(!XmlImportDrop::sessionAllowsDrop(false, 0));
  QVERIFY(XmlImportDrop::sessionAllowsDrop(true, 0));
  QVERIFY(!XmlImportDrop::sessionAllowsDrop(true, 1));
  QVERIFY(!XmlImportDrop::sessionAllowsDrop(true, 4));
}

void XmlFileDropTest::singleXmlFileIsAcceptedAndOtherPayloadsAreNot() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString xmlPath = writeXmlFile(directory, QStringLiteral("match.XML"), kFirstEventXml);
  const QString secondXmlPath = writeXmlFile(directory, QStringLiteral("other.xml"), kFirstEventXml);
  const QString textPath = writeBytes(directory.filePath(QStringLiteral("notes.txt")), "hello");
  const QString directoryPath = directory.filePath(QStringLiteral("folder.xml"));
  QVERIFY(!xmlPath.isEmpty());
  QVERIFY(!secondXmlPath.isEmpty());
  QVERIFY(!textPath.isEmpty());
  QVERIFY(QDir().mkpath(directoryPath));

  QMimeData oneXml;
  oneXml.setUrls({QUrl::fromLocalFile(xmlPath)});
  const XmlImportDrop::Offer accepted = XmlImportDrop::classify(&oneXml, true);
  QCOMPARE(accepted.decision, XmlImportDrop::Decision::Accept);
  QCOMPARE(accepted.filePath, QFileInfo(xmlPath).absoluteFilePath());

  const XmlImportDrop::Offer rejected = XmlImportDrop::classify(&oneXml, false);
  QCOMPARE(rejected.decision, XmlImportDrop::Decision::Reject);
  QCOMPARE(rejected.filePath, accepted.filePath);

  QMimeData twoFiles;
  twoFiles.setUrls({QUrl::fromLocalFile(xmlPath), QUrl::fromLocalFile(secondXmlPath)});
  QCOMPARE(XmlImportDrop::classify(&twoFiles, true).decision, XmlImportDrop::Decision::Unrelated);

  QMimeData textFile;
  textFile.setUrls({QUrl::fromLocalFile(textPath)});
  QCOMPARE(XmlImportDrop::classify(&textFile, true).decision, XmlImportDrop::Decision::Unrelated);

  QMimeData directoryUrl;
  directoryUrl.setUrls({QUrl::fromLocalFile(directoryPath)});
  QCOMPARE(XmlImportDrop::classify(&directoryUrl, true).decision, XmlImportDrop::Decision::Unrelated);

  QMimeData missingFile;
  missingFile.setUrls({QUrl::fromLocalFile(directory.filePath(QStringLiteral("missing.xml")))});
  QCOMPARE(XmlImportDrop::classify(&missingFile, true).decision, XmlImportDrop::Decision::Unrelated);

  QMimeData remoteUrl;
  remoteUrl.setUrls({QUrl(QStringLiteral("https://example.com/match.xml"))});
  QCOMPARE(XmlImportDrop::classify(&remoteUrl, true).decision, XmlImportDrop::Decision::Unrelated);

  QMimeData empty;
  QCOMPARE(XmlImportDrop::classify(&empty, true).decision, XmlImportDrop::Decision::Unrelated);
  QCOMPARE(XmlImportDrop::classify(nullptr, true).decision, XmlImportDrop::Decision::Unrelated);
}

void XmlFileDropTest::syncOffsetPlacesTheAnchorOnThePlayhead() {
  QCOMPARE(XmlImporter::syncOffsetMsForPlayhead(0, 12500), qint64{-12500});
  QCOMPARE(qint64{12500} + XmlImporter::syncOffsetMsForPlayhead(0, 12500), qint64{0});
  QCOMPARE(XmlImporter::syncOffsetMsForPlayhead(8300, 12500), qint64{-4200});
  QCOMPARE(qint64{12500} + XmlImporter::syncOffsetMsForPlayhead(8300, 12500), qint64{8300});
  QCOMPARE(XmlImporter::syncOffsetMsForPlayhead(20000, 12500), qint64{7500});
  QCOMPARE(qint64{12500} + XmlImporter::syncOffsetMsForPlayhead(20000, 12500), qint64{20000});
}

void XmlFileDropTest::syncDialogProposesTheCurrentPlayhead() {
  VideoPlayer player;
  player.seekToMs(8300);
  const qint64 playheadMs = player.currentPositionMs();

  XmlImporter::ParsedInstance firstEvent;
  firstEvent.startMs = 12500;
  firstEvent.endMs = 14000;
  firstEvent.code = QStringLiteral("Shot");
  XmlSyncDialog dialog(&player, firstEvent, {firstEvent}, true);

  QCOMPARE(dialog.offsetMs(), XmlImporter::syncOffsetMsForPlayhead(playheadMs, firstEvent.startMs));
  QCOMPARE(firstEvent.startMs + dialog.offsetMs(), playheadMs);

  QVERIFY(QMetaObject::invokeMethod(&player, "positionChangedMs", Qt::DirectConnection,
                                    Q_ARG(qint64, qint64{8300})));
  QCOMPARE(firstEvent.startMs + dialog.offsetMs(), qint64{8300});
}

void XmlFileDropTest::dropRingMeetsContrastOnAppSurfaces() {
  const QColor stroke = XmlDropHighlight::strokeColor();
  QCOMPARE(stroke.rgb(), Style::ThemeColors::foreground().rgb());
  QVERIFY(XmlDropHighlight::edgeInsetPx + XmlDropHighlight::strokePx < WorkWindow::kContentMarginPx);

  const QColor surfaces[] = {Style::ThemeColors::surface(), Style::ThemeColors::primaryForeground()};
  for (const QColor& background : surfaces) {
    QWidget canvas;
    canvas.setAutoFillBackground(true);
    QPalette palette = canvas.palette();
    palette.setColor(QPalette::Window, background);
    canvas.setPalette(palette);
    canvas.resize(160, 100);
    auto* highlight = new XmlDropHighlight(&canvas);
    highlight->setGeometry(canvas.rect());
    highlight->show();
    canvas.show();
    QVERIFY(QTest::qWaitForWindowExposed(&canvas));

    const QImage image = canvas.grab().toImage();
    QVERIFY(!image.isNull());
    const int strokeX = XmlDropHighlight::edgeInsetPx + XmlDropHighlight::strokePx / 2;
    const QColor paintedStroke = pixelAt(image, strokeX, 24);
    const QColor outside = pixelAt(image, XmlDropHighlight::edgeInsetPx - 1, 24);
    const QColor inside =
        pixelAt(image, XmlDropHighlight::edgeInsetPx + XmlDropHighlight::strokePx + 1, 24);
    QCOMPARE(paintedStroke.rgb(), stroke.rgb());
    QCOMPARE(outside.rgb(), background.rgb());
    QCOMPARE(inside.rgb(), background.rgb());
    QVERIFY2(contrastRatio(paintedStroke, outside) >= 3.0,
             qPrintable(QString::number(contrastRatio(paintedStroke, outside), 'f', 2)));
    QVERIFY2(contrastRatio(paintedStroke, inside) >= 3.0,
             qPrintable(QString::number(contrastRatio(paintedStroke, inside), 'f', 2)));
    QCOMPARE(highlight->findChildren<QLabel*>().size(), 0);
  }
}

void XmlFileDropTest::dropIsIgnoredWithoutAVideo() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString xmlPath = writeXmlFile(directory, QStringLiteral("match.xml"), kFirstEventXml);
  QVERIFY(!xmlPath.isEmpty());

  TagSession session;
  WorkWindow window;
  window.setTagSession(&session);
  showWorkSurface(window);
  QMimeData mimeData;
  mimeData.setUrls({QUrl::fromLocalFile(xmlPath)});

  QTextEdit* notes = matchNotes(window);
  QVERIFY(notes != nullptr);
  const QString notesBefore = notes->toPlainText();
  QVERIFY(!sendDragEnter(notes, &mimeData));
  QVERIFY(!sendDrop(notes, &mimeData));
  QCOMPARE(notes->toPlainText(), notesBefore);
  QCOMPARE(session.tags().size(), 0);
  QVERIFY(!highlightVisible(window));
  QVERIFY(QApplication::activeModalWidget() == nullptr);
}

void XmlFileDropTest::dropIsIgnoredOnceATagExists() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString videoPath = writePlaceholderVideo(directory);
  const QString xmlPath = writeXmlFile(directory, QStringLiteral("match.xml"), kFirstEventXml);
  QVERIFY(!videoPath.isEmpty());
  QVERIFY(!xmlPath.isEmpty());

  TagSession session;
  WorkWindow window;
  window.setTagSession(&session);
  window.loadVideoFromFile(videoPath);
  QVERIFY(window.hasOpenVideo());
  session.addTag(playTag());
  QCOMPARE(session.tags().size(), 1);
  showWorkSurface(window);

  QMimeData mimeData;
  mimeData.setUrls({QUrl::fromLocalFile(xmlPath)});
  QTextEdit* notes = matchNotes(window);
  QVERIFY(notes != nullptr);
  const QString notesBefore = notes->toPlainText();
  QVERIFY(!sendDragEnter(notes, &mimeData));
  QVERIFY(!sendDragMove(notes, &mimeData));
  QVERIFY(!sendDrop(&window, &mimeData));
  QCOMPARE(notes->toPlainText(), notesBefore);
  QCOMPARE(session.tags().size(), 1);
  QVERIFY(!highlightVisible(window));
  QVERIFY(QApplication::activeModalWidget() == nullptr);
}

void XmlFileDropTest::dropIgnoresOtherFiles() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString videoPath = writePlaceholderVideo(directory);
  const QString xmlPath = writeXmlFile(directory, QStringLiteral("match.xml"), kFirstEventXml);
  const QString secondXmlPath = writeXmlFile(directory, QStringLiteral("other.xml"), kFirstEventXml);
  const QString textPath = writeBytes(directory.filePath(QStringLiteral("notes.txt")), "hello");
  const QString directoryPath = directory.filePath(QStringLiteral("folder.xml"));
  QVERIFY(!videoPath.isEmpty());
  QVERIFY(!xmlPath.isEmpty());
  QVERIFY(!secondXmlPath.isEmpty());
  QVERIFY(!textPath.isEmpty());
  QVERIFY(QDir().mkpath(directoryPath));

  TagSession session;
  WorkWindow window;
  window.setTagSession(&session);
  window.loadVideoFromFile(videoPath);
  QVERIFY(window.hasOpenVideo());
  QCOMPARE(session.tags().size(), 0);
  showWorkSurface(window);

  const QList<QList<QUrl>> payloads = {
      {QUrl::fromLocalFile(textPath)},
      {QUrl::fromLocalFile(xmlPath), QUrl::fromLocalFile(secondXmlPath)},
      {QUrl::fromLocalFile(directoryPath)},
      {QUrl::fromLocalFile(directory.filePath(QStringLiteral("missing.xml")))},
      {QUrl(QStringLiteral("https://example.com/match.xml"))},
  };
  for (const QList<QUrl>& urls : payloads) {
    QMimeData mimeData;
    mimeData.setUrls(urls);
    QVERIFY(!sendDragEnter(&window, &mimeData));
    QVERIFY(!sendDrop(&window, &mimeData));
    QVERIFY(!highlightVisible(window));
  }
  QCOMPARE(session.tags().size(), 0);
  QVERIFY(QApplication::activeModalWidget() == nullptr);
}

void XmlFileDropTest::emptySessionDropProposesThePlayheadForTheFirstEvent() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString videoPath = writePlaceholderVideo(directory);
  const QString xmlPath = writeXmlFile(directory, QStringLiteral("Match.XML"), kFirstEventXml);
  QVERIFY(!videoPath.isEmpty());
  QVERIFY(!xmlPath.isEmpty());

  QVector<XmlImporter::ParsedInstance> instances;
  QString parseError;
  QVERIFY2(XmlImporter::parse(xmlPath, &instances, &parseError), qPrintable(parseError));
  QCOMPARE(instances.size(), 1);
  const XmlImporter::SyncAnchorResult anchor = XmlImporter::syncAnchorInstance(instances);
  QVERIFY(anchor.found);
  QCOMPARE(anchor.instance.startMs, instances.first().startMs);
  QCOMPARE(anchor.instance.code, QStringLiteral("Shot"));

  TagSession session;
  WorkWindow window;
  window.setTagSession(&session);
  window.loadVideoFromFile(videoPath);
  QVERIFY(window.hasOpenVideo());
  QCOMPARE(session.tags().size(), 0);
  showWorkSurface(window);

  auto* player = window.findChild<VideoPlayer*>();
  QVERIFY(player != nullptr);
  player->seekToMs(8300);
  const qint64 playheadBeforeDrop = player->currentPositionMs();

  QMimeData mimeData;
  mimeData.setUrls({QUrl::fromLocalFile(xmlPath)});
  QTextEdit* notes = matchNotes(window);
  QVERIFY(notes != nullptr);
  const QString notesBefore = notes->toPlainText();

  QVERIFY(sendDragEnter(notes, &mimeData));
  QVERIFY(sendDragMove(&window, &mimeData));
  QVERIFY(highlightVisible(window));

  const QImage whileDragging = window.grab().toImage();
  QVERIFY(!whileDragging.isNull());
  const int strokeX = XmlDropHighlight::edgeInsetPx + XmlDropHighlight::strokePx / 2;
  const QColor paintedStroke = pixelAt(whileDragging, strokeX, 24);
  const QColor besideStroke = pixelAt(whileDragging, XmlDropHighlight::edgeInsetPx - 1, 24);
  QCOMPARE(paintedStroke.rgb(), XmlDropHighlight::strokeColor().rgb());
  QVERIFY2(contrastRatio(paintedStroke, besideStroke) >= 3.0,
           qPrintable(QString::number(contrastRatio(paintedStroke, besideStroke), 'f', 2)));
  QCOMPARE(dropHighlight(window)->findChildren<QLabel*>().size(), 0);

  bool sawSyncDialog = false;
  qint64 proposedOffsetMs = 0;
  qint64 playheadAtDialogMs = 0;
  QTimer inspect;
  inspect.setSingleShot(true);
  QObject::connect(&inspect, &QTimer::timeout, &window, [&] {
    auto* dialog = qobject_cast<XmlSyncDialog*>(QApplication::activeModalWidget());
    if (dialog == nullptr) {
      if (QWidget* modal = QApplication::activeModalWidget()) modal->close();
      return;
    }
    sawSyncDialog = true;
    proposedOffsetMs = dialog->offsetMs();
    playheadAtDialogMs = player->currentPositionMs();
    dialog->reject();
  });

  ModalWatchdog watchdog;
  inspect.start(0);
  QVERIFY(sendDrop(notes, &mimeData));
  inspect.stop();

  QVERIFY2(sawSyncDialog, "XML drop did not open the sync dialog");
  QCOMPARE(playheadAtDialogMs, playheadBeforeDrop);
  QCOMPARE(proposedOffsetMs,
           XmlImporter::syncOffsetMsForPlayhead(playheadAtDialogMs, anchor.instance.startMs));
  QCOMPARE(anchor.instance.startMs + proposedOffsetMs, playheadAtDialogMs);
  QCOMPARE(notes->toPlainText(), notesBefore);
  QCOMPARE(session.tags().size(), 0);
  QVERIFY(!highlightVisible(window));
}

void XmlFileDropTest::closedVideoIgnoresALaterDrop() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString videoPath = writePlaceholderVideo(directory);
  const QString xmlPath = writeXmlFile(directory, QStringLiteral("match.xml"), kFirstEventXml);
  QVERIFY(!videoPath.isEmpty());
  QVERIFY(!xmlPath.isEmpty());

  TagSession session;
  WorkWindow window;
  window.setTagSession(&session);
  window.loadVideoFromFile(videoPath);
  QVERIFY(window.hasOpenVideo());
  QVERIFY(QMetaObject::invokeMethod(&window, "onCloseVideo"));
  QVERIFY(!window.hasOpenVideo());
  showWorkSurface(window);

  QMimeData mimeData;
  mimeData.setUrls({QUrl::fromLocalFile(xmlPath)});
  QVERIFY(!sendDragEnter(&window, &mimeData));
  QVERIFY(!sendDrop(&window, &mimeData));
  QVERIFY(!highlightVisible(window));
  QCOMPARE(session.tags().size(), 0);
  QVERIFY(QApplication::activeModalWidget() == nullptr);
}

int runXmlFileDropTests(int argc, char** argv) {
  XmlFileDropTest tests;
  return QTest::qExec(&tests, argc, argv);
}

#include "XmlFileDropTests.moc"
