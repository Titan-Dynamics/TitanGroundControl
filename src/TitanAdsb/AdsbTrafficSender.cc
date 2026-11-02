#include "AdsbTrafficSender.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "ADSBVehicle.h"
#include "ADSBVehicleManager.h"
#include "Fact.h"
#include "LinkConfiguration.h"
#include "LinkInterface.h"
#include "MAVLinkLib.h"
#include "MAVLinkProtocol.h"
#include "MultiVehicleManager.h"
#include "QmlObjectListModel.h"
#include "Vehicle.h"
#include "VehicleLinkManager.h"

AdsbTrafficSender::AdsbTrafficSender(ADSBVehicleManager* manager, Fact* enabled, QObject* parent)
    : QObject(parent)
    , _manager(manager)
    , _enabled(enabled)
{
    _timer.setInterval(kIntervalMs);
    (void) connect(&_timer, &QTimer::timeout, this, &AdsbTrafficSender::_sendNext);
    _timer.start();
}

QList<const ADSBVehicle*> AdsbTrafficSender::nearest(const QList<const ADSBVehicle*>& traffic,
                                                     const QGeoCoordinate& own)
{
    QList<QPair<double, const ADSBVehicle*>> inRange;
    for (const ADSBVehicle* const aircraft : traffic) {
        if (!aircraft || !aircraft->coordinate().isValid()) {
            continue;
        }
        const double distance = own.distanceTo(aircraft->coordinate());
        if (distance <= kRangeMeters) {
            inRange.append({distance, aircraft});
        }
    }
    std::sort(inRange.begin(), inRange.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

    QList<const ADSBVehicle*> nearestAircraft;
    for (qsizetype i = 0; i < std::min<qsizetype>(inRange.size(), kMaxAircraft); i++) {
        nearestAircraft.append(inRange.at(i).second);
    }
    return nearestAircraft;
}

void AdsbTrafficSender::_sendNext()
{
    if (!_enabled || !_enabled->rawValue().toBool()) {
        return;
    }
    Vehicle* const vehicle = MultiVehicleManager::instance()->activeVehicle();
    if (!vehicle || !vehicle->coordinate().isValid()) {
        return;
    }
    const SharedLinkInterfacePtr link = vehicle->vehicleLinkManager()->primaryLink().lock();
    if (!link || (link->linkConfiguration() && link->linkConfiguration()->isHighLatency())) {
        return;
    }

    QList<const ADSBVehicle*> traffic;
    const QmlObjectListModel* const vehicles = _manager->adsbVehicles();
    for (int i = 0; i < vehicles->count(); i++) {
        const ADSBVehicle* const aircraft = vehicles->value<const ADSBVehicle*>(i);
        if (aircraft && _manager->isLocalTraffic(aircraft->icaoAddress())) {
            traffic.append(aircraft);
        }
    }
    const QList<const ADSBVehicle*> candidates = nearest(traffic, vehicle->coordinate());
    if (candidates.isEmpty()) {
        return;
    }
    _index = (_index + 1) % static_cast<int>(candidates.size());
    const ADSBVehicle* const aircraft = candidates.at(_index);

    const QGeoCoordinate position = aircraft->coordinate();
    const bool altitudeValid = !std::isnan(aircraft->altitude());
    uint16_t flags =
        ADSB_FLAGS_VALID_COORDS | ADSB_FLAGS_VALID_HEADING | ADSB_FLAGS_VALID_VELOCITY | ADSB_FLAGS_VALID_CALLSIGN;
    if (altitudeValid) {
        flags |= ADSB_FLAGS_VALID_ALTITUDE;
    }

    // MAVLink's callsign is nine characters, the last a terminator
    char callsign[MAVLINK_MSG_ADSB_VEHICLE_FIELD_CALLSIGN_LEN] = {};
    const QByteArray latin1 = aircraft->callsign().toLatin1().left(MAVLINK_MSG_ADSB_VEHICLE_FIELD_CALLSIGN_LEN - 1);
    std::memcpy(callsign, latin1.constData(), static_cast<size_t>(latin1.size()));

    const double heading = std::fmod(std::fmod(aircraft->heading(), 360.0) + 360.0, 360.0);
    mavlink_message_t message;
    (void) mavlink_msg_adsb_vehicle_pack_chan(
        MAVLinkProtocol::instance()->getSystemId(), MAVLinkProtocol::getComponentId(), link->mavlinkChannel(), &message,
        aircraft->icaoAddress(), static_cast<int32_t>(std::lround(position.latitude() * 1e7)),
        static_cast<int32_t>(std::lround(position.longitude() * 1e7)), ADSB_ALTITUDE_TYPE_GEOMETRIC,
        altitudeValid ? static_cast<int32_t>(std::lround(aircraft->altitude() * 1000.0)) : 0,
        static_cast<uint16_t>(std::lround(heading * 100.0) % 36000),
        static_cast<uint16_t>(std::clamp(std::lround(aircraft->velocity() * 100.0), 0L, 65535L)),
        static_cast<int16_t>(std::clamp(std::lround(aircraft->verticalVel() * 100.0), -32768L, 32767L)), callsign,
        static_cast<uint8_t>(aircraft->emitterType()), 0, flags, aircraft->squawk());
    (void) vehicle->sendMessageOnLinkThreadSafe(link.get(), message);
}
