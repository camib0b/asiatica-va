#include "ClipFilterGraph.h"
#include "DrawingGraphicsItem.h"
#include "ExportClipBuilder.h"
#include "ExportSettingsDialog.h"
#include "Telestration.h"
#include "VideoPlayer.h"

#include <QCheckBox>
#include <QGraphicsVideoItem>
#include <QGraphicsView>
#include <QImage>
#include <QPainter>
#include <QTemporaryDir>
#include <QTest>
#include <QVideoSink>

#include <cmath>

namespace {

void expectNormalizedPoint(const Telestration::Point& point, double x, double y) {
    QVERIFY(std::abs(point.x - x) < 0.000001);
    QVERIFY(std::abs(point.y - y) < 0.000001);
}

void expectVideoPoint(const QPointF& point, double x, double y) {
    QVERIFY(std::abs(point.x() - x) < 0.000001);
    QVERIFY(std::abs(point.y() - y) < 0.000001);
}

bool pixelIsInk(const QColor& pixel, const QColor& ink) {
    return pixel.alpha() > 200 && std::abs(pixel.red() - ink.red()) < 12
           && std::abs(pixel.green() - ink.green()) < 12
           && std::abs(pixel.blue() - ink.blue()) < 12;
}

bool imageHasInkNear(const QImage& image, const QPoint& point, const QColor& ink, int radius) {
    for (int yOffset = -radius; yOffset <= radius; ++yOffset) {
        for (int xOffset = -radius; xOffset <= radius; ++xOffset) {
            const QPoint sample = point + QPoint(xOffset, yOffset);
            if (!image.rect().contains(sample)) continue;
            if (pixelIsInk(image.pixelColor(sample), ink)) return true;
        }
    }
    return false;
}

Telestration::Drawing horizontalArrow(Telestration::InkColor color) {
    Telestration::Drawing drawing;
    drawing.id = 7;
    drawing.anchorMs = 1000;
    drawing.holdMs = 3000;
    Telestration::Shape shape;
    shape.kind = Telestration::ShapeKind::Arrow;
    shape.color = color;
    shape.strokeWidth = 0.12;
    shape.points = {Telestration::Point{0.1, 0.5}, Telestration::Point{0.9, 0.5}};
    drawing.shapes = {shape};
    return drawing;
}

QImage paintDrawingImage(const Telestration::Drawing& drawing, const QSize& size) {
    QImage image(size, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    Telestration::paintDrawing(painter, QRectF(QPointF(0, 0), QSizeF(size)), drawing);
    painter.end();
    return image;
}

QImage paintItemImage(const Telestration::Drawing& drawing, const QSize& size) {
    DrawingGraphicsItem item(drawing);
    item.setExplicitRectangle(QRectF(QPointF(0, 0), QSizeF(size)));
    QImage image(size, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    item.paint(&painter, nullptr, nullptr);
    painter.end();
    return image;
}

ClipFilterGraphInput plainGraph() {
    ClipFilterGraphInput input;
    input.outputVideoSize = QSize(1920, 1080);
    input.topSafeY = QStringLiteral("main_h*0.065");
    input.bottomSafeY = QStringLiteral("main_h-overlay_h-main_h*0.120");
    input.shootoutY = QStringLiteral("main_h*0.065+40");
    input.bottomOverlayLeftMargin = 24;
    input.cornerMargin = 16;
    return input;
}

TagSession::GameTag passTag(qint64 startMs, qint64 endMs, qint64 markMs) {
    TagSession::GameTag tag;
    tag.mainEventId = EventCatalog::EventId::Pass;
    tag.markMs = markMs;
    tag.startMs = startMs;
    tag.endMs = endMs;
    tag.team = QStringLiteral("Home");
    return tag;
}

}  // namespace

class TelestrationTest : public QObject {
    Q_OBJECT

private slots:
    void normalizedCoordinatesRoundTripAndClamp();
    void sharedPainterMatchesGraphicsItemAndPng();
    void timelineWindowAndFreezeCues();
    void tagCoveringPositionPicksTightestInterval();
    void exportSegmentsOmitDrawingsUnlessRequested();
    void exportFilterIsUnchangedWhenDrawingsAreOff();
    void exportFilterTimesDrawingsOverMovingVideo();
    void includeDrawingsCheckboxDefaultsOff();
    void videoPictureUsesGraphicsViewSink();
};

void TelestrationTest::normalizedCoordinatesRoundTripAndClamp() {
    const QRectF picture(0, 0, 200, 100);
    const Telestration::Point center =
        Telestration::normalizedPointFromVideo(QPointF(50, 25), picture);
    expectNormalizedPoint(center, 0.25, 0.25);
    expectVideoPoint(Telestration::videoPointFromNormalized(center, picture), 50, 25);

    const QRectF letterboxed(10, 20, 100, 50);
    expectNormalizedPoint(Telestration::normalizedPointFromVideo(QPointF(10, 20), letterboxed), 0, 0);
    expectNormalizedPoint(Telestration::normalizedPointFromVideo(QPointF(110, 70), letterboxed), 1, 1);
    expectVideoPoint(Telestration::videoPointFromNormalized(Telestration::Point{0, 0}, letterboxed), 10, 20);
    expectVideoPoint(Telestration::videoPointFromNormalized(Telestration::Point{1, 1}, letterboxed), 110, 70);

    expectNormalizedPoint(Telestration::normalizedPointFromVideo(QPointF(-40, 400), picture), 0, 1);
    expectVideoPoint(Telestration::videoPointFromNormalized(Telestration::Point{-2, 3}, picture), 0, 100);
    expectNormalizedPoint(Telestration::normalizedPointFromVideo(QPointF(10, 10), QRectF()), 0, 0);
    expectVideoPoint(Telestration::videoPointFromNormalized(Telestration::Point{0.5, 0.5}, QRectF()), 0, 0);
}

void TelestrationTest::sharedPainterMatchesGraphicsItemAndPng() {
    const QSize size(200, 100);
    const Telestration::Drawing drawing = horizontalArrow(Telestration::InkColor::Red);
    const QImage direct = paintDrawingImage(drawing, size);
    const QImage throughItem = paintItemImage(drawing, size);
    QCOMPARE(throughItem, direct);

    const QColor red = Telestration::colorForInk(Telestration::InkColor::Red);
    QVERIFY(imageHasInkNear(direct, QPoint(100, 50), red, 2));
    QCOMPARE(direct.pixelColor(QPoint(2, 2)).alpha(), 0);

    Telestration::Drawing circle = drawing;
    circle.shapes[0].kind = Telestration::ShapeKind::Circle;
    circle.shapes[0].color = Telestration::InkColor::Blue;
    circle.shapes[0].strokeWidth = 0.06;
    circle.shapes[0].points = {Telestration::Point{0.5, 0.5}, Telestration::Point{0.5, 0.8}};
    const QImage circleImage = paintDrawingImage(circle, size);
    const QColor blue = Telestration::colorForInk(Telestration::InkColor::Blue);
    QVERIFY(imageHasInkNear(circleImage, QPoint(100, 80), blue, 4));
    QCOMPARE(circleImage.pixelColor(QPoint(100, 50)).alpha(), 0);

    Telestration::Drawing dotted = drawing;
    dotted.shapes[0].kind = Telestration::ShapeKind::DottedArrow;
    dotted.shapes[0].color = Telestration::InkColor::White;
    dotted.shapes[0].strokeWidth = 0.08;
    const QImage dottedImage = paintDrawingImage(dotted, size);
    QVERIFY(imageHasInkNear(dottedImage, QPoint(180, 50), Telestration::colorForInk(Telestration::InkColor::White), 8));

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("drawing.png"));
    QVERIFY(Telestration::writeDrawingPng(drawing, size, path));
    const QImage fromFile(path);
    QVERIFY(imageHasInkNear(fromFile, QPoint(100, 50), red, 2));
    QCOMPARE(fromFile.pixelColor(QPoint(2, 2)).alpha(), 0);
    QVERIFY(!Telestration::writeDrawingPng(drawing, QSize(), path));
}

void TelestrationTest::timelineWindowAndFreezeCues() {
    Telestration::Drawing drawing;
    drawing.id = 4;
    drawing.anchorMs = 5000;
    drawing.holdMs = 3000;
    drawing.tagId = 9;
    QVERIFY(Telestration::drawingVisibleOnTimeline(drawing, 5000));
    QVERIFY(Telestration::drawingVisibleOnTimeline(drawing, 7999));
    QVERIFY(!Telestration::drawingVisibleOnTimeline(drawing, 8000));
    QVERIFY(!Telestration::drawingVisibleOnTimeline(drawing, 4999));

    Telestration::ClipWindow clip;
    clip.tagId = 9;
    clip.startMs = 4000;
    clip.endMs = 12000;
    QSet<quint64> held;
    const std::optional<Telestration::FreezeCue> cue = Telestration::earliestFreezeCue(
        {drawing}, clip, 4500, 5200, true, held);
    QVERIFY(cue.has_value());
    QCOMPARE(cue->drawingId, quint64{4});
    QCOMPARE(cue->holdMs, qint64{3000});

    QVERIFY(!Telestration::earliestFreezeCue({drawing}, clip, 4500, 5200, false, held).has_value());
    QVERIFY(!Telestration::earliestFreezeCue({drawing}, clip, 1000, 5200, true, held).has_value());
    held.insert(4);
    QVERIFY(!Telestration::earliestFreezeCue({drawing}, clip, 4500, 5200, true, held).has_value());

    Telestration::ClipWindow otherClip = clip;
    otherClip.tagId = 3;
    QVERIFY(!Telestration::earliestFreezeCue({drawing}, otherClip, 4500, 5200, true, {}).has_value());

    Telestration::Drawing freeStanding = drawing;
    freeStanding.id = 5;
    freeStanding.tagId.reset();
    QVERIFY(Telestration::earliestFreezeCue({freeStanding}, clip, 4500, 5200, true, {}).has_value());
    freeStanding.anchorMs = 1000;
    QVERIFY(!Telestration::drawingBelongsToClip(freeStanding, clip));

    Telestration::Drawing second = drawing;
    second.id = 8;
    second.anchorMs = 5100;
    held.clear();
    const std::optional<Telestration::FreezeCue> first = Telestration::earliestFreezeCue(
        {second, drawing}, clip, 4500, 5300, true, held);
    QVERIFY(first.has_value());
    QCOMPARE(first->drawingId, quint64{4});
    held.insert(first->drawingId);
    const std::optional<Telestration::FreezeCue> chained = Telestration::earliestUnheldCueThrough(
        {drawing, second}, clip, 4500, 5300, held);
    QVERIFY(chained.has_value());
    QCOMPARE(chained->drawingId, quint64{8});

    Telestration::forgetHeldDrawingsAfterRewind(held, {drawing, second}, 4000);
    QVERIFY(!held.contains(4));
    held.insert(8);
    Telestration::forgetHeldDrawingsAfterRewind(held, {drawing, second}, 6000);
    QVERIFY(held.contains(8));
}

void TelestrationTest::tagCoveringPositionPicksTightestInterval() {
    TagSession session;
    session.addTag(passTag(0, 20000, 10000));
    session.addTag(passTag(8000, 12000, 10000));
    const quint64 wideId = session.tags().at(0).id;
    const quint64 tightId = session.tags().at(1).id;
    const std::optional<quint64> covering = Telestration::tagIdCoveringPosition(session, 10000);
    QVERIFY(covering.has_value());
    QCOMPARE(*covering, tightId);
    QVERIFY(!Telestration::tagIdCoveringPosition(session, 30000).has_value());
    Q_UNUSED(wideId);
}

void TelestrationTest::exportSegmentsOmitDrawingsUnlessRequested() {
    TagSession session;
    session.addTag(passTag(10000, 20000, 15000));
    const TagSession::GameTag stored = session.tags().at(0);

    PresentationQueue::Clip clip;
    clip.tagId = stored.id;
    clip.markMs = stored.markMs;
    clip.startMs = stored.startMs;
    clip.endMs = stored.endMs;
    clip.mainEvent = stored.mainEventKey();
    clip.team = stored.team;

    Telestration::Drawing attached;
    attached.id = 1;
    attached.tagId = stored.id;
    attached.anchorMs = 16000;
    attached.holdMs = 3000;
    Telestration::Shape shape;
    shape.kind = Telestration::ShapeKind::Arrow;
    shape.points = {Telestration::Point{0.2, 0.2}, Telestration::Point{0.8, 0.8}};
    attached.shapes = {shape};

    Telestration::Drawing freeStanding;
    freeStanding.id = 2;
    freeStanding.anchorMs = 19000;
    freeStanding.holdMs = 3000;
    freeStanding.shapes = attached.shapes;

    Telestration::Drawing otherTag = attached;
    otherTag.id = 3;
    otherTag.tagId = stored.id + 50;

    Telestration::Drawing beforeClip = freeStanding;
    beforeClip.id = 4;
    beforeClip.anchorMs = 1000;
    beforeClip.holdMs = 500;

    ExportClipBuilder::OverlayOptions withDrawings;
    withDrawings.includeBottomOverlay = true;
    withDrawings.includeScoreboardOverlay = false;
    withDrawings.includeNotesOverlay = false;
    withDrawings.includeDrawings = true;
    withDrawings.drawings = {otherTag, freeStanding, attached, beforeClip};

    ExportClipBuilder::OverlayOptions withoutDrawings = withDrawings;
    withoutDrawings.includeDrawings = false;

    const QVector<ClipSegment> included =
        ExportClipBuilder::buildClipSegments(&session, {clip}, withDrawings);
    const QVector<ClipSegment> excluded =
        ExportClipBuilder::buildClipSegments(&session, {clip}, withoutDrawings);
    QCOMPARE(included.size(), 1);
    QCOMPARE(excluded.size(), 1);
    QCOMPARE(excluded.at(0).drawings.size(), 0);
    QCOMPARE(included.at(0).overlayText, excluded.at(0).overlayText);
    QCOMPARE(included.at(0).scoreboards.size(), excluded.at(0).scoreboards.size());
    QCOMPARE(included.at(0).shootouts.size(), excluded.at(0).shootouts.size());
    QCOMPARE(included.at(0).startMs, excluded.at(0).startMs);
    QCOMPARE(included.at(0).durationMs, excluded.at(0).durationMs);
    QCOMPARE(included.at(0).drawings.size(), 2);
    QCOMPARE(included.at(0).drawings.at(0).drawing.id, quint64{1});
    QVERIFY(std::abs(included.at(0).drawings.at(0).startOffsetSeconds - 6.0) < 0.000001);
    QVERIFY(std::abs(included.at(0).drawings.at(0).endOffsetSeconds - 9.0) < 0.000001);
    QCOMPARE(included.at(0).drawings.at(1).drawing.id, quint64{2});
    QVERIFY(std::abs(included.at(0).drawings.at(1).startOffsetSeconds - 9.0) < 0.000001);
    QVERIFY(std::abs(included.at(0).drawings.at(1).endOffsetSeconds - 10.0) < 0.000001);
}

void TelestrationTest::exportFilterIsUnchangedWhenDrawingsAreOff() {
    ClipFilterGraphInput input = plainGraph();
    const QString noOverlays = buildClipFilterComplex(input);
    QCOMPARE(noOverlays,
             QStringLiteral("[0:v]scale=1920:1080:force_original_aspect_ratio=decrease:"
                            "force_divisible_by=2,setsar=1[scaled];[scaled]null[v]"));
    QVERIFY(!noOverlays.contains(QStringLiteral("between(t,")));
    QVERIFY(!noOverlays.contains(QStringLiteral("tpad")));

    input.includeBottomOverlay = true;
    input.includeBrandingOverlay = true;
    const QString staticOverlays = buildClipFilterComplex(input);
    QCOMPARE(staticOverlays,
             QStringLiteral("[0:v]scale=1920:1080:force_original_aspect_ratio=decrease:"
                            "force_divisible_by=2,setsar=1[scaled];"
                            "[scaled][1:v]overlay=24:main_h-overlay_h-main_h*0.120[ov0];"
                            "[ov0][2:v]overlay=main_w-overlay_w-16:main_h*0.065[ov1];"
                            "[ov1]null[v]"));

    ClipFilterGraphInput scoreboards = plainGraph();
    scoreboards.scoreboards = {TimedOverlayPhase{0.0}, TimedOverlayPhase{2.5}};
    const QString scoreboardGraph = buildClipFilterComplex(scoreboards);
    QCOMPARE(scoreboardGraph,
             QStringLiteral("[0:v]scale=1920:1080:force_original_aspect_ratio=decrease:"
                            "force_divisible_by=2,setsar=1[scaled];"
                            "[scaled][1:v]overlay=16:main_h*0.065:enable='gte(t,0.000)*lt(t,2.500)'[sb0];"
                            "[sb0][2:v]overlay=16:main_h*0.065:enable='gte(t,2.500)'[v]"));
    QVERIFY(!scoreboardGraph.contains(QStringLiteral("between(t,")));

    ClipFilterGraphInput rotated = plainGraph();
    rotated.outputVideoSize = QSize(1280, 720);
    rotated.sourceRotationDegrees = 90;
    QCOMPARE(buildClipFilterComplex(rotated),
             QStringLiteral("[0:v]transpose=1,scale=1280:720:force_original_aspect_ratio=decrease:"
                            "force_divisible_by=2,setsar=1[scaled];[scaled]null[v]"));
}

void TelestrationTest::exportFilterTimesDrawingsOverMovingVideo() {
    ClipFilterGraphInput withDrawings = plainGraph();
    withDrawings.scoreboards = {TimedOverlayPhase{0.0}, TimedOverlayPhase{2.5}};
    DrawingOverlaySpan span;
    span.startOffsetSeconds = 1.0;
    span.endOffsetSeconds = 4.0;
    withDrawings.drawings = {span};

    const QString filter = buildClipFilterComplex(withDrawings);
    QCOMPARE(filter,
             QStringLiteral("[0:v]scale=1920:1080:force_original_aspect_ratio=decrease:"
                            "force_divisible_by=2,setsar=1[scaled];"
                            "[scaled][1:v]overlay=16:main_h*0.065:enable='gte(t,0.000)*lt(t,2.500)'[sb0];"
                            "[sb0][2:v]overlay=16:main_h*0.065:enable='gte(t,2.500)'[sb1];"
                            "[sb1][3:v]overlay=0:0:enable='between(t,1.000,4.000)'[v]"));
    QVERIFY(filter.contains(QStringLiteral("overlay=0:0:enable='between(t,1.000,4.000)'")));
    QVERIFY(!filter.contains(QStringLiteral("tpad")));
    QVERIFY(!filter.contains(QStringLiteral("loop")));

    withDrawings.drawings.clear();
    ClipFilterGraphInput withoutDrawings = plainGraph();
    withoutDrawings.scoreboards = withDrawings.scoreboards;
    QCOMPARE(buildClipFilterComplex(withDrawings), buildClipFilterComplex(withoutDrawings));
}

void TelestrationTest::includeDrawingsCheckboxDefaultsOff() {
    TagSession session;
    ExportSettingsDialog dialog(&session, QString(), QString(), {}, {}, nullptr);
    auto* checkbox = dialog.findChild<QCheckBox*>(QStringLiteral("IncludeDrawingsCheckBox"));
    QVERIFY(checkbox != nullptr);
    QVERIFY(!checkbox->isChecked());
    QCOMPARE(checkbox->text(), QStringLiteral("Include drawings"));
}

void TelestrationTest::videoPictureUsesGraphicsViewSink() {
    VideoPlayer player;
    auto* view = qobject_cast<QGraphicsView*>(player.videoFrame());
    QVERIFY(view != nullptr);
    QCOMPARE(view->objectName(), QStringLiteral("VideoSurface"));
    QVERIFY(view->scene() != nullptr);

    QGraphicsVideoItem* videoItem = nullptr;
    const QList<QGraphicsItem*> items = view->scene()->items();
    for (QGraphicsItem* item : items) {
        videoItem = qgraphicsitem_cast<QGraphicsVideoItem*>(item);
        if (videoItem != nullptr) break;
    }
    QVERIFY(videoItem != nullptr);
    QVERIFY(videoItem->videoSink() != nullptr);
    QCOMPARE(player.videoSink(), videoItem->videoSink());

    auto* toolbar = view->viewport()->findChild<QWidget*>(QStringLiteral("TelestrationToolBar"));
    QVERIFY(toolbar != nullptr);
}

int runTelestrationTests(int argc, char** argv) {
    TelestrationTest tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "TelestrationTests.moc"
