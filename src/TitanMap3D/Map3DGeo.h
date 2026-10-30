#pragma once

#include <QtCore/QHashFunctions>
#include <QtGui/QMatrix4x4>

/// Geometry shared by the Titan 3D map, ported from Mission Planner's Map3D (TitanPlanner): UTM, the scene's
/// world frame, Web Mercator tile addressing and frustum tests. Pure maths, safe on any thread.
namespace Map3D {

/// WGS84 transverse Mercator (UTM) using the Krueger series, accurate to well under a millimetre inside a zone.
namespace Utm {

/// Zone 1..60, negative for the southern hemisphere.
int zoneFor(double lat, double lng);
double centralMeridianDegrees(int zone);
/// Degrees of longitude between a position and a zone's central meridian, across the antimeridian too.
double longitudeFromCentralMeridian(double lng, int zone);
/// The hemisphere comes from the zone's sign, so points just across the equator stay continuous.
void forward(double lat, double lng, int zone, double& easting, double& northing);
void inverse(double easting, double northing, int zone, double& lat, double& lng);

}  // namespace Utm

/// The scene's coordinate system: metres east/north of an origin in one UTM zone (x east, y north, z metres
/// AMSL). Moving the origin keeps float precision; only a zone change invalidates built terrain.
class WorldFrame
{
public:
    static constexpr double kRebaseDistance = 10000;
    static constexpr double kZoneChangeLongitude = 4.0;

    int zone() const { return _zone; }

    double originEasting() const { return _originEasting; }

    double originNorthing() const { return _originNorthing; }

    int version() const { return _version; }

    int zoneVersion() const { return _zoneVersion; }

    bool isValid() const { return _zone != 0; }

    /// Keeps the origin near the given position. Returns true if the origin or zone changed.
    bool update(double lat, double lng);

    void toAbsolute(double lat, double lng, double& easting, double& northing) const;
    void toLocal(double lat, double lng, double& x, double& y) const;
    void toLatLng(double x, double y, double& lat, double& lng) const;

private:
    void _setZoneAndOrigin(double lat, double lng);

    int _zone = 0;
    double _originEasting = 0;
    double _originNorthing = 0;
    int _version = 0;
    int _zoneVersion = 0;
};

/// A Web Mercator (XYZ) tile address; y grows southwards.
struct TileId
{
    int x = 0;
    int y = 0;
    int z = 0;

    TileId parent() const { return {x >> 1, y >> 1, z - 1}; }

    /// Child 0..3: bit 0 picks east, bit 1 picks south.
    TileId child(int index) const { return {x * 2 + (index & 1), y * 2 + (index >> 1), z + 1}; }

    TileId ancestorAt(int zoom) const
    {
        const int shift = z - zoom;
        return {x >> shift, y >> shift, zoom};
    }

    friend bool operator==(const TileId& a, const TileId& b) { return a.x == b.x && a.y == b.y && a.z == b.z; }

    friend bool operator!=(const TileId& a, const TileId& b) { return !(a == b); }

    friend size_t qHash(const TileId& id, size_t seed = 0) noexcept { return qHashMulti(seed, id.x, id.y, id.z); }
};

/// Spherical Web Mercator maths, matching GMap.NET's MercatorProjection.
namespace TileMath {

constexpr int kTileSize = 256;
constexpr double kEarthRadius = 6378137.0;
constexpr double kMaxLatitude = 85.05112878;

inline int tileCount(int zoom)
{
    return 1 << zoom;
}

double tileXToLng(double x, int zoom);
double tileYToLat(double y, int zoom);
void latLngToTile(double lat, double lng, int zoom, double& x, double& y);
/// Ground size of a tile edge in metres at the given latitude.
double tileSizeMeters(double lat, int zoom);
double centerLat(const TileId& id);
double centerLng(const TileId& id);
bool isValid(const TileId& id);

}  // namespace TileMath

/// View frustum planes of a column-vector view-projection matrix (OpenGL clip conventions).
class Frustum
{
public:
    static Frustum fromViewProjection(const QMatrix4x4& m);

    /// True unless the box is entirely outside one of the planes.
    bool intersectsBox(double minX, double minY, double minZ, double maxX, double maxY, double maxZ) const;

private:
    double _planes[24] = {};
};

/// Mission Planner's PointLatLngAlt.newpos: a position extrapolated along a bearing (degrees) for a distance (m).
void newPosition(double lat, double lng, double bearing, double distance, double& outLat, double& outLng);

/// Great-circle distance in metres, as Mission Planner's PointLatLngAlt.GetDistance.
double distanceMeters(double lat1, double lng1, double lat2, double lng2);

}  // namespace Map3D
