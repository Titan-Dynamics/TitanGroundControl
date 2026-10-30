#include "Map3DTest.h"

#include <algorithm>
#include <cmath>

#include <QtCore/QBuffer>
#include <QtGui/QImage>

#include "Map3DClouds.h"
#include "Map3DGeo.h"
#include "Map3DModel.h"
#include "Map3DOverlays.h"
#include "Map3DScene.h"
#include "Map3DTerrain.h"
#include "Map3DTileLoader.h"

using namespace Map3D;

namespace {

// Van Nuys airport
constexpr double kLat = 34.2098;
constexpr double kLng = -118.4900;

Fix fixAt(double lat, double lng, double alt, double time)
{
    Fix fix;
    fix.lat = lat;
    fix.lng = lng;
    fix.alt = alt;
    fix.timeSeconds = time;
    return fix;
}

}  // namespace

void Map3DTest::_utmRoundTrip()
{
    const int zone = Utm::zoneFor(kLat, kLng);
    QCOMPARE(zone, 11);
    QCOMPARE(Utm::zoneFor(-33.9, 151.2), -56);

    double easting = 0;
    double northing = 0;
    Utm::forward(kLat, kLng, zone, easting, northing);
    // UTM zone 11 coordinates of the point, to a metre
    QVERIFY(std::abs(easting - 362000) < 2000);
    QVERIFY(std::abs(northing - 3786000) < 2000);

    double lat = 0;
    double lng = 0;
    Utm::inverse(easting, northing, zone, lat, lng);
    QVERIFY(std::abs(lat - kLat) < 1e-8);
    QVERIFY(std::abs(lng - kLng) < 1e-8);
}

void Map3DTest::_worldFrameRebasesAndChangesZone()
{
    WorldFrame frame;
    QVERIFY(frame.update(kLat, kLng));
    QCOMPARE(frame.zoneVersion(), 1);
    QVERIFY(!frame.update(kLat + 0.01, kLng));  // ~1 km: origin stays

    double north = 0;
    double east = 0;
    newPosition(kLat, kLng, 0, 12000, north, east);
    QVERIFY(frame.update(north, east));  // > 10 km: origin moves, zone stays
    QCOMPARE(frame.version(), 2);
    QCOMPARE(frame.zoneVersion(), 1);

    QVERIFY(frame.update(kLat, kLng + 5.5));  // > 4 degrees from the central meridian: next zone
    QCOMPARE(frame.zone(), 12);
    QCOMPARE(frame.zoneVersion(), 2);
}

void Map3DTest::_kalmanConvergesToStep()
{
    KalmanFilter filter(0.02, 1.5);
    filter.reset(0);
    double value = 0;
    double peak = 0;
    for (int i = 1; i <= 600; i++) {
        value = filter.update(20, i * 0.016);
        peak = std::max(peak, value);
    }
    QVERIFY2(std::abs(value - 20) < 0.1, qPrintable(QString::number(value)));
    QVERIFY2(peak < 21.5, qPrintable(QString::number(peak)));
}

void Map3DTest::_kalmanAngleTakesShortWay()
{
    KalmanFilter filter(0.015, 2.5);
    filter.reset(350);
    for (int i = 1; i <= 600; i++) {
        const double heading = filter.updateAngle(10, i * 0.016);
        QVERIFY2((heading >= 340) || (heading <= 20), qPrintable(QString::number(heading)));
    }
    QVERIFY(std::abs(filter.updateAngle(10, 601 * 0.016) - 10) < 0.5);
}

void Map3DTest::_trackerJumpsWhenDisarmed()
{
    WorldFrame frame;
    frame.update(kLat, kLng);
    VehicleTracker tracker;
    tracker.update(frame, fixAt(kLat, kLng, 300, 0), 0, 0, 90, 200, 0, 0, true, false, 0);

    double lat = 0;
    double lng = 0;
    newPosition(kLat, kLng, 90, 50, lat, lng);
    tracker.update(frame, fixAt(lat, lng, 300, 0.1), 0, 0, 90, 200, 0, 0, true, false, 0.1);

    double x = 0;
    double y = 0;
    frame.toLocal(lat, lng, x, y);
    QVERIFY(std::hypot(tracker.x - x, tracker.y - y) < 0.01);
}

void Map3DTest::_trackerSmoothsWhenArmed()
{
    WorldFrame frame;
    frame.update(kLat, kLng);
    VehicleTracker tracker;
    tracker.update(frame, fixAt(kLat, kLng, 300, 0), 0, 0, 90, 200, 0, 0, true, true, 0);

    double lat = 0;
    double lng = 0;
    newPosition(kLat, kLng, 90, 50, lat, lng);
    tracker.update(frame, fixAt(lat, lng, 300, 0.016), 0, 0, 90, 200, 0, 0, true, true, 0.016);

    double x = 0;
    double y = 0;
    frame.toLocal(lat, lng, x, y);
    const double moved = std::hypot(tracker.x - (x - 50), tracker.y - y);
    QVERIFY2(moved > 1 && moved < 49, qPrintable(QString::number(moved)));

    // Never below a metre over the ground
    tracker.update(frame, fixAt(lat, lng, 150, 0.032), 0, 0, 90, 200, 0, 0, false, true, 0.032);
    QCOMPARE(tracker.z, 201.0);
}

