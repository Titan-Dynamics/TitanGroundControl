#pragma once

#include <QtCore/QList>
#include <QtCore/QObject>
#include <QtCore/QTimer>
#include <QtPositioning/QGeoCoordinate>

class ADSBVehicle;
class ADSBVehicleManager;
class Fact;

/// Sends nearby traffic that QGroundControl gets itself, from the internet or an SBS server rather than from the
/// vehicle, to the active vehicle as ADSB_VEHICLE, so the autopilot's ADS-B avoidance can act on it. As TitanPlanner
/// does: once a second, the next of the ten nearest aircraft within 10 km.
class AdsbTrafficSender : public QObject
{
    Q_OBJECT

public:
    AdsbTrafficSender(ADSBVehicleManager* manager, Fact* enabled, QObject* parent = nullptr);

    /// The aircraft to take turns sending: those within kRangeMeters of own, nearest first, at most kMaxAircraft
    static QList<const ADSBVehicle*> nearest(const QList<const ADSBVehicle*>& traffic, const QGeoCoordinate& own);

    static constexpr double kRangeMeters = 10000;
    static constexpr int kMaxAircraft = 10;

private:
    void _sendNext();

    ADSBVehicleManager* _manager = nullptr;
    Fact* _enabled = nullptr;
    QTimer _timer;
    int _index = 0;

    static constexpr int kIntervalMs = 1000;
};
