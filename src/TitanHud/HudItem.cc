#include "HudItem.h"

#include <algorithm>
#include <cmath>

#include <QtConcurrent/QtConcurrentRun>
#include <QtCore/QtMath>
#include <QtGui/QPainter>
#include <QtQuick/QQuickWindow>
#include <QtQuick/QSGSimpleTextureNode>

#include "BatteryFactGroupListModel.h"
#include "Fact.h"
#include "FactGroup.h"
#include "FactMetaData.h"
#include "QmlObjectListModel.h"
#include "VehicleGPSFactGroup.h"
#include "VehicleLinkManager.h"

namespace {
/// At most about 60 frames a second
constexpr int kMinPaintIntervalMs = 16;
/// The speed and altitude tapes show this much either side of the value
constexpr double kSpeedTapeHalfRange = 12.0;     ///< m/s
constexpr double kAltitudeTapeHalfRange = 15.0;  ///< m
/// How far ahead the speed and altitude trend vectors reach
constexpr double kTrendSeconds = 6.0;
/// Below this ground speed the flight path is mostly GPS noise
constexpr double kFlightPathMinSpeed = 2.0;  ///< m/s
constexpr double kMinTimeToGoSpeed = 1.0;    ///< m/s

double wrap180(double degrees)
{
    double wrapped = std::fmod(degrees + 180.0, 360.0);
    if (wrapped < 0.0) {
        wrapped += 360.0;
    }
    return wrapped - 180.0;
}

double factValue(Fact* fact)
{
    return fact ? fact->rawValue().toDouble() : qQNaN();
}

double groupValue(FactGroup* group, const char* name)
{
    if (!group) {
        return qQNaN();
    }
    Fact* const fact = group->getFact(QString::fromLatin1(name));
    return factValue(fact);
}

HudFrame::Battery batteryValues(QmlObjectListModel* batteries, int index)
{
    HudFrame::Battery values;
    if (!batteries || (batteries->count() <= index)) {
        return values;
    }
    auto* const battery = qobject_cast<BatteryFactGroup*>(batteries->get(index));
    if (!battery) {
        return values;
    }
    values.voltage = factValue(battery->voltage());
    values.current = factValue(battery->current());
    values.remaining = factValue(battery->percentRemaining());
    values.chargeState = battery->chargeState()->rawValue().toInt();
    return values;
}
}  // namespace

HudItem::HudItem(QQuickItem* parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
    HudPainter::registerFont();
    _clock.start();
    _painter.setMaxThreadCount(1);
    _paintInterval.setSingleShot(true);
    _paintInterval.setInterval(kMinPaintIntervalMs);
    (void) connect(&_paintInterval, &QTimer::timeout, this, [this]() {
        if (_paintPending) {
            _requestPaint();
        }
    });
}

HudItem::~HudItem()
{
    // The painting thread uses this item's painter
    _painter.clear();
    _painter.waitForDone();
}

void HudItem::setHudData(HudData* data)
{
    if (_data == data) {
        return;
    }
    if (_data) {
        (void) disconnect(_data, nullptr, this, nullptr);
    }
    _data = data;
    if (_data) {
        (void) connect(_data, &HudData::dataChanged, this, &HudItem::_requestPaint);
        (void) connect(_data, &HudData::vehicleChanged, this,
                       [this]() { _connectVehicle(_data ? _data->vehicle() : nullptr); });
    }
    _connectVehicle(_data ? _data->vehicle() : nullptr);
    emit hudDataChanged();
    _requestPaint();
}

void HudItem::setDrawBackground(bool draw)
{
    if (_drawBackground == draw) {
        return;
    }
    _drawBackground = draw;
    emit drawBackgroundChanged();
    _requestPaint();
}

void HudItem::_connectVehicle(Vehicle* vehicle)
{
    if (_connectedVehicle) {
        for (Fact* fact : {_connectedVehicle->roll(), _connectedVehicle->pitch(), _connectedVehicle->heading()}) {
            (void) disconnect(fact, nullptr, this, nullptr);
        }
    }
    _connectedVehicle = vehicle;
    if (_connectedVehicle) {
        // Attitude arrives faster than the data tick; repaint on it so the horizon never lags
        for (Fact* fact : {_connectedVehicle->roll(), _connectedVehicle->pitch(), _connectedVehicle->heading()}) {
            (void) connect(fact, &Fact::rawValueChanged, this, &HudItem::_requestPaint);
        }
    }
    _requestPaint();
}

