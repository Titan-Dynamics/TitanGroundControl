#include "AdsbTrafficTest.h"

#include <QtCore/QScopeGuard>

#include "ADSBVehicle.h"
#include "ADSBVehicleManager.h"
#include "AdsbCategory.h"
#include "AdsbInternetSource.h"
#include "AdsbTrafficSender.h"
#include "MAVLinkLib.h"
#include "QmlObjectListModel.h"

namespace {

mavlink_message_t adsbVehicleMessage(uint32_t icaoAddress)
{
    char callsign[MAVLINK_MSG_ADSB_VEHICLE_FIELD_CALLSIGN_LEN] = "VEHICLE";
    mavlink_message_t message;
    (void) mavlink_msg_adsb_vehicle_pack_chan(
        1, 1, 0, &message, icaoAddress, 470000000, 80000000, ADSB_ALTITUDE_TYPE_GEOMETRIC, 1000000, 9000, 5000, 0,
        callsign, ADSB_EMITTER_TYPE_LIGHT, 1,
        ADSB_FLAGS_VALID_COORDS | ADSB_FLAGS_VALID_ALTITUDE | ADSB_FLAGS_VALID_CALLSIGN, 1200);
    return message;
}

mavlink_message_t collisionMessage(uint32_t icaoAddress, MAV_COLLISION_THREAT_LEVEL threatLevel)
{
    mavlink_message_t message;
    (void) mavlink_msg_collision_pack_chan(1, 1, 0, &message, MAV_COLLISION_SRC_ADSB, icaoAddress,
                                           MAV_COLLISION_ACTION_REPORT, threatLevel, 10.0f, 50.0f, 300.0f);
    return message;
}

ADSBVehicle* findAircraft(uint32_t icaoAddress)
{
    const QmlObjectListModel* const vehicles = ADSBVehicleManager::instance()->adsbVehicles();
    for (int i = 0; i < vehicles->count(); i++) {
        ADSBVehicle* const aircraft = vehicles->value<ADSBVehicle*>(i);
        if (aircraft && (aircraft->icaoAddress() == icaoAddress)) {
            return aircraft;
        }
    }
    return nullptr;
}

}  // namespace

void AdsbTrafficTest::_categoryCodesFollowMavlink()
{
    QCOMPARE(AdsbCategory::fromCode(u"A3"), ADSB_EMITTER_TYPE_LARGE);
    QCOMPARE(AdsbCategory::fromCode(u"a7"), ADSB_EMITTER_TYPE_ROTOCRAFT);
    QCOMPARE(AdsbCategory::fromCode(u"B6"), ADSB_EMITTER_TYPE_UAV);
    QCOMPARE(AdsbCategory::fromCode(u"C3"), ADSB_EMITTER_TYPE_POINT_OBSTACLE);
    QCOMPARE(AdsbCategory::fromCode(u"C4"), ADSB_EMITTER_TYPE_NO_INFO);
    QCOMPARE(AdsbCategory::fromCode(u"D1"), ADSB_EMITTER_TYPE_NO_INFO);
    QCOMPARE(AdsbCategory::fromCode(u""), ADSB_EMITTER_TYPE_NO_INFO);
    QCOMPARE(AdsbCategory::name(ADSB_EMITTER_TYPE_HEAVY), QStringLiteral("Heavy"));
}

void AdsbTrafficTest::_requestsFollowTheServer()
{
    QCOMPARE(AdsbInternetSource::requestUrl(QStringLiteral("https://opendata.adsb.fi"), 47.5, -122.25, 100).toString(),
             QStringLiteral("https://opendata.adsb.fi/api/v3/lat/47.50000/lon/-122.25000/dist/100"));
    QCOMPARE(
        AdsbInternetSource::requestUrl(QStringLiteral("https://opendata.adsb.fi/api/"), 47.5, -122.25, 110).toString(),
        QStringLiteral("https://opendata.adsb.fi/api/v3/lat/47.50000/lon/-122.25000/dist/110"));
    QCOMPARE(AdsbInternetSource::requestUrl(QStringLiteral("https://api.adsb.lol"), 47.5, -122.25, 100).toString(),
             QStringLiteral("https://api.adsb.lol/v2/point/47.50000/-122.25000/100"));
    QCOMPARE(AdsbInternetSource::pollIntervalMs(QStringLiteral("https://opendata.adsb.fi")), 1100);
    QCOMPARE(AdsbInternetSource::pollIntervalMs(QStringLiteral("https://api.adsb.lol")), 3000);
}

