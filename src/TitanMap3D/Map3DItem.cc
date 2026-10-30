#include "Map3DItem.h"

#include <cmath>

#include <QtCore/QFileInfo>
#include <QtQuick/QQuickWindow>

#include "ADSBVehicle.h"
#include "ADSBVehicleManager.h"
#include "FactMetaData.h"
#include "FlightMapSettings.h"
#include "HudData.h"
#include "Map3DModel.h"
#include "Map3DRenderer.h"
#include "Map3DSettings.h"
#include "Map3DTileLoader.h"
#include "MissionItem.h"
#include "MissionManager.h"
#include "ParameterManager.h"
#include "QGCMapUrlEngine.h"
#include "QmlObjectListModel.h"
#include "SettingsManager.h"
#include "Vehicle.h"
#include "VehicleGPSFactGroup.h"
#include "VehicleLocalPositionFactGroup.h"

using namespace Map3D;

namespace {

constexpr int kTimerIntervalMs = 12;
constexpr double kActiveLingerSeconds = 2;
constexpr double kIdleIntervalSeconds = 1;
constexpr int kSaveCameraDelayMs = 1000;

double factValue(Fact* fact, double fallback = 0)
{
    if (!fact) {
        return fallback;
    }
    const double value = fact->rawValue().toDouble();
    return std::isnan(value) ? fallback : value;
}

double rounded2(double value)
{
    return std::round(value * 100.0) / 100.0;
}

QColor lightened(const QColor& color)
{
    // The HUD's sky colours lightened, as Mission Planner's 3D map takes them
    return QColor::fromRgbF(color.redF() * 0.5f + 0.5f, color.greenF() * 0.5f + 0.5f, color.blueF() * 0.5f + 0.5f);
}

}  // namespace

Map3DItem::Map3DItem(QQuickItem* parent)
    : QQuickRhiItem(parent)
    , _hudData(new HudData(this))
{
    setSampleCount(4);
    _input.skyTop = lightened(QColor(0x4A, 0x90, 0xC9));
    _input.skyHorizon = lightened(QColor(0x9D, 0xC4, 0xDB));
    _lastActivity = nowSeconds();

    Map3DSettings* const settings = SettingsManager::instance()->map3DSettings();
    _followCameraSetting(settings->cameraDistance(), _cameraDistance);
    _followCameraSetting(settings->cameraHeight(), _cameraHeight);
    _followCameraSetting(settings->cameraAngle(), _cameraAngle);
    _saveCameraTimer.setSingleShot(true);
    _saveCameraTimer.setInterval(kSaveCameraDelayMs);
    (void) connect(&_saveCameraTimer, &QTimer::timeout, this, &Map3DItem::_saveCamera);

    _timer.setInterval(kTimerIntervalMs);
    (void) connect(&_timer, &QTimer::timeout, this, &Map3DItem::_tick);
    _refreshInput();
    _timer.start();
}

Map3DItem::~Map3DItem()
{
    if (_saveCameraTimer.isActive()) {
        _saveCamera();
    }
}

QQuickRhiItemRenderer* Map3DItem::createRenderer()
{
    return new Map3DRenderer();
}

void Map3DItem::itemChange(ItemChange change, const ItemChangeData& value)
{
    QQuickRhiItem::itemChange(change, value);
    if (change == ItemVisibleHasChanged) {
        if (value.boolValue) {
            markActive();
            _timer.start();
        } else {
            _timer.stop();
        }
    }
}

Vehicle* Map3DItem::vehicle() const
{
    return _vehicle;
}

void Map3DItem::setVehicle(Vehicle* vehicle)
{
    if (_vehicle == vehicle) {
        return;
    }
    if (_vehicle) {
        (void) disconnect(_vehicle, nullptr, this, nullptr);
    }
    _vehicle = vehicle;
    _hudData->setVehicle(vehicle);
    if (_vehicle) {
        (void) connect(_vehicle, &Vehicle::coordinateChanged, this, &Map3DItem::_onCoordinateChanged);
        _onCoordinateChanged();
    }
    _refreshInput();
    markActive();
    emit vehicleChanged();
}

void Map3DItem::setMap2DCenter(const QGeoCoordinate& center)
{
    if (_map2DCenter == center) {
        return;
    }
    _map2DCenter = center;
    emit map2DCenterChanged();
}

void Map3DItem::setFreeLookYaw(double yaw)
{
    if (_freeLookYaw != yaw) {
        _freeLookYaw = yaw;
        emit freeLookChanged();
    }
}

