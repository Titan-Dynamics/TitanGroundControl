#pragma once

#include <QtCore/QElapsedTimer>
#include <QtCore/QtNumeric>
#include <QtCore/QObject>
#include <QtPositioning/QGeoCoordinate>
#include <QtQmlIntegration/QtQmlIntegration>

#include "ADSB.h"

class ADSBVehicle : public QObject
{
    Q_OBJECT
    // QML_ELEMENT

    friend class ADSBTest;

    Q_PROPERTY(uint             icaoAddress READ    icaoAddress CONSTANT)
    Q_PROPERTY(QString          callsign    READ    callsign    NOTIFY callsignChanged)
    Q_PROPERTY(QGeoCoordinate   coordinate  READ    coordinate  NOTIFY coordinateChanged)
    Q_PROPERTY(double           altitude    READ    altitude    NOTIFY altitudeChanged)
    Q_PROPERTY(double           heading     READ    heading     NOTIFY headingChanged)
    Q_PROPERTY(double           velocity    READ    velocity    NOTIFY velocityChanged)
    Q_PROPERTY(double           verticalVel READ    verticalVel NOTIFY verticalVelChanged)
    Q_PROPERTY(uint16_t         squawk      READ    squawk      NOTIFY squawkChanged)
    Q_PROPERTY(bool             alert       READ    alert       NOTIFY alertChanged)
    // Titan: category and type for the map icon and tooltip, the ground state, and the autopilot's collision threat
    // level (MAV_COLLISION_THREAT_LEVEL)
    Q_PROPERTY(int              emitterType     READ emitterType    NOTIFY emitterTypeChanged)
    Q_PROPERTY(QString          categoryName    READ categoryName   NOTIFY emitterTypeChanged)
    Q_PROPERTY(QString          aircraftType    READ aircraftType   NOTIFY aircraftTypeChanged)
    Q_PROPERTY(bool             onGround        READ onGround       NOTIFY onGroundChanged)
    Q_PROPERTY(int              threatLevel     READ threatLevel    NOTIFY threatLevelChanged)

public:
    explicit ADSBVehicle(const ADSB::VehicleInfo_t &vehicleInfo, QObject *parent = nullptr);
    ~ADSBVehicle();

    uint32_t icaoAddress() const { return _info.icaoAddress; }
    QString callsign() const { return _info.callsign; }
    QGeoCoordinate coordinate() const { return _info.location; }
    double altitude() const { return _info.location.altitude(); }
    double heading() const { return _info.heading; }
    double velocity() const { return _info.velocity; }
    double verticalVel() const { return _info.verticalVel; }
    uint16_t squawk() const { return _info.squawk; }
    bool alert() const { return _info.alert; }
    int emitterType() const { return _info.emitterType; }
    QString categoryName() const;
    QString aircraftType() const { return _info.aircraftType; }
    bool onGround() const { return _info.onGround; }
    int threatLevel() const { return _threatLevel; }
    void setThreatLevel(int threatLevel);
    bool expired() const { return !_lastUpdateTimer.isValid() || _lastUpdateTimer.hasExpired(_expirationTimeoutMs); }
    void update(const ADSB::VehicleInfo_t &vehicleInfo);

signals:
    void coordinateChanged();
    void callsignChanged();
    void altitudeChanged();
    void headingChanged();
    void velocityChanged();
    void verticalVelChanged();
    void squawkChanged();
    void alertChanged();
    void emitterTypeChanged();
    void aircraftTypeChanged();
    void onGroundChanged();
    void threatLevelChanged();

private:
    ADSB::VehicleInfo_t _info{};
    int _threatLevel = 0;
    QElapsedTimer _lastUpdateTimer;
    QElapsedTimer _lastPropertyUpdateTimer;

    static constexpr qint64 _expirationTimeoutMs = 120000; ///< timeout with no update in ms after which the vehicle is removed.
    static constexpr qint64 _propertyUpdateMinIntervalMs = 1000; ///< min interval in ms between property updates
};