void AdsbTrafficTest::_responseIsRead()
{
    const QByteArray json = R"({"now":1700000000000,"ac":[
        {"hex":"a1b2c3","flight":"ual123  ","lat":47.5,"lon":-122.3,"alt_baro":35000,"alt_geom":35500,"gs":450,
         "track":90,"baro_rate":-640,"squawk":"1200","category":"A3","t":"B738","seen_pos":1.5},
        {"hex":"~00abcd","lat":47.6,"lon":-122.2,"alt_baro":"ground","gs":12,"category":"C2"},
        {"hex":"c0ffee","flight":"N1"},
        {"hex":"xyz","lat":1,"lon":1}]})";

    // Received two seconds after the snapshot was taken
    const QList<AdsbInternetSource::Report> reports = AdsbInternetSource::parseResponse(json, 1700000002000);
    QCOMPARE(reports.size(), 2);

    const AdsbInternetSource::Report& airliner = reports.at(0);
    QCOMPARE(airliner.info.icaoAddress, 0xA1B2C3u);
    QCOMPARE(airliner.info.callsign, QStringLiteral("UAL123"));
    QCOMPARE(airliner.info.location.latitude(), 47.5);
    QVERIFY(qAbs(airliner.info.location.altitude() - (35500 * 0.3048)) < 0.01);
    QCOMPARE(airliner.info.altitudeeType, ADSB_ALTITUDE_TYPE_GEOMETRIC);
    QVERIFY(qAbs(airliner.info.velocity - (450 * 0.514444)) < 0.001);
    QCOMPARE(airliner.info.heading, 90.0);
    QVERIFY(airliner.trackKnown);
    QVERIFY(qAbs(airliner.info.verticalVel + (640 * 0.00508)) < 0.0001);
    QCOMPARE(airliner.info.squawk, static_cast<uint16_t>(1200));
    QCOMPARE(airliner.info.emitterType, ADSB_EMITTER_TYPE_LARGE);
    QCOMPARE(airliner.info.aircraftType, QStringLiteral("B738"));
    QVERIFY(!airliner.info.onGround);
    QVERIFY(airliner.info.availableFlags.testFlag(ADSB::HeadingAvailable));
    // 1.5 s since its position was measured, in a snapshot already 2 s old
    QVERIFY(qAbs(airliner.ageSeconds - 3.5) < 0.001);

    const AdsbInternetSource::Report& truck = reports.at(1);
    QCOMPARE(truck.info.icaoAddress, 0x0100ABCDu);
    QVERIFY(truck.info.onGround);
    QVERIFY(!truck.trackKnown);
    QVERIFY(!truck.info.availableFlags.testFlag(ADSB::AltitudeAvailable));
    QCOMPARE(truck.info.emitterType, ADSB_EMITTER_TYPE_SERVICE_SURFACE);

    QVERIFY(AdsbInternetSource::parseResponse("not json", 0).isEmpty());
}

void AdsbTrafficTest::_trafficIsCarriedForward()
{
    const QGeoCoordinate measured(47.0, 8.0, 1000.0);

    const QGeoCoordinate predicted = AdsbInternetSource::predict(measured, 90.0, 100.0, 5.0, 3.0);
    QVERIFY(qAbs(measured.distanceTo(predicted) - 300.0) < 1.0);
    QVERIFY(qAbs(measured.azimuthTo(predicted) - 90.0) < 0.5);
    QVERIFY(qAbs(predicted.altitude() - 1015.0) < 0.01);

    // Never more than ten seconds ahead
    const QGeoCoordinate capped = AdsbInternetSource::predict(measured, 90.0, 100.0, 0.0, 60.0);
    QVERIFY(qAbs(measured.distanceTo(capped) - 1000.0) < 1.0);

    QCOMPARE(AdsbInternetSource::predict(measured, 90.0, 0.0, 0.0, 3.0), measured);
}