void Map3DTest::_chaseCameraSitsBehindVehicle()
{
    VehicleTracker vehicle;
    vehicle.x = 100;
    vehicle.y = 200;
    vehicle.z = 300;
    vehicle.yaw = 90;  // east

    CameraRig camera;
    camera.chase(vehicle, 0.8, 0.2, 0);
    QVERIFY(std::abs(camera.x - 99.2) < 1e-6);
    QVERIFY(std::abs(camera.y - 200) < 1e-6);
    QVERIFY(std::abs(camera.z - 300.2) < 1e-6);
    QCOMPARE(camera.lookX, 100.0);

    // 90 degrees around puts the camera on the vehicle's right
    camera.chase(vehicle, 0.8, 0.2, 90);
    QVERIFY(std::abs(camera.x - 100) < 1e-6);
    QVERIFY(std::abs(camera.y - 200.8) < 1e-6);

    double fx = 0;
    double fy = 0;
    double fz = 0;
    camera.chase(vehicle, 0.8, 0, 0);
    camera.forward(fx, fy, fz);
    QVERIFY(std::abs(fx - 1) < 1e-6);
}

void Map3DTest::_firstPersonStaysAboveGround()
{
    VehicleTracker vehicle;
    vehicle.z = 100;
    vehicle.yaw = 0;
    vehicle.pitch = 10;
    vehicle.roll = 20;

    CameraRig camera;
    camera.firstPerson(vehicle, 150);
    QCOMPARE(camera.z, 150.0);
    QCOMPARE(camera.roll, 20.0f);
    QVERIFY(camera.lookY > camera.y);
    QVERIFY(camera.lookZ > camera.z);
}

void Map3DTest::_quadtreeRequestsRootsFirst()
{
    WorldFrame frame;
    frame.update(kLat, kLng);
    TerrainQuadtree quadtree(frame);

    SelectionParams params;
    params.cameraZ = 400;
    params.cameraLat = kLat;
    params.cameraLng = kLng;
    params.viewRadius = 30000;
    params.referenceHeight = 200;
    quadtree.select(params, 0);

    QVERIFY(quadtree.drawList().isEmpty());
    QVERIFY(!quadtree.requests().isEmpty());
    QCOMPARE(quadtree.requests().first().id.z, TerrainQuadtree::kRootZoom);
    QCOMPARE(quadtree.requests().first().kind, TileJobKind::Full);
    for (int i = 1; i < quadtree.requests().size(); i++) {
        QVERIFY(quadtree.requests()[i - 1].priority <= quadtree.requests()[i].priority);
    }
}

void Map3DTest::_maxZoomFollowsHeight()
{
    // Never finer than Google's satellite imagery, even on the ground
    QCOMPARE(SelectionParams::kMaxImageryZoom, 22);
    QCOMPARE(SelectionParams::maxZoomFor(1, 20), 22);
    QCOMPARE(SelectionParams::maxZoomFor(2000, 22), 20);

    // Within 10% of the 500 m boundary the level holds
    QCOMPARE(SelectionParams::maxZoomFor(520, 22), 22);
    QCOMPARE(SelectionParams::maxZoomFor(480, 20), 20);
}

void Map3DTest::_meshHasSkirtsBelowEdges()
{
    const TileId id{2810, 6533, 14};
    const int gridN = TileMeshBuilder::gridSizeFor(id);
    std::vector<double> rowLats;
    std::vector<double> colLngs;
    TileMeshBuilder::gridLatLngs(id, gridN, rowLats, colLngs);
    auto heights = std::make_shared<std::vector<float>>(static_cast<size_t>(gridN + 1) * (gridN + 1), 250.0f);
    const TileMesh mesh =
        TileMeshBuilder::build(id, gridN, rowLats, colLngs, heights, Utm::zoneFor(rowLats[0], colLngs[0]));

    QCOMPARE(static_cast<int>(mesh.vertices.size()),
             TileMeshBuilder::vertexCount(gridN) * TileMeshBuilder::kFloatsPerVertex);
    QCOMPARE(static_cast<int>(TileMeshBuilder::buildIndices(gridN).size()), TileMeshBuilder::indexCount(gridN));
    QCOMPARE(mesh.maxHeight, 250.0f);
    QVERIFY(mesh.minHeight <= 245.0f);
    // The last vertex is a skirt vertex, hanging below the edge
    QVERIFY(mesh.vertices[mesh.vertices.size() - 3] < 250.0f);
}

void Map3DTest::_gridSizeFollowsDemResolution()
{
    QCOMPARE(TileMeshBuilder::gridSizeFor(TileId{87, 205, 9}), TileMeshBuilder::kMaxGrid);
    QCOMPARE(TileMeshBuilder::gridSizeFor(TileId{1438960, 3344970, 22}), TileMeshBuilder::kMinGrid);
}

