#pragma once

#include <vector>

#include <QtCore/QByteArray>

/// The pure side of the 3D map's clouds, ported from Mission Planner's CloudField: a tileable cloud-density
/// texture, the layers it is drawn in, and how high they sit (well above the vehicle and the ground).
namespace Map3D::Clouds {

constexpr double kClearanceAboveVehicle = 1000;
constexpr double kMinimumAboveGround = 1500;

/// One sheet of cloud, from the lowest up.
struct Layer
{
    double height;       ///< Metres above the lowest layer
    double scaleX;       ///< Metres per texture tile east-west
    double scaleY;       ///< Metres per texture tile north-south
    double detailScale;  ///< The finer second sampling, as a fraction of the scale
    float coverage;      ///< Density below which sky shows through at full cloud density
    float softness;      ///< How soft the cloud edges are, in density
    float opacity;
    double windX;        ///< Drift in m/s east
    double windY;        ///< Drift in m/s north
};

/// Cumulus, then patchy mid-level cloud, then thin high streaks.
inline constexpr Layer kLayers[] = {
    {0, 14000, 14000, 0.29, 0.451f, 0.11f, 0.93f, 7, 2},
    {1800, 22000, 22000, 0.33, 0.500f, 0.16f, 0.62f, 11, -3},
    {5000, 52000, 13000, 0.21, 0.494f, 0.22f, 0.42f, 19, 6},
};
constexpr int kLayerCount = 3;

constexpr double kWeatherScale = 0.17;
constexpr float kWeatherStrength = 0.12f;
constexpr float kDetailWeight = 0.25f;
constexpr double kDetailDrift = 1.4;
constexpr double kWeatherDrift = 0.5;
constexpr double kFadeStart = 0.35;
constexpr double kFadeEnd = 0.85;
constexpr float kLitColor[3] = {0.98f, 0.98f, 0.99f};
constexpr float kShadeColor[3] = {0.64f, 0.67f, 0.72f};
constexpr int kTextureSize = 256;
constexpr int kTextureSeed = 1729;
constexpr int kMaxDensity = 100;
constexpr int kDefaultDensity = 50;

/// Each layer's coverage threshold for a cloud density from 0 to kMaxDensity, measured on the texture.
std::vector<float> coverages(const std::vector<float>& texture, int size, int density);

/// The share of sky a layer covers with a coverage threshold.
double cover(const Layer& layer, const std::vector<float>& texture, int size, double coverage);

/// The lowest layer's altitude AMSL for the ground and the vehicle (or camera) altitude below it.
double baseAltitude(double groundHeight, double vehicleAltitude);

/// A size x size tileable density texture, 0 to 1, as single-channel bytes; density receives the values.
QByteArray texture(int size, int seed, std::vector<float>& density);

}  // namespace Map3D::Clouds