void Map3DItem::setFreeLookPitch(double pitch)
{
    if (_freeLookPitch != pitch) {
        _freeLookPitch = pitch;
        emit freeLookChanged();
    }
}

void Map3DItem::setCameraDistance(double distance)
{
    _setCamera(_cameraDistance, distance);
}

void Map3DItem::setCameraHeight(double height)
{
    _setCamera(_cameraHeight, height);
}

void Map3DItem::setCameraAngle(double angle)
{
    _setCamera(_cameraAngle, angle);
}

void Map3DItem::_setCamera(double& value, double newValue)
{
    if (value != newValue) {
        value = newValue;
        _saveCameraTimer.start();
        emit cameraChanged();
    }
}

void Map3DItem::_followCameraSetting(Fact* fact, double& value)
{
    // Edits from the settings dialog or page
    value = fact->rawValue().toDouble();
    (void) connect(fact, &Fact::rawValueChanged, this, [this, &value](const QVariant& rawValue) {
        if (value != rawValue.toDouble()) {
            value = rawValue.toDouble();
            emit cameraChanged();
        }
    });
}

void Map3DItem::_saveCamera()
{
    _saveCameraTimer.stop();
    Map3DSettings* const settings = SettingsManager::instance()->map3DSettings();
    settings->cameraDistance()->setRawValue(_cameraDistance);
    settings->cameraHeight()->setRawValue(_cameraHeight);
    settings->cameraAngle()->setRawValue(_cameraAngle);
}

void Map3DItem::markActive()
{
    _lastActivity = nowSeconds();
    update();
}

void Map3DItem::_onCoordinateChanged()
{
    if (!_vehicle) {
        return;
    }
    // A fix and its time, the base the renderer dead-reckons from
    const QGeoCoordinate coordinate = _vehicle->coordinate();
    _input.fix.lat = coordinate.latitude();
    _input.fix.lng = coordinate.longitude();
    _input.fix.timeSeconds = nowSeconds();

    VehicleLocalPositionFactGroup* const local =
        qobject_cast<VehicleLocalPositionFactGroup*>(_vehicle->localPositionFactGroup());
    double north = local ? factValue(local->vx(), qQNaN()) : qQNaN();
    double east = local ? factValue(local->vy(), qQNaN()) : qQNaN();
    double down = local ? factValue(local->vz(), qQNaN()) : qQNaN();
    if (std::isnan(north) || std::isnan(east)) {
        // No local position: from ground speed and course over ground
        const double speed = factValue(_vehicle->groundSpeed());
        const double course = qDegreesToRadians(factValue(
            _vehicle->gpsFactGroup()->getFact(QStringLiteral("courseOverGround")), factValue(_vehicle->heading())));
        north = speed * std::cos(course);
        east = speed * std::sin(course);
    }
    if (std::isnan(down)) {
        down = -factValue(_vehicle->climbRate());
    }
    _input.fix.velocityNorth = north;
    _input.fix.velocityEast = east;
    _input.fix.velocityDown = down;
}

void Map3DItem::_tick()
{
    if (!isVisible()) {
        return;
    }
    if (_map2DCenter.isValid() && (_map2DCenter != _lastSeenMap2D)) {
        _lastSeenMap2D = _map2DCenter;
        _lastActivity = nowSeconds();
    }
    if (_needsFrame()) {
        _refreshInput();
        _lastFrame = nowSeconds();
        update();
    }
}

bool Map3DItem::_needsFrame() const
{
    // Full frame rate while anything changes: a connected vehicle, terrain streaming, spinning markers, ADS-B, the
    // stats panel or recent input. Otherwise one frame a second, which still picks up arriving data.
    const double now = nowSeconds();
    const bool animating = _input.connected || _input.statsForNerds || _output.terrainBusy ||
                           (_output.markersDrawn > 0) || !_output.adsb.isEmpty();
    return animating || (now - _lastActivity < kActiveLingerSeconds) || (now - _lastFrame >= kIdleIntervalSeconds);
}

QString Map3DItem::_imageryMapType() const
{
    // The 2D map's imagery when it is a satellite map, so both share cached tiles; satellite otherwise
    FlightMapSettings* const settings = SettingsManager::instance()->flightMapSettings();
    const QString provider = settings->mapProvider()->rawValue().toString();
    const QString type = settings->mapType()->rawValue().toString();
    const QString current = provider + QLatin1Char(' ') + type;
    if (type.contains(QLatin1String("Satellite")) && (UrlFactory::getQtMapIdFromProviderType(current) >= 0)) {
        return current;
    }
    const QString satellite = provider + QStringLiteral(" Satellite");
    if (UrlFactory::getQtMapIdFromProviderType(satellite) >= 0) {
        return satellite;
    }
    return QStringLiteral("Google Satellite");
}