void Map3DTest::_cloudCoverageFollowsDensity()
{
    std::vector<float> density;
    const QByteArray pixels = Clouds::texture(64, Clouds::kTextureSeed, density);
    QCOMPARE(pixels.size(), 64 * 64);

    const auto share = [&density](int cloudDensity) {
        const std::vector<float> coverages = Clouds::coverages(density, 64, cloudDensity);
        return Clouds::cover(Clouds::kLayers[0], density, 64, coverages[0]);
    };
    const double none = share(0);
    const double half = share(50);
    const double full = share(100);
    QVERIFY2(none < 0.01, qPrintable(QString::number(none)));
    QVERIFY(half > none);
    QVERIFY(full > half);
}

void Map3DTest::_elevationDecodesTerrarium()
{
    // height = R * 256 + G + B / 256 - 32768: 32768 + 1234.5 m
    QImage image(4, 4, QImage::Format_RGB32);
    const int value = 32768 * 256 + static_cast<int>(1234.5 * 256);
    image.fill(qRgb((value >> 16) & 0xff, (value >> 8) & 0xff, value & 0xff));
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    QVERIFY(image.save(&buffer, "PNG"));

    const auto tile = ElevationCache::decode(TileId{0, 0, 1}, png);
    QVERIFY(tile);
    QCOMPARE(tile->size, 4);
    QCOMPARE(tile->heights.front(), 1234.5f);
}

void Map3DTest::_elevationTileServesSeveralTerrainTiles()
{
    // Three levels up: one elevation tile covers 8 x 8 terrain tiles
    const TileId id{2810, 6533, 14};
    const TileId elevation = ElevationCache::elevationTileFor(id);
    QCOMPARE(elevation.z, 11);
    QCOMPARE(elevation.x, id.x / 8);
    QCOMPARE(elevation.y, id.y / 8);
    const TileId lastInBlock{(id.x / 8) * 8 + 7, (id.y / 8) * 8 + 7, 14};
    QVERIFY(ElevationCache::elevationTileFor(lastInBlock) == elevation);

    // Never finer than Terrarium's last level, never above the root
    QCOMPARE(ElevationCache::elevationTileFor(TileId{1438960, 3344970, 22}).z, kMaxElevationZoom);
    QCOMPARE(ElevationCache::elevationTileFor(TileId{1, 1, 2}).z, 0);
}

void Map3DTest::_smoothedTrailEndsAtVehicle()
{
    WorldFrame frame;
    frame.update(kLat, kLng);
    VehicleTracker vehicle;
    Trail trail;
    for (int i = 0; i < 200; i++) {
        vehicle.x = i * 2.0;
        vehicle.y = 0;
        vehicle.z = 100;
        trail.record(frame, vehicle, true, true, 3000);
    }
    QVERIFY(trail.size() > 100);

    LineList lines;
    trail.draw(lines, frame, vehicle, true, true, 0, 20, 0, 5);
    QVERIFY(!lines.isEmpty());
    // The last segment's far end is the vehicle
    const std::vector<float>& data = lines.data();
    const size_t last = data.size() - LineList::kFloatsPerVertex;
    QCOMPARE(data[last + 3], static_cast<float>(vehicle.x));
    QCOMPARE(data[last + 5], static_cast<float>(vehicle.z));
}

void Map3DTest::_modelLoadsWithNoseForward()
{
    const std::vector<float> plane = Model::loadStl(Model::defaultModelPath(MAV_TYPE_FIXED_WING, false));
    QVERIFY(plane.size() > 1000);
    QCOMPARE(plane.size() % 18, size_t(0));

    float minX = 1e9f;
    float maxX = -1e9f;
    float minY = 1e9f;
    float maxY = -1e9f;
    for (size_t i = 0; i < plane.size(); i += 6) {
        minX = std::min(minX, plane[i]);
        maxX = std::max(maxX, plane[i]);
        minY = std::min(minY, plane[i + 1]);
        maxY = std::max(maxY, plane[i + 1]);
    }
    // Wingspan across x (about a metre, in millimetres), the shorter fuselage along y
    QVERIFY2(maxX - minX > 900, qPrintable(QString::number(maxX - minX)));
    QVERIFY(maxY - minY < maxX - minX);

    QCOMPARE(Model::defaultModelPath(MAV_TYPE_HEXAROTOR, false), Model::defaultModelPath(MAV_TYPE_QUADROTOR, false));
    QVERIFY(Model::defaultModelPath(MAV_TYPE_FIXED_WING, true).endsWith(QStringLiteral("quadplane.stl")));
    QVERIFY(Model::defaultModelPath(MAV_TYPE_VTOL_TILTROTOR, false).endsWith(QStringLiteral("quadplane.stl")));
}

UT_REGISTER_TEST(Map3DTest, TestLabel::Unit)
