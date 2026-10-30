#include "Map3DGeo.h"

#include <algorithm>
#include <cmath>

#include <QtCore/QtMath>

namespace Map3D {

namespace {

constexpr double kSemiMajorAxis = 6378137.0;
constexpr double kFlattening = 1 / 298.257223563;
constexpr double kScaleFactor = 0.9996;
constexpr double kFalseEasting = 500000.0;
constexpr double kFalseNorthingSouth = 10000000.0;
constexpr double kDegToRad = M_PI / 180.0;

struct UtmConstants
{
    double n;
    double rectifyingRadius;
    double conformalFactor;
    double alpha[3];
    double beta[3];
    double delta[3];
};

const UtmConstants& utmConstants()
{
    static const UtmConstants constants = [] {
        UtmConstants c{};
        const double n = kFlattening / (2 - kFlattening);
        c.n = n;
        c.rectifyingRadius = kSemiMajorAxis / (1 + n) * (1 + n * n / 4 + n * n * n * n / 64);
        c.conformalFactor = 2 * std::sqrt(n) / (1 + n);
        c.alpha[0] = n / 2 - 2.0 / 3 * n * n + 5.0 / 16 * n * n * n;
        c.alpha[1] = 13.0 / 48 * n * n - 3.0 / 5 * n * n * n;
        c.alpha[2] = 61.0 / 240 * n * n * n;
        c.beta[0] = n / 2 - 2.0 / 3 * n * n + 37.0 / 96 * n * n * n;
        c.beta[1] = 1.0 / 48 * n * n + 1.0 / 15 * n * n * n;
        c.beta[2] = 17.0 / 480 * n * n * n;
        c.delta[0] = 2 * n - 2.0 / 3 * n * n - 2 * n * n * n;
        c.delta[1] = 7.0 / 3 * n * n - 8.0 / 5 * n * n * n;
        c.delta[2] = 56.0 / 15 * n * n * n;
        return c;
    }();
    return constants;
}

double centralMeridian(int zone)
{
    return Utm::centralMeridianDegrees(zone) * kDegToRad;
}

}  // namespace

// ---------------------------------------------------------------------------
// UTM

int Utm::zoneFor(double lat, double lng)
{
    int zone = static_cast<int>((lng + 186.0) / 6.0);
    zone = std::clamp(zone, 1, 60);
    return lat < 0 ? -zone : zone;
}

double Utm::centralMeridianDegrees(int zone)
{
    return std::abs(zone) * 6 - 183;
}

double Utm::longitudeFromCentralMeridian(double lng, int zone)
{
    double delta = std::fmod(lng - centralMeridianDegrees(zone), 360.0);
    if (delta > 180) {
        delta -= 360;
    }
    if (delta < -180) {
        delta += 360;
    }
    return std::abs(delta);
}

void Utm::forward(double lat, double lng, int zone, double& easting, double& northing)
{
    const UtmConstants& c = utmConstants();
    const double phi = lat * kDegToRad;
    const double lambda = lng * kDegToRad - centralMeridian(zone);

    const double sinPhi = std::sin(phi);
    const double t = std::sinh(std::atanh(sinPhi) - c.conformalFactor * std::atanh(c.conformalFactor * sinPhi));
    const double xiPrime = std::atan2(t, std::cos(lambda));
    const double etaPrime = std::atanh(std::sin(lambda) / std::sqrt(1 + t * t));

    double xi = xiPrime;
    double eta = etaPrime;
    for (int j = 0; j < 3; j++) {
        const double k = 2 * (j + 1);
        xi += c.alpha[j] * std::sin(k * xiPrime) * std::cosh(k * etaPrime);
        eta += c.alpha[j] * std::cos(k * xiPrime) * std::sinh(k * etaPrime);
    }

    easting = kFalseEasting + kScaleFactor * c.rectifyingRadius * eta;
    northing = (zone < 0 ? kFalseNorthingSouth : 0) + kScaleFactor * c.rectifyingRadius * xi;
}

void Utm::inverse(double easting, double northing, int zone, double& lat, double& lng)
{
    const UtmConstants& c = utmConstants();
    const double xi = (northing - (zone < 0 ? kFalseNorthingSouth : 0)) / (kScaleFactor * c.rectifyingRadius);
    const double eta = (easting - kFalseEasting) / (kScaleFactor * c.rectifyingRadius);

    double xiPrime = xi;
    double etaPrime = eta;
    for (int j = 0; j < 3; j++) {
        const double k = 2 * (j + 1);
        xiPrime -= c.beta[j] * std::sin(k * xi) * std::cosh(k * eta);
        etaPrime -= c.beta[j] * std::cos(k * xi) * std::sinh(k * eta);
    }

    const double chi = std::asin(std::sin(xiPrime) / std::cosh(etaPrime));
    double phi = chi;
    for (int j = 0; j < 3; j++) {
        phi += c.delta[j] * std::sin(2 * (j + 1) * chi);
    }

    lat = phi / kDegToRad;
    lng = (centralMeridian(zone) + std::atan2(std::sinh(etaPrime), std::cos(xiPrime))) / kDegToRad;
}

// ---------------------------------------------------------------------------
// WorldFrame

bool WorldFrame::update(double lat, double lng)
{
    if (!isValid()) {
        _setZoneAndOrigin(lat, lng);
        return true;
    }

    // Measured from the zone itself, not the origin: the origin follows the vehicle every 10 km
    if ((Utm::longitudeFromCentralMeridian(lng, _zone) > kZoneChangeLongitude) &&
        (std::abs(Utm::zoneFor(lat, lng)) != std::abs(_zone))) {
        _setZoneAndOrigin(lat, lng);
        return true;
    }

    double e = 0;
    double n = 0;
    Utm::forward(lat, lng, _zone, e, n);
    if (std::hypot(e - _originEasting, n - _originNorthing) > kRebaseDistance) {
        _originEasting = e;
        _originNorthing = n;
        _version++;
        return true;
    }
    return false;
}

void WorldFrame::_setZoneAndOrigin(double lat, double lng)
{
    _zone = Utm::zoneFor(lat, lng);
    Utm::forward(lat, lng, _zone, _originEasting, _originNorthing);
    _version++;
    _zoneVersion++;
}

void WorldFrame::toAbsolute(double lat, double lng, double& easting, double& northing) const
{
    Utm::forward(lat, lng, _zone, easting, northing);
}

void WorldFrame::toLocal(double lat, double lng, double& x, double& y) const
{
    double e = 0;
    double n = 0;
    Utm::forward(lat, lng, _zone, e, n);
    x = e - _originEasting;
    y = n - _originNorthing;
}

void WorldFrame::toLatLng(double x, double y, double& lat, double& lng) const
{
    Utm::inverse(x + _originEasting, y + _originNorthing, _zone, lat, lng);
}

// ---------------------------------------------------------------------------
// TileMath

double TileMath::tileXToLng(double x, int zoom)
{
    return x / tileCount(zoom) * 360.0 - 180.0;
}

double TileMath::tileYToLat(double y, int zoom)
{
    const double n = M_PI - 2.0 * M_PI * y / tileCount(zoom);
    return 180.0 / M_PI * std::atan(std::sinh(n));
}

void TileMath::latLngToTile(double lat, double lng, int zoom, double& x, double& y)
{
    lat = std::clamp(lat, -kMaxLatitude, kMaxLatitude);
    const double count = tileCount(zoom);
    x = (lng + 180.0) / 360.0 * count;
    const double latRad = lat * M_PI / 180.0;
    y = (1.0 - std::log(std::tan(latRad) + 1.0 / std::cos(latRad)) / M_PI) / 2.0 * count;
}

double TileMath::tileSizeMeters(double lat, int zoom)
{
    return 2 * M_PI * kEarthRadius * std::cos(lat * M_PI / 180.0) / tileCount(zoom);
}

double TileMath::centerLat(const TileId& id)
{
    return tileYToLat(id.y + 0.5, id.z);
}

double TileMath::centerLng(const TileId& id)
{
    return tileXToLng(id.x + 0.5, id.z);
}

bool TileMath::isValid(const TileId& id)
{
    const int count = tileCount(id.z);
    return id.z >= 0 && id.x >= 0 && id.y >= 0 && id.x < count && id.y < count;
}

// ---------------------------------------------------------------------------
// Frustum

Frustum Frustum::fromViewProjection(const QMatrix4x4& m)
{
    // clip = m * v: each plane is the last row plus or minus another row
    Frustum f;
    const QVector4D r0 = m.row(0);
    const QVector4D r1 = m.row(1);
    const QVector4D r2 = m.row(2);
    const QVector4D r3 = m.row(3);
    const QVector4D planes[6] = {r3 + r0, r3 - r0, r3 + r1, r3 - r1, r3 + r2, r3 - r2};
    for (int p = 0; p < 6; p++) {
        f._planes[p * 4] = planes[p].x();
        f._planes[p * 4 + 1] = planes[p].y();
        f._planes[p * 4 + 2] = planes[p].z();
        f._planes[p * 4 + 3] = planes[p].w();
    }
    return f;
}

bool Frustum::intersectsBox(double minX, double minY, double minZ, double maxX, double maxY, double maxZ) const
{
    for (int p = 0; p < 6; p++) {
        const double a = _planes[p * 4];
        const double b = _planes[p * 4 + 1];
        const double c = _planes[p * 4 + 2];
        const double d = _planes[p * 4 + 3];
        // The box corner furthest along the plane normal
        const double x = a >= 0 ? maxX : minX;
        const double y = b >= 0 ? maxY : minY;
        const double z = c >= 0 ? maxZ : minZ;
        if (a * x + b * y + c * z + d < 0) {
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------

void newPosition(double lat, double lng, double bearing, double distance, double& outLat, double& outLng)
{
    constexpr double kRadiusOfEarth = 6378100.0;
    const double lat1 = lat * kDegToRad;
    const double lon1 = lng * kDegToRad;
    const double brng = bearing * kDegToRad;
    const double dr = distance / kRadiusOfEarth;

    const double lat2 = std::asin(std::sin(lat1) * std::cos(dr) + std::cos(lat1) * std::sin(dr) * std::cos(brng));
    const double lon2 = lon1 + std::atan2(std::sin(brng) * std::sin(dr) * std::cos(lat1),
                                          std::cos(dr) - std::sin(lat1) * std::sin(lat2));
    outLat = lat2 / kDegToRad;
    outLng = lon2 / kDegToRad;
}

double distanceMeters(double lat1, double lng1, double lat2, double lng2)
{
    const double a1 = lat1 * kDegToRad;
    const double a2 = lat2 * kDegToRad;
    const double dLng = (lng2 - lng1) * kDegToRad;
    const double dLat = a2 - a1;
    const double h =
        std::pow(std::sin(dLat / 2.0), 2.0) + std::cos(a1) * std::cos(a2) * std::pow(std::sin(dLng / 2.0), 2.0);
    return 6371000.0 * 2.0 * std::atan2(std::sqrt(h), std::sqrt(1.0 - h));
}

}  // namespace Map3D