QString Map3DItem::_modelPath() const
{
    const QString custom = SettingsManager::instance()->map3DSettings()->stlPath()->rawValue().toString();
    if (!custom.isEmpty() && QFileInfo::exists(custom)) {
        return custom;
    }
    if (!_vehicle) {
        return Model::defaultModelPath(MAV_TYPE_FIXED_WING, false);
    }
    bool quadPlane = false;
    if (_vehicle->apmFirmware() && (_vehicle->vehicleType() == MAV_TYPE_FIXED_WING)) {
        // Some QuadPlanes report as fixed wing; Mission Planner checks Q_ENABLE
        ParameterManager* const parameters = _vehicle->parameterManager();
        if (parameters &&
            parameters->parameterExists(ParameterManager::defaultComponentId, QStringLiteral("Q_ENABLE"))) {
            quadPlane = parameters->getParameter(ParameterManager::defaultComponentId, QStringLiteral("Q_ENABLE"))
                            ->rawValue()
                            .toInt() == 1;
        }
    }
    return Model::defaultModelPath(_vehicle->vehicleType(), quadPlane);
}

void Map3DItem::_refreshInput()
{
    Map3DSettings* const settings = SettingsManager::instance()->map3DSettings();
    _input.cameraDistance = _cameraDistance;
    _input.cameraHeight = _cameraHeight;
    _input.cameraAngle = _cameraAngle;
    _input.fieldOfView = settings->fieldOfView()->rawValue().toDouble();
    _input.vehicleScale = settings->vehicleScale()->rawValue().toDouble();
    _input.vehicleColor = QColor(settings->vehicleColor()->rawValue().toString());
    _input.waypointMarkerSize = settings->waypointMarkerSize()->rawValue().toDouble();
    _input.adsbCircleSize = settings->adsbCircleSize()->rawValue().toDouble();
    _input.cloudDensity = settings->cloudDensity()->rawValue().toInt();
    _input.showHeadingLine = settings->showHeadingLine()->rawValue().toBool();
    _input.showNavBearingLine = settings->showNavBearingLine()->rawValue().toBool();
    _input.showGpsHeadingLine = settings->showGpsHeadingLine()->rawValue().toBool();
    _input.showTurnRadius = settings->showTurnRadius()->rawValue().toBool();
    _input.showTrail = settings->showTrail()->rawValue().toBool();
    _input.fpvMode = settings->fpvMode()->rawValue().toBool();
    _input.smoothMovement = settings->smoothMovement()->rawValue().toBool();
    _input.statsForNerds = settings->statsForNerds()->rawValue().toBool();
    _input.modelPath = _modelPath();
    _input.devicePixelRatio = window() ? window()->effectiveDevicePixelRatio() : 1.0;
    _input.freeLookYaw = _freeLookYaw;
    _input.freeLookPitch = _freeLookPitch;
    _input.hasMap2DCenter = _map2DCenter.isValid();
    _input.map2DLat = _map2DCenter.latitude();
    _input.map2DLng = _map2DCenter.longitude();

    const QString imagery = _imageryMapType();
    if (!_loader || (_input.imageryMapType != imagery)) {
        _input.imageryMapType = imagery;
        // Deleted on its own (GUI) thread once the renderer lets go of it too
        _loader =
            std::shared_ptr<TileLoader>(new TileLoader(imagery), [](TileLoader* loader) { loader->deleteLater(); });
    }

    const bool wasConnected = _input.connected;
    const QGeoCoordinate coordinate = _vehicle ? _vehicle->coordinate() : QGeoCoordinate();
    _input.connected =
        _vehicle && coordinate.isValid() && ((coordinate.latitude() != 0) || (coordinate.longitude() != 0));
    if (_input.connected) {
        _input.armed = _vehicle->armed();
        _input.roll = rounded2(factValue(_vehicle->roll()));
        _input.pitch = rounded2(factValue(_vehicle->pitch()));
        _input.yaw = rounded2(factValue(_vehicle->heading()));
        _input.relativeAltitude = factValue(_vehicle->altitudeRelative());
        _input.groundSpeed = factValue(_vehicle->groundSpeed());
        _input.groundCourse =
            factValue(_vehicle->gpsFactGroup()->getFact(QStringLiteral("courseOverGround")), _input.yaw);
        const double navBearing = _hudData->navBearing();
        _input.navBearing = std::isnan(navBearing) ? 0 : navBearing;
        // Mission Planner's turn radius: level-turn radius from ground speed and bank
        _input.turnRadius = (_input.groundSpeed <= 1) ? 0
                                                      : _input.groundSpeed * _input.groundSpeed /
                                                            (9.80665 * std::tan(qDegreesToRadians(_input.roll)));
    }
    if (wasConnected != _input.connected) {
        emit vehicleConnectedChanged();
    }

    _refreshMission();
    _refreshAdsb();
}

