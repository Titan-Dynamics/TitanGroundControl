#pragma once

#include <array>
#include <vector>

#include <QtGui/QVector3D>

#include "Map3DScene.h"

/// CPU-side geometry of the 3D map's overlays, ported from Mission Planner's VehicleOverlay and AdsbOverlay:
/// the indicator lines, the predicted turn, the flown trail and ADS-B circles. Render thread only.
namespace Map3D {

using Rgba = std::array<float, 4>;

/// Line segments of any width, as the line shader expects them: six vertices (two triangles) per segment.
class LineList
{
public:
    /// pointA(3) pointB(3) corner(2) color(4) width(1)
    static constexpr int kFloatsPerVertex = 13;

    void clear() { _data.clear(); }

    bool isEmpty() const { return _data.empty(); }

    int vertexCount() const { return static_cast<int>(_data.size() / kFloatsPerVertex); }

    const std::vector<float>& data() const { return _data; }

    void addSegment(const QVector3D& a, const QVector3D& b, const Rgba& color, float width);
    void addStrip(const std::vector<QVector3D>& points, const Rgba& color, float width);

private:
    void _vertex(const QVector3D& a, const QVector3D& b, float side, float end, const Rgba& color, float width);

    std::vector<float> _data;
};

namespace Overlays {

constexpr double kLineLength = 100;
constexpr double kTurnArcLength = 200;
constexpr int kTurnSegments = 50;

/// A line from the vehicle along a bearing (degrees), climbing with pitch (radians).
void addDirection(LineList& lines, const VehicleTracker& vehicle, double bearing, double pitch, const Rgba& color,
                  float width);

/// The path the vehicle will fly at its current turn radius (negative turns left), climbing with pitch (radians).
void addTurnArc(LineList& lines, const VehicleTracker& vehicle, double radius, double pitch, float width);

/// A circle facing the camera, as separate segments, for ADS-B traffic.
void addBillboardCircle(LineList& lines, const QVector3D& center, double radius, double viewX, double viewY,
                        double viewZ, const Rgba& color, float width);

/// ADS-B colour by distance: red close, yellow, green far.
Rgba adsbColor(double distance);

}  // namespace Overlays

/// The flown trail: recorded in absolute UTM while armed, drawn raw or smoothed with a curve into the vehicle.
class Trail
{
public:
    void record(const WorldFrame& frame, const VehicleTracker& vehicle, bool enabled, bool armed, int maxPoints);
    /// Velocity is the vehicle's in m/s (north, east, down), the direction a smoothed trail arrives along.
    void draw(LineList& lines, const WorldFrame& frame, const VehicleTracker& vehicle, bool armed, bool smooth,
              double velocityNorth, double velocityEast, double velocityDown, float width) const;

    int size() const { return static_cast<int>(_points.size()); }

    static constexpr int kStabilityFrames = 30;
    static constexpr int kWindowXY = 20;
    static constexpr int kWindowZ = 60;
    static constexpr int kTailSegments = 24;

private:
    std::vector<std::array<double, 3>> _points;
    int _zoneVersion = -1;
    int _stableFrames = 0;
};

}  // namespace Map3D
