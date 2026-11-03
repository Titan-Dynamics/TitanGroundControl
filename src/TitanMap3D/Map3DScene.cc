#include "Map3DScene.h"

#include <algorithm>
#include <cmath>

#include <QtCore/QElapsedTimer>
#include <QtCore/QtMath>

namespace Map3D {

namespace {

/// Slow, faster, slow again: 0 to 1 as t goes from 0 to 1
double easeInOutCubic(double t)
{
    return (t < 0.5) ? 4 * t * t * t : 1 - std::pow(2 - 2 * t, 3) / 2;
}

}  // namespace

double nowSeconds()
{
    static QElapsedTimer clock = [] {
        QElapsedTimer timer;
        timer.start();
        return timer;
    }();
    return clock.nsecsElapsed() / 1e9;
}

// ---------------------------------------------------------------------------
// KalmanFilter

KalmanFilter::KalmanFilter(double q, double r)
    : _processNoise(q)
    , _velocityNoise(q * 3.0)
    , _measurementNoise(r)
{}

double KalmanFilter::update(double measurement, double timeSeconds)
{
    double dt = 0.033;  // ~30 fps until there is a previous update
    if (_hasLastUpdate) {
        dt = std::clamp(timeSeconds - _lastUpdate, 0.001, 0.5);
    }
    _lastUpdate = timeSeconds;
    _hasLastUpdate = true;

    // Predict with the constant-velocity model
    const double xPred = _x + _v * dt;
    const double vPred = _v;
    const double p00Pred = _p00 + dt * (_p10 + _p01) + dt * dt * _p11 + _processNoise * dt;
    const double p01Pred = _p01 + dt * _p11;
    const double p10Pred = _p10 + dt * _p11;
    const double p11Pred = _p11 + _velocityNoise * dt;

    // Update, trusting the prediction more when it is close to the measurement
    const double s = p00Pred + _measurementNoise;
    double k0 = p00Pred / s;
    double k1 = p10Pred / s;
    const double innovation = measurement - xPred;
    const double innovationMagnitude = std::abs(innovation);
    double adaptiveFactor = 1.0;
    if (innovationMagnitude < 1.0) {
        adaptiveFactor = _smoothingFactor + (1.0 - _smoothingFactor) * innovationMagnitude;
    }
    k0 *= adaptiveFactor;
    k1 *= adaptiveFactor;

    _x = xPred + k0 * innovation;
    _v = std::clamp(vPred + k1 * innovation, -500.0, 500.0);

    _p00 = (1 - k0) * p00Pred;
    _p01 = (1 - k0) * p01Pred;
    _p10 = p10Pred - k1 * p00Pred;
    _p11 = p11Pred - k1 * p01Pred;
    return _x;
}

double KalmanFilter::updateAngle(double measurement, double timeSeconds)
{
    measurement = _normalizeAngle(measurement);
    double diff = measurement - _normalizeAngle(_x);
    if (diff > 180) {
        diff -= 360;
    } else if (diff < -180) {
        diff += 360;
    }
    // A measurement next to the current state, so the filter never sees the wrap as a jump
    return _normalizeAngle(update(_x + diff, timeSeconds));
}

void KalmanFilter::reset(double value)
{
    _x = value;
    _v = 0;
    _p00 = 1;
    _p01 = 0;
    _p10 = 0;
    _p11 = 1;
    _hasLastUpdate = false;
}

double KalmanFilter::_normalizeAngle(double angle)
{
    angle = std::fmod(angle, 360.0);
    if (angle < 0) {
        angle += 360;
    }
    return angle;
}

// ---------------------------------------------------------------------------

Fix projectFix(const Fix& fix, double timeSeconds)
{
    const double dt = timeSeconds - fix.timeSeconds;
    const double bearing = qRadiansToDegrees(std::atan2(fix.velocityEast, fix.velocityNorth));
    const double distNorth = fix.velocityNorth * dt;
    const double distEast = fix.velocityEast * dt;

    Fix projected = fix;
    newPosition(fix.lat, fix.lng, bearing, std::sqrt(distNorth * distNorth + distEast * distEast), projected.lat,
                projected.lng);
    // down is positive
    projected.alt = fix.alt - fix.velocityDown * dt;
    return projected;
}

// ---------------------------------------------------------------------------
// VehicleTracker

void VehicleTracker::update(const WorldFrame& frame, const Fix& fix, double rawRoll, double rawPitch, double rawYaw,
                            double groundHeight, double rawNavBearing, double rawGpsHeading, bool smooth, bool armed,
                            double timeSeconds)
{
    if (!smooth) {
        double rawEast = 0;
        double rawNorth = 0;
        frame.toAbsolute(fix.lat, fix.lng, rawEast, rawNorth);
        x = rawEast - frame.originEasting();
        y = rawNorth - frame.originNorthing();
        z = std::max(fix.alt, groundHeight + 1);
        roll = static_cast<float>(rawRoll);
        pitch = static_cast<float>(rawPitch);
        yaw = static_cast<float>(rawYaw);
        navBearing = rawNavBearing;
        gpsHeading = rawGpsHeading;

        // Smoothing switched back on starts from here, not from where it left off
        _initialized = false;
        return;
    }

    const Fix projected = projectFix(fix, timeSeconds);
    double east = 0;
    double north = 0;
    frame.toAbsolute(projected.lat, projected.lng, east, north);

    if (!_initialized || (frame.zoneVersion() != _zoneVersion)) {
        _east.reset(east);
        _north.reset(north);
        _altitude.reset(projected.alt);
        _roll.reset(rawRoll);
        _pitch.reset(rawPitch);
        _yaw.reset(rawYaw);
        _navBearing.reset(rawNavBearing);
        _gpsHeading.reset(rawGpsHeading);
        _initialized = true;
        _zoneVersion = frame.zoneVersion();
    }

    const double jumpEast = east - _east.value();
    const double jumpNorth = north - _north.value();
    if (!armed || (jumpEast * jumpEast + jumpNorth * jumpNorth > kJumpDistance * kJumpDistance)) {
        _east.reset(east);
        _north.reset(north);
        _altitude.reset(projected.alt);
    }

    x = _east.update(east, timeSeconds) - frame.originEasting();
    y = _north.update(north, timeSeconds) - frame.originNorthing();
    // Kept a metre above the ground, which the reported altitude often reads under on the ground
    z = std::max(_altitude.update(projected.alt, timeSeconds), groundHeight + 1);

    roll = static_cast<float>(_roll.updateAngle(rawRoll, timeSeconds));
    pitch = static_cast<float>(_pitch.updateAngle(rawPitch, timeSeconds));
    yaw = static_cast<float>(_yaw.updateAngle(rawYaw, timeSeconds));
    navBearing = _navBearing.updateAngle(rawNavBearing, timeSeconds);
    gpsHeading = _gpsHeading.updateAngle(rawGpsHeading, timeSeconds);
}

// ---------------------------------------------------------------------------
// CameraRig

void CameraRig::chase(const VehicleTracker& vehicle, double distance, double height, double angle)
{
    const double heading = qDegreesToRadians(vehicle.yaw + angle + 180);
    x = vehicle.x + std::sin(heading) * distance;
    y = vehicle.y + std::cos(heading) * distance;
    z = vehicle.z + height;
    lookX = vehicle.x;
    lookY = vehicle.y;
    lookZ = vehicle.z;
    roll = 0;
}

void CameraRig::firstPerson(const VehicleTracker& vehicle, double minZ)
{
    look(vehicle.x, vehicle.y, std::max(vehicle.z, minZ), vehicle.yaw, vehicle.pitch);
    roll = vehicle.roll;
}

void CameraRig::look(double px, double py, double pz, double yaw, double pitch)
{
    constexpr double kLookDistance = 100;
    x = px;
    y = py;
    z = pz;
    const double yawRad = qDegreesToRadians(yaw);
    const double pitchRad = qDegreesToRadians(pitch);
    lookX = px + std::sin(yawRad) * std::cos(pitchRad) * kLookDistance;
    lookY = py + std::cos(yawRad) * std::cos(pitchRad) * kLookDistance;
    lookZ = pz + std::sin(pitchRad) * kLookDistance;
    roll = 0;
}

QMatrix4x4 CameraRig::viewMatrix() const
{
    QMatrix4x4 view;
    view.lookAt(QVector3D(x, y, z), QVector3D(lookX, lookY, lookZ), QVector3D(0, 0, 1));
    if (roll != 0) {
        QMatrix4x4 rolled;
        rolled.rotate(roll, 0, 0, 1);
        view = rolled * view;
    }
    return view;
}

void CameraRig::forward(double& fx, double& fy, double& fz) const
{
    fx = lookX - x;
    fy = lookY - y;
    fz = lookZ - z;
    const double length = std::sqrt(fx * fx + fy * fy + fz * fz);
    if (length < 1e-9) {
        fx = 0;
        fy = 1;
        fz = 0;
        return;
    }
    fx /= length;
    fy /= length;
    fz /= length;
}

CameraPose CameraRig::pose(const WorldFrame& frame) const
{
    double fx = 0;
    double fy = 0;
    double fz = 0;
    forward(fx, fy, fz);
    CameraPose result;
    result.east = x + frame.originEasting();
    result.north = y + frame.originNorthing();
    result.up = z;
    result.yaw = qRadiansToDegrees(std::atan2(fx, fy));
    result.pitch = qRadiansToDegrees(std::atan2(fz, std::hypot(fx, fy)));
    result.roll = roll;
    result.zone = frame.zone();
    return result;
}

void CameraRig::setPose(const WorldFrame& frame, const CameraPose& target)
{
    look(target.east - frame.originEasting(), target.north - frame.originNorthing(), target.up, target.yaw,
         target.pitch);
    roll = static_cast<float>(target.roll);
}

// ---------------------------------------------------------------------------
// CameraFlight

void CameraFlight::start(const CameraPose& from, double timeSeconds)
{
    _from = from;
    _startSeconds = timeSeconds;
    _durationSeconds = 0;
    _active = true;
}

CameraPose CameraFlight::update(const CameraPose& to, double timeSeconds, double& arc)
{
    arc = 0;
    if (!_active || (to.zone != _from.zone)) {
        // Positions in another UTM zone don't compare: straight there
        _active = false;
        return to;
    }

    const double east = to.east - _from.east;
    const double north = to.north - _from.north;
    const double up = to.up - _from.up;
    const double distance = std::sqrt(east * east + north * north + up * up);
    if (_durationSeconds <= 0) {
        _durationSeconds = std::clamp(kBaseSeconds + distance / kMetresPerSecond, kMinSeconds, kMaxSeconds);
    }
    const double t = (timeSeconds - _startSeconds) / _durationSeconds;
    if (t >= 1) {
        _active = false;
        return to;
    }

    const double progress = easeInOutCubic(std::max(0.0, t));
    arc = std::sin(M_PI * progress);
    const double rise = std::min(distance * kRise, kMaxRise) * arc;
    CameraPose result;
    result.east = _from.east + east * progress;
    result.north = _from.north + north * progress;
    result.up = _from.up + up * progress + rise;
    result.yaw = _from.yaw + std::remainder(to.yaw - _from.yaw, 360.0) * progress;
    const double lookDown = qRadiansToDegrees(std::atan2(rise, distance / 2));
    result.pitch = std::clamp(_from.pitch + (to.pitch - _from.pitch) * progress - lookDown, -89.0, 89.0);
    result.roll = _from.roll + std::remainder(to.roll - _from.roll, 360.0) * progress;
    result.zone = to.zone;
    return result;
}

}  // namespace Map3D
