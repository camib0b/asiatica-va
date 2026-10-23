#pragma once

#include "../state/Telestration.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QVector>

#include <functional>
#include <optional>

class DrawingGraphicsItem;
class QEvent;
class QGraphicsVideoItem;
class QGraphicsView;
class QMediaPlayer;
class TelestrationToolBar;

/// Drawings on the video scene, the tool picker, and stroke input.
/// Playback, scrubbing, and the controls bar stay on VideoPlayer.
class TelestrationSurface final : public QObject {
    Q_OBJECT

public:
    explicit TelestrationSurface(QObject* parent = nullptr);

    void attach(QGraphicsView* view, QGraphicsVideoItem* videoItem, QMediaPlayer* player);
    void setSession(Telestration::Session* session);
    void setTagResolver(std::function<std::optional<quint64>(qint64 positionMs)> resolver);
    void setPresentationMode(bool presenting);
    void setForcedVisibleDrawingId(quint64 drawingId);
    void setToolsEnabled(bool enabled);
    void setToolsVisible(bool visible);
    void relayout();

    /// True when the event starts or continues a stroke and must not toggle playback.
    bool handleViewportEvent(QObject* watched, QEvent* event);

private:
    void rebuildItems();
    void refreshVisibility();
    void updateToolCursor();
    void positionToolBar();
    QRectF videoSceneRectangle() const;
    std::optional<Telestration::Point> normalizedPointInPicture(const QPoint& viewportPosition,
                                                                bool* insidePicture) const;
    void beginStroke(const Telestration::Point& point);
    void extendStroke(const Telestration::Point& point);
    void finishStroke();
    void cancelStroke();
    Telestration::Shape shapeFromStroke() const;
    bool strokeIsLongEnough() const;

    QPointer<QGraphicsView> view_;
    QPointer<QGraphicsVideoItem> videoItem_;
    QPointer<QMediaPlayer> player_;
    QPointer<Telestration::Session> session_;
    QPointer<TelestrationToolBar> toolBar_;
    QMetaObject::Connection sessionConnection_;
    std::function<std::optional<quint64>(qint64 positionMs)> tagResolver_;
    QHash<quint64, DrawingGraphicsItem*> drawingItems_;
    DrawingGraphicsItem* previewItem_ = nullptr;
    bool presentationMode_ = false;
    quint64 forcedDrawingId_ = 0;
    bool strokeActive_ = false;
    QVector<Telestration::Point> strokePoints_;
};
