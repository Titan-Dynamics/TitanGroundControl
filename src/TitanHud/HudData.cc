#include "HudData.h"

#include <algorithm>
#include <cmath>

#include <QtCore/QtMath>

#include "Fact.h"

namespace {
constexpr int kTickMs = 100;
constexpr double kAccelSmoothing = 0.2;
}  // namespace

HudData::HudData(QObject* parent)
    : QObject(parent)
{
    _tickTimer.setInterval(kTickMs);
    (void) connect(&_tickTimer, &QTimer::timeout, this, &HudData::_tick);
}

void HudData::setVehicle(Vehicle* vehicle)
{
    if (_vehicle == vehicle) {
        return;
    }
    if (_vehicle) {
        (void) disconnect(_vehicle, nullptr, this, nullptr);
    }
    _vehicle = vehicle;
    _resetTelemetry();
    if (_vehicle) {
        (void) connect(_vehicle, &Vehicle::mavlinkMessageReceived, this, &HudData::_handleMavlinkMessage);
        (void) connect(_vehicle, &Vehicle::textMessageReceived, this, &HudData::_handleTextMessage);
        (void) connect(_vehicle, &Vehicle::armedChanged, this, &HudData::_handleArmedChanged);
        _tickTimer.start();
    } else {
        _tickTimer.stop();
    }
    emit vehicleChanged();
    emit dataChanged();
}

double HudData::turnRate() const
{
    if (!_vehicle) {
        return 0.0;
    }
    const double groundSpeed = _vehicle->groundSpeed()->rawValue().toDouble();
    const double roll = _vehicle->roll()->rawValue().toDouble();
    if (!(groundSpeed > 1.0) || std::isnan(roll)) {
        return 0.0;
    }
    // Coordinated-turn rate: g tan(roll) / v, in degrees per second
    return qRadiansToDegrees(9.80665 * std::tan(qDegreesToRadians(roll)) / groundSpeed);
}

double HudData::targetAltitude() const
{
    if (!_vehicle || std::isnan(_altitudeError)) {
        return qQNaN();
    }
    // ArduPilot reports alt_error as target minus current
    return _vehicle->altitudeRelative()->rawValue().toDouble() + _altitudeError;
}

double HudData::targetAirspeed() const
{
    if (!_vehicle || std::isnan(_airspeedError)) {
        return qQNaN();
    }
    return _vehicle->airSpeed()->rawValue().toDouble() + _airspeedError;
}

void HudData::_handleMavlinkMessage(const mavlink_message_t& message)
{
    bool changed = true;
    switch (message.msgid) {
        case MAVLINK_MSG_ID_NAV_CONTROLLER_OUTPUT: {
            mavlink_nav_controller_output_t nav{};
            mavlink_msg_nav_controller_output_decode(&message, &nav);
            _navBearing = nav.nav_bearing;
            _targetBearing = nav.target_bearing;
            _wpDistance = nav.wp_dist;
            _altitudeError = nav.alt_error;
            _airspeedError = nav.aspd_error;
            _xtrackError = nav.xtrack_error;
            break;
        }
        case MAVLINK_MSG_ID_HEARTBEAT: {
            mavlink_heartbeat_t heartbeat{};
            mavlink_msg_heartbeat_decode(&message, &heartbeat);
            _failsafe = (heartbeat.system_status == MAV_STATE_CRITICAL);
            break;
        }
        case MAVLINK_MSG_ID_SYS_STATUS: {
            mavlink_sys_status_t status{};
            mavlink_msg_sys_status_decode(&message, &status);
            _safetyActive = (status.onboard_control_sensors_enabled & MAV_SYS_STATUS_SENSOR_MOTOR_OUTPUTS) == 0;
            break;
        }
#ifdef MAVLINK_MSG_ID_EKF_STATUS_REPORT
        case MAVLINK_MSG_ID_EKF_STATUS_REPORT: {
            mavlink_ekf_status_report_t ekf{};
            mavlink_msg_ekf_status_report_decode(&message, &ekf);
            // Mission Planner: the worst variance, forced to 1 when the estimator reports a bad attitude or position
            double worst =
                std::max({static_cast<double>(ekf.velocity_variance), static_cast<double>(ekf.pos_horiz_variance),
                          static_cast<double>(ekf.pos_vert_variance), static_cast<double>(ekf.compass_variance),
                          static_cast<double>(ekf.terrain_alt_variance)});
            const uint16_t required = EKF_ATTITUDE | EKF_VELOCITY_HORIZ | EKF_POS_HORIZ_REL;
            if ((ekf.flags & required) != required) {
                worst = 1.0;
            }
            _ekfStatus = std::min(worst, 1.0);
            break;
        }
#endif
#ifdef MAVLINK_MSG_ID_AOA_SSA
        case MAVLINK_MSG_ID_AOA_SSA: {
            mavlink_aoa_ssa_t aoaSsa{};
            mavlink_msg_aoa_ssa_decode(&message, &aoaSsa);
            _angleOfAttack = aoaSsa.AOA;
            _sideslip = aoaSsa.SSA;
            break;
        }
#endif
        default:
            changed = false;
            break;
    }
    if (changed) {
        emit dataChanged();
    }
}

