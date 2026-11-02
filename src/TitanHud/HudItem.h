#pragma once

#include <QtCore/QElapsedTimer>
#include <QtCore/QPointer>
#include <QtCore/QThreadPool>
#include <QtCore/QTimer>
#include <QtGui/QColor>
#include <QtGui/QImage>
#include <QtQuick/QQuickItem>

#include "HudData.h"
#include "HudPainter.h"

/// The Titan HUD as a Qt Quick item: gathers a HudFrame from the vehicle on the GUI thread and has HudPainter paint it.
/// With drawBackground off (the video overlay) only the symbology is drawn, over the host.
///
/// Frames are painted on a thread of their own and shown once done. A QQuickPaintedItem paints during the scene graph
/// sync, holding up the whole window while QPainter works, which is slow in a Debug build.
class HudItem : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(HudData* hudData READ hudData WRITE setHudData NOTIFY hudDataChanged)
    Q_PROPERTY(bool drawBackground READ drawBackground WRITE setDrawBackground NOTIFY drawBackgroundChanged)
    Q_PROPERTY(qreal symbolScale READ symbolScale WRITE setSymbolScale NOTIFY symbolScaleChanged)
    Q_PROPERTY(QColor symbolColor READ symbolColor WRITE setSymbolColor NOTIFY symbolColorChanged)

public:
    explicit HudItem(QQuickItem* parent = nullptr);
    ~HudItem() override;

    HudData* hudData() const { return _data; }

    void setHudData(HudData* data);

    bool drawBackground() const { return _drawBackground; }

    void setDrawBackground(bool draw);

    qreal symbolScale() const { return _symbolScale; }

    void setSymbolScale(qreal scale);

    QColor symbolColor() const { return _symbolColor; }

    void setSymbolColor(const QColor& color);

protected:
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* data) override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;
    void itemChange(ItemChange change, const ItemChangeData& value) override;

signals:
    void hudDataChanged();
    void drawBackgroundChanged();
    void symbolScaleChanged();
    void symbolColorChanged();

private:
    HudFrame _collectFrame() const;
    void _requestPaint();
    void _connectVehicle(Vehicle* vehicle);

    QPointer<HudData> _data;
    QPointer<Vehicle> _connectedVehicle;
    bool _drawBackground = true;
    qreal _symbolScale = 1.0;
    QColor _symbolColor{Qt::white};
    QElapsedTimer _clock;

    HudPainter _hudPainter;  ///< Used only by the painting thread
    QThreadPool _painter;
    QTimer _paintInterval;
    QImage _image;  ///< The latest finished frame
    bool _imageChanged = false;
    bool _painting = false;
    bool _paintPending = false;
};
