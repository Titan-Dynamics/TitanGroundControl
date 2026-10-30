#pragma once

#include <memory>

#include <QtCore/QHash>
#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <QtCore/QVariantMap>
#include <QtPositioning/QGeoCoordinate>
#include <QtQmlIntegration/QtQmlIntegration>
#include <QtQuick/QQuickRhiItem>

#include "Map3DTypes.h"

class Fact;
class HudData;
class Vehicle;

namespace Map3D {
class TileLoader;
}

/// The Titan 3D map: Mission Planner's Map3D view (TitanPlanner) of the vehicle and mission over streamed
/// satellite terrain. This item gathers telemetry, the mission, ADS-B traffic and settings on the GUI thread;
/// Map3DRenderer draws them on the render thread. Frames run at full rate while anything changes and drop to one a
/// second when idle, as in Mission Planner.
class Map3DItem : public QQuickRhiItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_MOC_INCLUDE("Vehicle.h")
    Q_PROPERTY(Vehicle* vehicle READ vehicle WRITE setVehicle NOTIFY vehicleChanged)
    Q_PROPERTY(QGeoCoordinate map2DCenter READ map2DCenter WRITE setMap2DCenter NOTIFY map2DCenterChanged)
    Q_PROPERTY(double freeLookYaw READ freeLookYaw WRITE setFreeLookYaw NOTIFY freeLookChanged)
    Q_PROPERTY(double freeLookPitch READ freeLookPitch WRITE setFreeLookPitch NOTIFY freeLookChanged)
    Q_PROPERTY(double cameraDistance READ cameraDistance WRITE setCameraDistance NOTIFY cameraChanged)
    Q_PROPERTY(double cameraHeight READ cameraHeight WRITE setCameraHeight NOTIFY cameraChanged)
    Q_PROPERTY(double cameraAngle READ cameraAngle WRITE setCameraAngle NOTIFY cameraChanged)
    Q_PROPERTY(bool vehicleConnected READ vehicleConnected NOTIFY vehicleConnectedChanged)
    Q_PROPERTY(QVariantMap stats READ stats NOTIFY statsChanged)
    Q_PROPERTY(QString adsbTooltip READ adsbTooltip NOTIFY adsbTooltipChanged)

public:
    explicit Map3DItem(QQuickItem* parent = nullptr);
    ~Map3DItem() override;

    Vehicle* vehicle() const;

    void setVehicle(Vehicle* vehicle);

    QGeoCoordinate map2DCenter() const { return _map2DCenter; }

    void setMap2DCenter(const QGeoCoordinate& center);

    double freeLookYaw() const { return _freeLookYaw; }

    void setFreeLookYaw(double yaw);

    double freeLookPitch() const { return _freeLookPitch; }

    void setFreeLookPitch(double pitch);

    /// The chase camera's placement. Mouse adjustments are kept here and reach the settings a second after they
    /// stop, as Mission Planner saves them when its map closes: every settings write rewrites the settings file.
    double cameraDistance() const { return _cameraDistance; }

    void setCameraDistance(double distance);

    double cameraHeight() const { return _cameraHeight; }

    void setCameraHeight(double height);

    double cameraAngle() const { return _cameraAngle; }

    void setCameraAngle(double angle);

    bool vehicleConnected() const { return _input.connected; }

    QVariantMap stats() const { return _output.stats; }

    QString adsbTooltip() const { return _adsbTooltip; }

    /// Keeps the full frame rate for a moment, e.g. after input.
    Q_INVOKABLE void markActive();
    /// Updates adsbTooltip for the aircraft under a point in item coordinates, if any.
    Q_INVOKABLE void hover(qreal x, qreal y);
    /// Clears the hover state.
    Q_INVOKABLE void hoverExited();

    // For Map3DRenderer::synchronize(), which runs while the GUI thread is blocked
    const Map3D::FrameInput& frameInput() const { return _input; }

    std::shared_ptr<Map3D::TileLoader> tileLoader() const { return _loader; }

    void setFrameOutput(const Map3D::FrameOutput& output);

signals:
    void vehicleChanged();
    void map2DCenterChanged();
    void freeLookChanged();
    void cameraChanged();
    void vehicleConnectedChanged();
    void statsChanged();
    void adsbTooltipChanged();

protected:
    QQuickRhiItemRenderer* createRenderer() override;
    void itemChange(ItemChange change, const ItemChangeData& value) override;

private:
    void _tick();
    void _onCoordinateChanged();
    void _setCamera(double& value, double newValue);
    void _followCameraSetting(Fact* fact, double& value);
    void _saveCamera();
    void _refreshInput();
    void _refreshMission();
    void _refreshAdsb();
    bool _needsFrame() const;
    QString _imageryMapType() const;
    QString _modelPath() const;
    QString _tooltipFor(quint32 icao, double distance) const;

    QPointer<Vehicle> _vehicle;
    HudData* _hudData = nullptr;
    QGeoCoordinate _map2DCenter;
    QGeoCoordinate _lastSeenMap2D;
    double _freeLookYaw = 0;
    double _freeLookPitch = 0;
    double _cameraDistance = 0;
    double _cameraHeight = 0;
    double _cameraAngle = 0;
    QTimer _saveCameraTimer;

    Map3D::FrameInput _input;
    Map3D::FrameOutput _output;
    std::shared_ptr<Map3D::TileLoader> _loader;
    QString _adsbTooltip;
    quint32 _hoveredIcao = 0;
    QPointF _hoverPoint;
    bool _hovering = false;

    QTimer _timer;
    double _lastActivity = 0;
    double _lastFrame = 0;

    struct AdsbTrack
    {
        QGeoCoordinate coordinate;
        double timeSeconds = 0;
    };

    QHash<quint32, AdsbTrack> _adsbTracks;
};