HudFrame HudItem::_collectFrame() const
{
    HudFrame f;
    f.width = width();
    f.height = height();
    f.drawBackground = _drawBackground;
    f.clockMs = _clock.elapsed();
    f.speedUnits = FactMetaData::appSettingsSpeedUnitsString();
    f.altitudeUnits = FactMetaData::appSettingsVerticalDistanceUnitsString();
    f.distanceUnits = FactMetaData::appSettingsHorizontalDistanceUnitsString();
    f.speedRange = FactMetaData::metersSecondToAppSettingsSpeedUnits(kSpeedTapeHalfRange).toDouble();
    f.altitudeRange = FactMetaData::metersToAppSettingsVerticalDistanceUnits(kAltitudeTapeHalfRange).toDouble();

    Vehicle* const vehicle = _data ? _data->vehicle() : nullptr;
    if (!vehicle) {
        return f;
    }

    // To the user's units, keeping NaN for values not reported yet
    auto speed = [](double metersSecond) {
        return std::isnan(metersSecond) ? qQNaN()
                                        : FactMetaData::metersSecondToAppSettingsSpeedUnits(metersSecond).toDouble();
    };
    auto vertical = [](double meters) {
        return std::isnan(meters) ? qQNaN() : FactMetaData::metersToAppSettingsVerticalDistanceUnits(meters).toDouble();
    };
    auto horizontal = [](double meters) {
        return std::isnan(meters) ? qQNaN()
                                  : FactMetaData::metersToAppSettingsHorizontalDistanceUnits(meters).toDouble();
    };

    f.hasVehicle = true;
    f.connected = !vehicle->vehicleLinkManager()->communicationLost();
    f.armed = vehicle->armed();
    f.recentlyArmed = _data->recentlyArmed();
    f.failsafe = _data->failsafe();
    f.safetyActive = _data->safetyActive();
    f.prearmAvailable = vehicle->readyToFlyAvailable();
    f.prearmOk = vehicle->readyToFly();
    f.fixedWing = vehicle->fixedWing() || vehicle->vtol();

    f.roll = factValue(vehicle->roll());
    f.pitch = factValue(vehicle->pitch());
    f.heading = factValue(vehicle->heading());
    const double groundSpeed = factValue(vehicle->groundSpeed());
    const double climbRate = factValue(vehicle->climbRate());
    if (groundSpeed > kFlightPathMinSpeed) {
        f.flightPathValid = true;
        f.flightPathAngle = qRadiansToDegrees(std::atan2(std::isnan(climbRate) ? 0.0 : climbRate, groundSpeed));
        const double course = groupValue(vehicle->gpsFactGroup(), "courseOverGround");
        if (!std::isnan(course) && !std::isnan(f.heading)) {
            f.groundTrack = course;
            f.drift = wrap180(course - f.heading);
        }
    }
    f.turnRate = _data->turnRate();
    f.targetHeading = _data->navBearing();

    f.airspeed = speed(factValue(vehicle->airSpeed()));
    f.groundspeed = speed(groundSpeed);
    f.speedTrend = speed(_data->airspeedAccel() * kTrendSeconds);
    f.targetSpeed = speed(_data->targetAirspeed());
    f.verticalSpeed = speed(climbRate);
    f.aoa = _data->angleOfAttack();

    const double altitude = factValue(vehicle->altitudeRelative());
    f.altitude = vertical(altitude);
    f.altitudeTrend = vertical(std::isnan(climbRate) ? 0.0 : climbRate * kTrendSeconds);
    f.targetAltitude = vertical(_data->targetAltitude());
    f.homeSet = vehicle->homePosition().isValid();

    f.xtrack = _data->xtrackError();
    f.distanceToHome = horizontal(factValue(vehicle->distanceToHome()));
    f.headingToHome = factValue(vehicle->headingToHome());
    const double waypoint = factValue(vehicle->missionItemIndex());
    f.wpNumber = std::isnan(waypoint) ? 0 : static_cast<int>(waypoint);
    f.wpDistance = horizontal(_data->wpDistance());
    f.wpBearing = std::isnan(_data->targetBearing()) ? factValue(vehicle->headingToNextWP()) : _data->targetBearing();
    if (!std::isnan(_data->wpDistance()) && (groundSpeed > kMinTimeToGoSpeed)) {
        f.wpTimeToGo = _data->wpDistance() / groundSpeed;
    }
    f.mode = vehicle->flightMode();
    f.windDirection = groupValue(vehicle->windFactGroup(), "direction");
    f.windSpeed = speed(groupValue(vehicle->windFactGroup(), "speed"));

    f.battery = batteryValues(vehicle->batteries(), 0);
    f.battery2 = batteryValues(vehicle->batteries(), 1);
    const double fix = groupValue(vehicle->gpsFactGroup(), "lock");
    f.gpsFix = std::isnan(fix) ? -1 : static_cast<int>(fix);
    const double satellites = groupValue(vehicle->gpsFactGroup(), "count");
    f.gpsSatellites = std::isnan(satellites) ? -1 : static_cast<int>(satellites);
    const double fix2 = groupValue(vehicle->gps2FactGroup(), "lock");
    f.gps2Fix = std::isnan(fix2) ? -1 : static_cast<int>(fix2);
    FactGroup* const vibration = vehicle->vibrationFactGroup();
    for (const char* axis : {"xAxis", "yAxis", "zAxis"}) {
        const double value = groupValue(vibration, axis);
        if (!std::isnan(value)) {
            f.vibration = std::isnan(f.vibration) ? value : std::max(f.vibration, value);
        }
    }
    f.ekfStatus = _data->ekfStatus();
    f.linkQuality = f.connected ? std::clamp(100.0 - vehicle->mavlinkLossPercent(), 0.0, 100.0) : 0.0;
    f.message = _data->message();
    f.messageSeverity = _data->messageSeverity();

    return f;
}