void Map3DItem::_refreshMission()
{
    _input.waypoints.clear();
    _input.hasGuidedTarget = false;
    _input.navMode = NavMode::Other;
    if (!_vehicle) {
        return;
    }

    // Index = mission sequence number: home at 0, then the items, None where an item has no position
    MissionPoint home;
    const QGeoCoordinate homePosition = _vehicle->homePosition();
    if (homePosition.isValid() && ((homePosition.latitude() != 0) || (homePosition.longitude() != 0))) {
        home.kind = MissionPoint::Kind::Home;
        home.lat = homePosition.latitude();
        home.lng = homePosition.longitude();
    }
    _input.waypoints.append(home);

    const QList<MissionItem*>& items = _vehicle->missionManager()->missionItems();
    for (const MissionItem* item : items) {
        if (item->sequenceNumber() == 0) {
            continue;
        }
        while (_input.waypoints.size() < item->sequenceNumber()) {
            _input.waypoints.append(MissionPoint{});
        }
        MissionPoint point;
        const int command = item->command();
        // Positions of navigable items, as Mission Planner's 2D map draws them
        const bool navigable = ((command < 255) && (command != MAV_CMD_NAV_RETURN_TO_LAUNCH) &&
                                (command != MAV_CMD_NAV_CONTINUE_AND_CHANGE_ALT) && (command != MAV_CMD_NAV_DELAY) &&
                                (command != MAV_CMD_NAV_GUIDED_ENABLE)) ||
                               (command == MAV_CMD_DO_SET_ROI) || (command == MAV_CMD_DO_LAND_START);
        const double lat = item->param5();
        const double lng = item->param6();
        if (navigable && ((lat != 0) || (lng != 0))) {
            point.kind = (command == MAV_CMD_DO_SET_ROI) ? MissionPoint::Kind::Roi : MissionPoint::Kind::Waypoint;
            point.lat = lat;
            point.lng = lng;
            point.alt = item->param7();
        }
        _input.waypoints.append(point);
    }

    const QString mode = _vehicle->flightMode();
    if (mode == _vehicle->missionFlightMode()) {
        _input.navMode = NavMode::Auto;
    } else if ((mode == _vehicle->rtlFlightMode()) || (mode == _vehicle->smartRTLFlightMode()) ||
               (mode == _vehicle->landFlightMode())) {
        _input.navMode = NavMode::Return;
    } else if (mode == _vehicle->gotoFlightMode()) {
        _input.navMode = NavMode::Guided;
        const QGeoCoordinate target = _vehicle->lastGotoCoordinate();
        if (target.isValid()) {
            // Guided goto holds the vehicle's altitude above home
            _input.hasGuidedTarget = true;
            _input.guidedTarget.kind = MissionPoint::Kind::Guided;
            _input.guidedTarget.lat = target.latitude();
            _input.guidedTarget.lng = target.longitude();
            _input.guidedTarget.alt = _input.relativeAltitude;
        }
    }
    _input.currentWaypoint = static_cast<int>(factValue(_vehicle->missionItemIndex()));
}

void Map3DItem::_refreshAdsb()
{
    _input.adsb.clear();
    const QmlObjectListModel* const vehicles = ADSBVehicleManager::instance()->adsbVehicles();
    const double now = nowSeconds();
    QSet<quint32> seen;
    for (int i = 0; i < vehicles->count(); i++) {
        const ADSBVehicle* const adsb = vehicles->value<const ADSBVehicle*>(i);
        if (!adsb || !adsb->coordinate().isValid()) {
            continue;
        }
        // The time of each report, so traffic can be carried forward between them
        AdsbTrack& track = _adsbTracks[adsb->icaoAddress()];
        if (track.coordinate != adsb->coordinate()) {
            track.coordinate = adsb->coordinate();
            track.timeSeconds = now;
        }
        seen.insert(adsb->icaoAddress());

        AdsbAircraft aircraft;
        aircraft.icao = adsb->icaoAddress();
        aircraft.callsign = adsb->callsign();
        aircraft.lat = adsb->coordinate().latitude();
        aircraft.lng = adsb->coordinate().longitude();
        aircraft.alt = adsb->altitude();
        aircraft.heading = adsb->heading();
        aircraft.speed = adsb->velocity();
        aircraft.squawk = adsb->squawk();
        aircraft.alert = adsb->alert();
        aircraft.timeSeconds = track.timeSeconds;
        _input.adsb.append(aircraft);
    }
    for (auto it = _adsbTracks.begin(); it != _adsbTracks.end();) {
        it = seen.contains(it.key()) ? std::next(it) : _adsbTracks.erase(it);
    }
}