void HudData::_handleTextMessage(int sysid, int compid, int severity, const QString& text, const QString& description)
{
    Q_UNUSED(sysid);
    Q_UNUSED(compid);
    Q_UNUSED(description);
    // Lower MAV_SEVERITY values are more severe; keep the worst message of the expiry window
    const bool worthShowing = severity <= kMessageSeverityThreshold || text.startsWith(QStringLiteral("PreArm")) ||
                              text.startsWith(QStringLiteral("Arm:"));
    if (!worthShowing) {
        return;
    }
    const bool currentExpired =
        _message.isEmpty() || !_messageClock.isValid() || _messageClock.elapsed() >= kMessageExpiryMs;
    if (!currentExpired && severity > _messageSeverity) {
        return;
    }
    _message = text;
    _messageSeverity = severity;
    _messageClock.restart();
    emit dataChanged();
}

void HudData::_handleArmedChanged(bool armed)
{
    if (armed) {
        _armedClock.restart();
    } else {
        _armedClock.invalidate();
    }
    emit dataChanged();
}

void HudData::_tick()
{
    bool changed = false;

    if (!_message.isEmpty() && _messageClock.isValid() && _messageClock.elapsed() >= kMessageExpiryMs) {
        _message.clear();
        _messageClock.invalidate();
        changed = true;
    }

    if (_armedClock.isValid() && _armedClock.elapsed() >= kArmedFlashMs) {
        _armedClock.invalidate();
        changed = true;
    }

    if (_vehicle) {
        // Airspeed trend, smoothed, for the tape's acceleration strip
        const double airspeed = _vehicle->airSpeed()->rawValue().toDouble();
        if (!std::isnan(airspeed)) {
            if (!std::isnan(_lastAirspeed) && _airspeedClock.isValid()) {
                const double dt = _airspeedClock.elapsed() / 1000.0;
                if (dt > 0.0) {
                    const double accel = (airspeed - _lastAirspeed) / dt;
                    _airspeedAccel += (accel - _airspeedAccel) * kAccelSmoothing;
                }
            }
            _lastAirspeed = airspeed;
            _airspeedClock.restart();
        }
        changed = true;  // facts such as ground speed feed turnRate, which has no notify of its own
    }

    if (changed) {
        emit dataChanged();
    }
}

void HudData::_resetTelemetry()
{
    _navBearing = qQNaN();
    _targetBearing = qQNaN();
    _wpDistance = qQNaN();
    _altitudeError = qQNaN();
    _airspeedError = qQNaN();
    _xtrackError = qQNaN();
    _ekfStatus = qQNaN();
    _angleOfAttack = qQNaN();
    _sideslip = qQNaN();
    _failsafe = false;
    _safetyActive = false;
    _airspeedAccel = 0.0;
    _lastAirspeed = qQNaN();
    _airspeedClock.invalidate();
    _armedClock.invalidate();
    _message.clear();
    _messageClock.invalidate();
}