void HudItem::_requestPaint()
{
    _paintPending = true;
    if (_painting || _paintInterval.isActive() || !window() || !isVisible() || (width() < 1.0) || (height() < 1.0)) {
        // Picked up when the running frame finishes, the interval ends, or the item is shown or resized
        return;
    }
    _paintPending = false;
    _painting = true;
    _paintInterval.start();

    const HudFrame f = _collectFrame();
    const qreal devicePixelRatio = window()->effectiveDevicePixelRatio();
    (void) QtConcurrent::run(&_painter, [this, f, devicePixelRatio]() {
        QImage image((QSizeF(f.width, f.height) * devicePixelRatio).toSize(), QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(devicePixelRatio);
        image.fill(Qt::transparent);
        {
            QPainter painter(&image);
            _hudPainter.paint(painter, f);
        }
        return image;
    }).then(this, [this](const QImage& image) {
        _image = image;
        _imageChanged = true;
        _painting = false;
        update();
        if (_paintPending) {
            _requestPaint();
        }
    });
}

void HudItem::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        _requestPaint();
    }
}

void HudItem::itemChange(ItemChange change, const ItemChangeData& value)
{
    QQuickItem::itemChange(change, value);
    if ((change == ItemSceneChange) || (change == ItemVisibleHasChanged) ||
        (change == ItemDevicePixelRatioHasChanged)) {
        _requestPaint();
    }
}

QSGNode* HudItem::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*)
{
    auto* node = static_cast<QSGSimpleTextureNode*>(oldNode);
    if (_image.isNull()) {
        delete node;
        return nullptr;
    }
    if (!node) {
        node = new QSGSimpleTextureNode();
        node->setOwnsTexture(true);
        node->setFiltering(QSGTexture::Linear);
        _imageChanged = true;
    }
    if (_imageChanged) {
        node->setTexture(window()->createTextureFromImage(_image, QQuickWindow::TextureHasAlphaChannel));
        _imageChanged = false;
    }
    // Stretched over a resize until the frame for the new size is in
    node->setRect(boundingRect());
    return node;
}
