#pragma once

#include <QtCore/QElapsedTimer>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QString>
#include <QtCore/QTimer>
#include <QtQmlIntegration/QtQmlIntegration>

#include "Vehicle.h"

/// Telemetry for the Titan HUD that QGroundControl's Vehicle does not keep: the
/// navigation controller output, ArduPilot's EKF status report and angle of attack, the heartbeat and
/// system status flags, plus values derived from the vehicle facts (airspeed trend, turn rate, targets)
/// and the highest-severity recent status text.
class HudData : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_MOC_INCLUDE("Vehicle.h")
    Q_PROPERTY(Vehicle* vehicle READ vehicle WRITE setVehicle NOTIFY vehicleChanged)
    Q_PROPERTY(double navBearing READ navBearing NOTIFY dataChanged)        ///< deg, NaN when unknown
    Q_PROPERTY(double targetBearing READ targetBearing NOTIFY dataChanged)  ///< deg, NaN when unknown
    Q_PROPERTY(double wpDistance READ wpDistance NOTIFY dataChanged)        ///< m, NaN when unknown
    Q_PROPERTY(double altitudeError READ altitudeError NOTIFY dataChanged)  ///< m
    Q_PROPERTY(double airspeedError READ airspeedError NOTIFY dataChanged)  ///< m/s
    Q_PROPERTY(double xtrackError READ xtrackError NOTIFY dataChanged)      ///< m
    Q_PROPERTY(
        double ekfStatus READ ekfStatus NOTIFY dataChanged)  ///< 0..1 worst variance, 1 on bad flags, NaN unknown
    Q_PROPERTY(double angleOfAttack READ angleOfAttack NOTIFY dataChanged)  ///< deg, NaN when unknown
    Q_PROPERTY(double sideslip READ sideslip NOTIFY dataChanged)            ///< deg, NaN when unknown
    Q_PROPERTY(bool failsafe READ failsafe NOTIFY dataChanged)              ///< heartbeat system status CRITICAL
    Q_PROPERTY(bool safetyActive READ safetyActive NOTIFY dataChanged)  ///< motor outputs disabled by the safety switch
    Q_PROPERTY(double airspeedAccel READ airspeedAccel NOTIFY dataChanged)  ///< m/s^2, smoothed
    Q_PROPERTY(double turnRate READ turnRate NOTIFY dataChanged)            ///< deg/s from bank angle and ground speed
    Q_PROPERTY(double targetAltitude READ targetAltitude NOTIFY dataChanged)  ///< m relative, NaN when unknown
    Q_PROPERTY(double targetAirspeed READ targetAirspeed NOTIFY dataChanged)  ///< m/s, NaN when unknown
    Q_PROPERTY(bool recentlyArmed READ recentlyArmed NOTIFY dataChanged)      ///< true for 8 s after arming
    Q_PROPERTY(QString message READ message NOTIFY dataChanged)  ///< highest-severity status text of the last 10 s
    Q_PROPERTY(int messageSeverity READ messageSeverity NOTIFY dataChanged)  ///< MAV_SEVERITY of message

public:
    explicit HudData(QObject* parent = nullptr);

    Vehicle* vehicle() const { return _vehicle; }

    void setVehicle(Vehicle* vehicle);

    double navBearing() const { return _navBearing; }

    double targetBearing() const { return _targetBearing; }

    double wpDistance() const { return _wpDistance; }

    double altitudeError() const { return _altitudeError; }

    double airspeedError() const { return _airspeedError; }

    double xtrackError() const { return _xtrackError; }

    double ekfStatus() const { return _ekfStatus; }

    double angleOfAttack() const { return _angleOfAttack; }

    double sideslip() const { return _sideslip; }

    bool failsafe() const { return _failsafe; }

    bool safetyActive() const { return _safetyActive; }

    double airspeedAccel() const { return _airspeedAccel; }

    double turnRate() const;
    double targetAltitude() const;
    double targetAirspeed() const;

    bool recentlyArmed() const { return _armedClock.isValid() && _armedClock.elapsed() < kArmedFlashMs; }

    QString message() const { return _message; }

    int messageSeverity() const { return _messageSeverity; }

    static constexpr int kArmedFlashMs = 8000;
    static constexpr int kMessageExpiryMs = 10000;
    static constexpr int kMessageSeverityThreshold = 4;  ///< MAV_SEVERITY_WARNING and worse are shown

signals:
    void vehicleChanged();
    void dataChanged();

private slots:
    void _handleMavlinkMessage(const mavlink_message_t& message);
    void _handleTextMessage(int sysid, int compid, int severity, const QString& text, const QString& description);
    void _handleArmedChanged(bool armed);
    void _tick();

private:
    void _resetTelemetry();

    QPointer<Vehicle> _vehicle;
    QTimer _tickTimer;

    double _navBearing = qQNaN();
    double _targetBearing = qQNaN();
    double _wpDistance = qQNaN();
    double _altitudeError = qQNaN();
    double _airspeedError = qQNaN();
    double _xtrackError = qQNaN();
    double _ekfStatus = qQNaN();
    double _angleOfAttack = qQNaN();
    double _sideslip = qQNaN();
    bool _failsafe = false;
    bool _safetyActive = false;

    double _airspeedAccel = 0.0;
    double _lastAirspeed = qQNaN();
    QElapsedTimer _airspeedClock;
    QElapsedTimer _armedClock;

    QString _message;
    int _messageSeverity = 6;
    QElapsedTimer _messageClock;
};