void Map3DItem::setFrameOutput(const FrameOutput& output)
{
    const bool statsChangedValue = (output.stats != _output.stats);
    _output = output;
    if (statsChangedValue) {
        QMetaObject::invokeMethod(this, &Map3DItem::statsChanged, Qt::QueuedConnection);
    }
    if (_hovering) {
        QMetaObject::invokeMethod(this, [this] { hover(_hoverPoint.x(), _hoverPoint.y()); }, Qt::QueuedConnection);
    }
}

void Map3DItem::hover(qreal x, qreal y)
{
    _hovering = true;
    _hoverPoint = QPointF(x, y);
    const qreal dpr = _input.devicePixelRatio;
    const float px = static_cast<float>(x * dpr);
    const float py = static_cast<float>(y * dpr);
    QString tooltip;
    // Nearest aircraft is last
    for (qsizetype i = _output.adsb.size() - 1; i >= 0; i--) {
        const AdsbScreenPosition& position = _output.adsb[i];
        const float dx = px - position.x;
        const float dy = py - position.y;
        if (dx * dx + dy * dy <= position.radius * position.radius) {
            tooltip = _tooltipFor(position.icao, position.distance);
            break;
        }
    }
    if (tooltip != _adsbTooltip) {
        _adsbTooltip = tooltip;
        emit adsbTooltipChanged();
    }
}

void Map3DItem::hoverExited()
{
    _hovering = false;
    if (!_adsbTooltip.isEmpty()) {
        _adsbTooltip.clear();
        emit adsbTooltipChanged();
    }
}

QString Map3DItem::_tooltipFor(quint32 icao, double distance) const
{
    const AdsbAircraft* aircraft = nullptr;
    for (const AdsbAircraft& candidate : _input.adsb) {
        if (candidate.icao == icao) {
            aircraft = &candidate;
            break;
        }
    }
    if (!aircraft) {
        return {};
    }
    const QString altitudeUnits = FactMetaData::appSettingsVerticalDistanceUnitsString();
    const QString speedUnits = FactMetaData::appSettingsSpeedUnitsString();
    const QString distanceUnits = FactMetaData::appSettingsHorizontalDistanceUnitsString();
    const double altitude = FactMetaData::metersToAppSettingsVerticalDistanceUnits(aircraft->alt).toDouble();
    const double speed = FactMetaData::metersSecondToAppSettingsSpeedUnits(aircraft->speed).toDouble();
    const double ownAltitude = _vehicle ? factValue(_vehicle->altitudeAMSL()) : 0;
    const double delta = FactMetaData::metersToAppSettingsVerticalDistanceUnits(aircraft->alt - ownAltitude).toDouble();

    QStringList lines;
    lines << tr("ICAO: %1").arg(aircraft->icao, 6, 16, QLatin1Char('0')).toUpper();
    lines << tr("Callsign: %1").arg(aircraft->callsign);
    lines << tr("Squawk: %1").arg(aircraft->squawk, 4, 8, QLatin1Char('0'));
    lines << tr("Altitude: %1 %2").arg(altitude, 0, 'f', 0).arg(altitudeUnits);
    lines << tr("Speed: %1 %2").arg(speed, 0, 'f', 0).arg(speedUnits);
    lines << tr("Heading: %1°").arg(aircraft->heading, 0, 'f', 0);
    lines << (distance > 1000
                  ? tr("Distance: %1 km").arg(distance / 1000, 0, 'f', 1)
                  : tr("Distance: %1 %2")
                        .arg(FactMetaData::metersToAppSettingsHorizontalDistanceUnits(distance).toDouble(), 0, 'f', 0)
                        .arg(distanceUnits));
    lines << tr("Alt Delta: %1%2 %3")
                 .arg(delta >= 0 ? QStringLiteral("+") : QString())
                 .arg(delta, 0, 'f', 0)
                 .arg(altitudeUnits);
    if (aircraft->alert) {
        lines << tr("Collision risk: Danger");
    }
    return lines.join(QLatin1Char('\n'));
}