void AdsbTrafficTest::_nearestAircraftAreKept()
{
    const QGeoCoordinate center(47.0, 8.0);

    // Five aircraft, 5 km out down to 1 km
    QList<AdsbInternetSource::Report> reports;
    for (int i = 0; i < 5; i++) {
        AdsbInternetSource::Report report;
        report.info.icaoAddress = 0xD00000 + static_cast<uint32_t>(i);
        report.info.location = center.atDistanceAndAzimuth(1000.0 * (5 - i), 45.0);
        reports.append(report);
    }

    const AdsbInternetSource::Snapshot snapshot = AdsbInternetSource::selectNearest(reports, center, 3);
    QCOMPARE(snapshot.nearest.size(), 3);
    QCOMPARE(snapshot.nearest.at(0).info.icaoAddress, 0xD00004u);
    QCOMPARE(snapshot.nearest.at(2).info.icaoAddress, 0xD00002u);
    QCOMPARE(snapshot.listed.size(), 5);
}

void AdsbTrafficTest::_nearestTrafficTakesTurns()
{
    const QGeoCoordinate own(47.0, 8.0);
    QList<ADSBVehicle*> aircraft;
    const auto cleanup = qScopeGuard([&aircraft] { qDeleteAll(aircraft); });

    // Twelve aircraft 500 m apart and one 20 km out, farthest first
    QList<const ADSBVehicle*> traffic;
    for (int i = 12; i >= 0; i--) {
        ADSB::VehicleInfo_t info;
        info.icaoAddress = 0xB00000 + static_cast<uint32_t>(i);
        info.availableFlags = ADSB::LocationAvailable;
        info.location = own.atDistanceAndAzimuth((i == 12) ? 20000.0 : 500.0 * (i + 1), 90.0);
        aircraft.append(new ADSBVehicle(info));
        traffic.append(aircraft.last());
    }

    const QList<const ADSBVehicle*> nearest = AdsbTrafficSender::nearest(traffic, own);
    QCOMPARE(nearest.size(), AdsbTrafficSender::kMaxAircraft);
    QCOMPARE(nearest.first()->icaoAddress(), 0xB00000u);
    QCOMPARE(nearest.last()->icaoAddress(), 0xB00009u);
}

void AdsbTrafficTest::_vehicleEchoesOfLocalTrafficAreIgnored()
{
    ADSBVehicleManager* const manager = ADSBVehicleManager::instance();

    // Known to QGroundControl itself (an SBS callsign message, say) before it has a position
    ADSB::VehicleInfo_t local;
    local.icaoAddress = 0xC10001;
    local.callsign = QStringLiteral("LOCAL");
    local.availableFlags = ADSB::CallsignAvailable;
    manager->adsbVehicleUpdate(local);
    QVERIFY(manager->isLocalTraffic(0xC10001));
    QVERIFY(!findAircraft(0xC10001));

    // The vehicle's copy of it, as sent for avoidance, is not taken back
    manager->mavlinkMessageReceived(adsbVehicleMessage(0xC10001));
    QVERIFY(!findAircraft(0xC10001));

    // Traffic only the vehicle knows is
    manager->mavlinkMessageReceived(adsbVehicleMessage(0xC10002));
    ADSBVehicle* const vehicleTraffic = findAircraft(0xC10002);
    QVERIFY(vehicleTraffic);
    QVERIFY(!manager->isLocalTraffic(0xC10002));
    QCOMPARE(vehicleTraffic->emitterType(), static_cast<int>(ADSB_EMITTER_TYPE_LIGHT));
}

void AdsbTrafficTest::_collisionReportsSetTheThreatLevel()
{
    ADSBVehicleManager* const manager = ADSBVehicleManager::instance();

    ADSB::VehicleInfo_t info;
    info.icaoAddress = 0xC20001;
    info.location = QGeoCoordinate(47.0, 8.0, 500.0);
    info.availableFlags = ADSB::LocationAvailable | ADSB::AltitudeAvailable;
    manager->adsbVehicleUpdate(info);
    ADSBVehicle* const aircraft = findAircraft(0xC20001);
    QVERIFY(aircraft);
    QCOMPARE(aircraft->threatLevel(), static_cast<int>(MAV_COLLISION_THREAT_LEVEL_NONE));

    manager->mavlinkMessageReceived(collisionMessage(0xC20001, MAV_COLLISION_THREAT_LEVEL_HIGH));
    QCOMPARE(aircraft->threatLevel(), static_cast<int>(MAV_COLLISION_THREAT_LEVEL_HIGH));

    manager->mavlinkMessageReceived(collisionMessage(0xC20001, MAV_COLLISION_THREAT_LEVEL_NONE));
    QCOMPARE(aircraft->threatLevel(), static_cast<int>(MAV_COLLISION_THREAT_LEVEL_NONE));
}

UT_REGISTER_TEST(AdsbTrafficTest, TestLabel::Unit)
