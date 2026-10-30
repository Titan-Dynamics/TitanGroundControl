#pragma once

#include <QtGui/QMatrix4x4>

#include "Map3DGeo.h"

/// The vehicle's smoothed pose and the camera that follows it, ported from Mission Planner's Map3D
/// (VehicleTracker, CameraRig, SimpleKalmanFilter and LocationProjection). Render thread only.
namespace Map3D {

/// Monotonic seconds, the clock the filters and dead reckoning run on.
double nowSeconds();

/// Position-velocity Kalman filter with an adaptive gain: small innovations trust the prediction more.
class KalmanFilter
{
public:
    KalmanFilter(double q, double r);

    double update(double measurement) { return update(measurement, nowSeconds()); }

    double update(double measurement, double timeSeconds);

    /// Angular measurement in degrees, filtered across the 0/360 wrap; returns 0..360.
    double updateAngle(double measurement) { return updateAngle(measurement, nowSeconds()); }

    double updateAngle(double measurement, double timeSeconds);
    void reset(double value);

    double value() const { return _x; }

    double velocity() const { return _v; }

private:
    static double _normalizeAngle(double angle);

    double _x = 0;
    double _v = 0;
    double _p00 = 1;
    double _p01 = 0;
    double _p10 = 0;
    double _p11 = 1;
    double _processNoise;
    double _velocityNoise;
    double _measurementNoise;
    double _smoothingFactor = 0.3;
    double _lastUpdate = 0;
    bool _hasLastUpdate = false;
};

/// A telemetry fix: position (altitude in metres AMSL), when it arrived and the vehicle's NED velocity (m/s).
struct Fix
{
    double lat = 0;
    double lng = 0;
    double alt = 0;
    double timeSeconds = 0;
    double velocityNorth = 0;
    double velocityEast = 0;
    double velocityDown = 0;
};

/// Where a fix moving at its velocity is at timeSeconds (Mission Planner's LocationProjection.Project).
Fix projectFix(const Fix& fix, double timeSeconds);

/// Smooths the vehicle's pose between telemetry updates: dead-reckons from the last fix using velocity, then
/// runs Kalman filters on position, altitude, attitude and the heading indicators. Position is filtered in
/// absolute UTM so moving the scene origin never makes the vehicle slide.
class VehicleTracker
{
public:
    static constexpr double kJumpDistance = 100;

    /// Vehicle position in local scene coordinates (z is AMSL)
    double x = 0;
    double y = 0;
    double z = 0;
    float roll = 0;
    float pitch = 0;
    float yaw = 0;
    double navBearing = 0;
    double gpsHeading = 0;

    /// Forget filter state so the next update jumps straight to the vehicle.
    void reset() { _initialized = false; }

    /// Takes this frame's pose. Unsmoothed it is the telemetry as it arrived. Disarmed, the position is never
    /// eased either; armed, a fix more than kJumpDistance away is jumped to as well.
    void update(const WorldFrame& frame, const Fix& fix, double roll, double pitch, double yaw, double groundHeight,
                double rawNavBearing, double rawGpsHeading, bool smooth, bool armed, double timeSeconds);

private:
    KalmanFilter _east{0.02, 1.5};
    KalmanFilter _north{0.02, 1.5};
    KalmanFilter _altitude{0.02, 1.5};
    KalmanFilter _roll{0.015, 2.0};
    KalmanFilter _pitch{0.015, 2.0};
    KalmanFilter _yaw{0.015, 2.5};
    KalmanFilter _navBearing{0.015, 2.0};
    KalmanFilter _gpsHeading{0.015, 2.0};
    bool _initialized = false;
    int _zoneVersion = -1;
};

/// Where the camera sits and looks: orbiting behind the vehicle, from the cockpit (FPV), or free-look from a
/// fixed point when no vehicle is connected.
class CameraRig
{
public:
    double x = 0;
    double y = 0;
    double z = 0;
    double lookX = 0;
    double lookY = 0;
    double lookZ = 0;
    /// Camera roll in degrees (FPV only)
    float roll = 0;

    /// Behind the vehicle at distance/height, rotated by angle (degrees) around it.
    void chase(const VehicleTracker& vehicle, double distance, double height, double angle);
    /// From the vehicle, but no lower than minZ: on the ground the vehicle sits level with the terrain.
    void firstPerson(const VehicleTracker& vehicle, double minZ);
    /// From a point, looking along yaw/pitch in degrees (pitch positive up).
    void look(double px, double py, double pz, double yaw, double pitch);

    QMatrix4x4 viewMatrix() const;
    /// Unit view direction.
    void forward(double& fx, double& fy, double& fz) const;
};

}  // namespace Map3D
