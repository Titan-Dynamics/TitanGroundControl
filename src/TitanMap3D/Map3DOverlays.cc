#include "Map3DOverlays.h"

#include <algorithm>
#include <cmath>

#include <QtCore/QtMath>

namespace Map3D {

// ---------------------------------------------------------------------------
// LineList

void LineList::_vertex(const QVector3D& a, const QVector3D& b, float side, float end, const Rgba& color, float width)
{
    _data.insert(_data.end(),
                 {a.x(), a.y(), a.z(), b.x(), b.y(), b.z(), side, end, color[0], color[1], color[2], color[3], width});
}

void LineList::addSegment(const QVector3D& a, const QVector3D& b, const Rgba& color, float width)
{
    _vertex(a, b, -1, 0, color, width);
    _vertex(a, b, 1, 0, color, width);
    _vertex(a, b, -1, 1, color, width);
    _vertex(a, b, -1, 1, color, width);
    _vertex(a, b, 1, 0, color, width);
    _vertex(a, b, 1, 1, color, width);
}

void LineList::addStrip(const std::vector<QVector3D>& points, const Rgba& color, float width)
{
    for (size_t i = 1; i < points.size(); i++) {
        addSegment(points[i - 1], points[i], color, width);
    }
}

// ---------------------------------------------------------------------------
// Overlays

namespace Overlays {

void addDirection(LineList& lines, const VehicleTracker& vehicle, double bearing, double pitch, const Rgba& color,
                  float width)
{
    const double bearingRad = qDegreesToRadians(bearing);
    const double horizontal = kLineLength * std::cos(pitch);
    lines.addSegment(
        QVector3D(vehicle.x, vehicle.y, vehicle.z),
        QVector3D(vehicle.x + std::sin(bearingRad) * horizontal, vehicle.y + std::cos(bearingRad) * horizontal,
                  vehicle.z + kLineLength * std::sin(pitch)),
        color, width);
}

void addTurnArc(LineList& lines, const VehicleTracker& vehicle, double radius, double pitch, float width)
{
    static constexpr Rgba kPink = {1.0f, 105.0f / 255.0f, 180.0f / 255.0f, 1.0f};
    const double sweep = std::min(M_PI, kTurnArcLength / std::abs(radius));
    const double course = qDegreesToRadians(vehicle.gpsHeading);
    const double perpendicular = course + (radius > 0 ? M_PI / 2 : -M_PI / 2);
    const double horizontalRadius = std::abs(radius) * std::cos(pitch);
    const double centerX = vehicle.x + std::sin(perpendicular) * horizontalRadius;
    const double centerY = vehicle.y + std::cos(perpendicular) * horizontalRadius;
    const double start = std::atan2(vehicle.x - centerX, vehicle.y - centerY);
    const double step = sweep / kTurnSegments;
    const double direction = radius > 0 ? 1 : -1;
    const double climbPerSegment = std::abs(radius) * step * std::sin(pitch);

    std::vector<QVector3D> points;
    points.reserve(kTurnSegments + 1);
    points.emplace_back(vehicle.x, vehicle.y, vehicle.z);
    for (int i = 1; i <= kTurnSegments; i++) {
        const double angle = start + direction * step * i;
        points.emplace_back(centerX + std::sin(angle) * horizontalRadius, centerY + std::cos(angle) * horizontalRadius,
                            vehicle.z + climbPerSegment * i);
    }
    lines.addStrip(points, kPink, width);
}

void addBillboardCircle(LineList& lines, const QVector3D& center, double radius, double viewX, double viewY,
                        double viewZ, const Rgba& color, float width)
{
    constexpr int kSegments = 24;
    // Right = view x world-up; up = right x view
    double rightX = viewY;
    double rightY = -viewX;
    const double rightZ = 0;
    const double rightLength = std::sqrt(rightX * rightX + rightY * rightY);
    if (rightLength > 0.001) {
        rightX /= rightLength;
        rightY /= rightLength;
    } else {
        rightX = 1;
        rightY = 0;
    }
    const double upX = rightY * viewZ - rightZ * viewY;
    const double upY = rightZ * viewX - rightX * viewZ;
    const double upZ = rightX * viewY - rightY * viewX;

    const auto point = [&](double angle) {
        const double c = std::cos(angle);
        const double s = std::sin(angle);
        return QVector3D(center.x() + radius * (c * rightX + s * upX), center.y() + radius * (c * rightY + s * upY),
                         center.z() + radius * (c * rightZ + s * upZ));
    };
    for (int i = 0; i < kSegments; i++) {
        lines.addSegment(point(2 * M_PI * i / kSegments), point(2 * M_PI * (i + 1) / kSegments), color, width);
    }
}

Rgba adsbColor(double distance)
{
    constexpr double kRed = 5000;
    constexpr double kYellow = 10000;
    constexpr double kGreen = 20000;
    if (distance <= kRed) {
        return {1, 0, 0, 1};
    }
    if (distance <= kYellow) {
        return {1, static_cast<float>((distance - kRed) / (kYellow - kRed)), 0, 1};
    }
    if (distance <= kGreen) {
        return {1 - static_cast<float>((distance - kYellow) / (kGreen - kYellow)), 1, 0, 1};
    }
    return {0, 1, 0, 1};
}

}  // namespace Overlays

// ---------------------------------------------------------------------------
// Trail

namespace {

// Scales a direction to a length; a zero one takes the fallback's direction
void scaleTo(double& x, double& y, double& z, double length, double fx, double fy, double fz)
{
    double size = std::sqrt(x * x + y * y + z * z);
    if (size < 1e-9) {
        x = fx;
        y = fy;
        z = fz;
        size = std::sqrt(x * x + y * y + z * z);
    }
    x *= length / size;
    y *= length / size;
    z *= length / size;
}

}  // namespace

void Trail::record(const WorldFrame& frame, const VehicleTracker& vehicle, bool enabled, bool armed, int maxPoints)
{
    if (!enabled || !armed || (vehicle.z < 0.5)) {
        _stableFrames = 0;
        return;
    }
    // Frames of a real altitude to wait for, so the trail never starts at 0 m
    if (++_stableFrames < kStabilityFrames) {
        return;
    }
    if (_zoneVersion != frame.zoneVersion()) {
        _points.clear();
        _zoneVersion = frame.zoneVersion();
    }
    if (static_cast<int>(_points.size()) > maxPoints) {
        _points.erase(_points.begin(), _points.begin() + (static_cast<int>(_points.size()) - maxPoints));
    }
    _points.push_back({vehicle.x + frame.originEasting(), vehicle.y + frame.originNorthing(), vehicle.z});
}

void Trail::draw(LineList& lines, const WorldFrame& frame, const VehicleTracker& vehicle, bool armed, bool smooth,
                 double velocityNorth, double velocityEast, double velocityDown, float width) const
{
    if (!armed || (_points.size() < 2) || (_zoneVersion != frame.zoneVersion())) {
        return;
    }

    // MidnightBlue, as the 2D map's track
    static constexpr Rgba kColor = {25.0f / 255.0f, 25.0f / 255.0f, 112.0f / 255.0f, 144.0f / 255.0f};
    const QVector3D vehiclePoint(vehicle.x, vehicle.y, vehicle.z);

    std::vector<QVector3D> points;
    if (!smooth) {
        points.reserve(_points.size() + 1);
        for (const auto& point : _points) {
            points.emplace_back(point[0] - frame.originEasting(), point[1] - frame.originNorthing(), point[2]);
        }
        points.push_back(vehiclePoint);
        lines.addStrip(points, kColor, width);
        return;
    }

    // Centred moving average (wider for altitude, which is noisier); near the ends the window narrows on both
    // sides so it stays centred and never lags behind the vehicle
    const int n = static_cast<int>(_points.size());
    std::vector<double> sumX(n + 1, 0.0);
    std::vector<double> sumY(n + 1, 0.0);
    std::vector<double> sumZ(n + 1, 0.0);
    for (int i = 0; i < n; i++) {
        sumX[i + 1] = sumX[i] + (_points[i][0] - frame.originEasting());
        sumY[i + 1] = sumY[i] + (_points[i][1] - frame.originNorthing());
        sumZ[i + 1] = sumZ[i] + _points[i][2];
    }
    std::vector<std::array<double, 3>> smoothed(n);
    for (int i = 0; i < n; i++) {
        const int toEnd = std::min(i, n - 1 - i);
        const int half = std::min(kWindowXY / 2, toEnd);
        const int halfZ = std::min(kWindowZ / 2, toEnd);
        smoothed[i] = {(sumX[i + half + 1] - sumX[i - half]) / (2 * half + 1),
                       (sumY[i + half + 1] - sumY[i - half]) / (2 * half + 1),
                       (sumZ[i + halfZ + 1] - sumZ[i - halfZ]) / (2 * halfZ + 1)};
    }

    // The newest points have no later ones to average with, so draw up to where the average is whole, then bend
    // into the vehicle on a curve that leaves the trail along it and arrives along the vehicle's velocity
    const int end = std::max(0, n - 1 - kWindowZ / 2);
    points.reserve(end + 1 + kTailSegments);
    for (int i = 0; i <= end; i++) {
        points.emplace_back(smoothed[i][0], smoothed[i][1], smoothed[i][2]);
    }

    const double x0 = smoothed[end][0];
    const double y0 = smoothed[end][1];
    const double z0 = smoothed[end][2];
    const double x1 = vehicle.x;
    const double y1 = vehicle.y;
    const double z1 = vehicle.z;
    const double dx = x1 - x0;
    const double dy = y1 - y0;
    const double dz = z1 - z0;
    const double chord = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (chord < 0.01) {
        points.push_back(vehiclePoint);
    } else {
        const int back = std::max(0, end - 5);
        double ax = x0 - smoothed[back][0];
        double ay = y0 - smoothed[back][1];
        double az = z0 - smoothed[back][2];
        scaleTo(ax, ay, az, chord, dx, dy, dz);

        // Velocity in scene axes: east, north, up
        double bx = velocityEast;
        double by = velocityNorth;
        double bz = -velocityDown;
        if (std::sqrt(bx * bx + by * by + bz * bz) < 0.5) {
            bx = dx;
            by = dy;
            bz = dz;
        }
        scaleTo(bx, by, bz, chord, dx, dy, dz);

        // Cubic Hermite from the smoothed trail to the vehicle
        for (int k = 1; k <= kTailSegments; k++) {
            const double t = static_cast<double>(k) / kTailSegments;
            const double t2 = t * t;
            const double t3 = t2 * t;
            const double h00 = 2 * t3 - 3 * t2 + 1;
            const double h10 = t3 - 2 * t2 + t;
            const double h01 = -2 * t3 + 3 * t2;
            const double h11 = t3 - t2;
            points.emplace_back(h00 * x0 + h10 * ax + h01 * x1 + h11 * bx, h00 * y0 + h10 * ay + h01 * y1 + h11 * by,
                                h00 * z0 + h10 * az + h01 * z1 + h11 * bz);
        }
    }
    lines.addStrip(points, kColor, width);
}

}  // namespace Map3D
