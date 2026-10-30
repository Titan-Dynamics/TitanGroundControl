#include "Map3DTerrain.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Map3D {

// ---------------------------------------------------------------------------
// SelectionParams

int SelectionParams::maxZoomFor(double heightAboveGround, int current)
{
    const auto band = [](double height) { return height < 500 ? kMaxImageryZoom : 20; };

    // Only change once the height is clearly past the boundary between the two levels
    const int wanted = band(heightAboveGround);
    if (wanted == current) {
        return current;
    }
    return ((band(heightAboveGround * 1.1) == wanted) && (band(heightAboveGround / 1.1) == wanted)) ? wanted : current;
}

// ---------------------------------------------------------------------------
// TerrainQuadtree

TerrainTile* TerrainQuadtree::find(const TileId& id)
{
    const auto it = _tiles.find(id);
    return it == _tiles.end() ? nullptr : &it->second;
}

TerrainTile& TerrainQuadtree::getOrCreate(const TileId& id)
{
    auto [it, inserted] = _tiles.try_emplace(id);
    if (inserted) {
        it->second.id = id;
    }
    return it->second;
}

void TerrainQuadtree::clear()
{
    _tiles.clear();
    _drawList.clear();
    _requests.clear();
}

void TerrainQuadtree::select(const SelectionParams& p, double now)
{
    _frameNumber++;
    _drawList.clear();
    _requests.clear();
    _requested.clear();

    if (!_frame.isValid()) {
        return;
    }

    // Root tiles covering the view radius around the camera
    const double dLat = p.viewRadius / 111320.0;
    const double dLng = p.viewRadius / (111320.0 * std::max(0.05, std::cos(p.cameraLat * M_PI / 180.0)));
    double x0 = 0;
    double y0 = 0;
    double x1 = 0;
    double y1 = 0;
    TileMath::latLngToTile(p.cameraLat + dLat, p.cameraLng - dLng, kRootZoom, x0, y0);
    TileMath::latLngToTile(p.cameraLat - dLat, p.cameraLng + dLng, kRootZoom, x1, y1);
    const int count = TileMath::tileCount(kRootZoom);
    const int minX = static_cast<int>(std::floor(x0));
    const int maxX = std::min(minX + count - 1, static_cast<int>(std::floor(x1)));
    const int minY = std::max(0, static_cast<int>(std::floor(y0)));
    const int maxY = std::min(count - 1, static_cast<int>(std::floor(y1)));

    // x wraps across the antimeridian; the projection handles longitudes either side of it
    for (int x = minX; x <= maxX; x++) {
        for (int y = minY; y <= maxY; y++) {
            _visit(TileId{((x % count) + count) % count, y, kRootZoom}, p, now);
        }
    }

    std::stable_sort(_requests.begin(), _requests.end(),
                     [](const TileRequest& a, const TileRequest& b) { return a.priority < b.priority; });
    if (_requests.count() > kMaxRequests) {
        _requests.resize(kMaxRequests);
    }
}

void TerrainQuadtree::_visit(const TileId& id, const SelectionParams& p, double now)
{
    TerrainTile& tile = getOrCreate(id);
    tile.lastVisitedFrame = _frameNumber;
    _refreshState(tile, now);

    double dist = 0;
    double tileMeters = 0;
    bool inView = false;
    if (!_measure(tile, p, dist, inView, tileMeters)) {
        return;
    }

    const double texelPixels =
        tileMeters / TileMath::kTileSize * p.viewportHeight / (2 * std::max(1.0, dist) * std::tan(p.fovY / 2));
    const double threshold = inView ? p.maxTexelPixels : p.maxTexelPixels * kPrefetchFactor;
    const bool refine = (id.z < p.maxZoom) && (texelPixels > threshold) && !_childrenUnavailable(id, now);

    if (refine) {
        // Children wholly beyond the view radius are never drawn, so they are never loaded and must not hold up
        // replacing this tile. Children missing from the offline cache don't either when this tile can't be
        // drawn itself: descending loses nothing and may reach cached tiles further down.
        bool allChildrenReady = true;
        for (int c = 0; c < 4; c++) {
            TerrainTile& child = getOrCreate(id.child(c));
            child.lastVisitedFrame = _frameNumber;
            _refreshState(child, now);
            if (child.state == TileState::Ready) {
                continue;
            }
            if ((child.state == TileState::Unavailable) && (tile.state != TileState::Ready)) {
                continue;
            }
            double childDist = 0;
            double childMeters = 0;
            bool childInView = false;
            if (_measure(child, p, childDist, childInView, childMeters)) {
                allChildrenReady = false;
            }
        }

        if (allChildrenReady) {
            for (int c = 0; c < 4; c++) {
                _visit(id.child(c), p, now);
            }
            return;
        }

        // This tile stands in for its children while they load
        _use(tile, p, inView, dist, tileMeters, now);

        for (int c = 0; c < 4; c++) {
            TerrainTile& child = getOrCreate(id.child(c));
            if (child.state == TileState::Ready) {
                continue;
            }
            double childDist = 0;
            double childMeters = 0;
            bool childInView = false;
            if (_measure(child, p, childDist, childInView, childMeters)) {
                _request(child, p, childInView, childDist, childMeters);
            }
        }
        return;
    }

    _use(tile, p, inView, dist, tileMeters, now);
}

void TerrainQuadtree::_use(TerrainTile& tile, const SelectionParams& p, bool inView, double dist, double tileMeters,
                           double now)
{
    if (tile.state == TileState::Ready) {
        if (inView) {
            _drawList.append(tile.id);
        }
        if (tile.heightsAreFallback && (now >= tile.remeshAt)) {
            _request(tile, p, inView, dist, tileMeters);
        }
    } else {
        _request(tile, p, inView, dist, tileMeters);
    }
}

bool TerrainQuadtree::_isBackingOff(const TerrainTile& tile)
{
    return (tile.state == TileState::Failed) || (tile.state == TileState::NoData) ||
           (tile.state == TileState::Unavailable);
}

void TerrainQuadtree::_refreshState(TerrainTile& tile, double now)
{
    if (_isBackingOff(tile) && (now >= tile.retryAt)) {
        tile.state = TileState::Unloaded;
    }
}

bool TerrainQuadtree::_childrenUnavailable(const TileId& id, double now) const
{
    for (int c = 0; c < 4; c++) {
        const auto it = _tiles.find(id.child(c));
        if ((it != _tiles.end()) && (it->second.state == TileState::NoData) && (now < it->second.retryAt)) {
            return true;
        }
    }
    return false;
}

bool TerrainQuadtree::_measure(TerrainTile& tile, const SelectionParams& p, double& dist, bool& inView,
                               double& tileMeters)
{
    _ensureBounds(tile);

    const double minX = tile.minEasting - _frame.originEasting();
    const double maxX = tile.maxEasting - _frame.originEasting();
    const double minY = tile.minNorthing - _frame.originNorthing();
    const double maxY = tile.maxNorthing - _frame.originNorthing();
    double minZ = 0;
    double maxZ = 0;
    _heightRange(tile, p, minZ, maxZ);

    const double dx = std::max(0.0, std::max(minX - p.cameraX, p.cameraX - maxX));
    const double dy = std::max(0.0, std::max(minY - p.cameraY, p.cameraY - maxY));
    const double dz = std::max(0.0, std::max(minZ - p.cameraZ, p.cameraZ - maxZ));
    dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    tileMeters = std::max(maxX - minX, maxY - minY);

    if (dist > p.viewRadius) {
        inView = false;
        return false;
    }

    inView = !p.hasFrustum || p.frustum.intersectsBox(minX, minY, minZ, maxX, maxY, maxZ);
    return true;
}

void TerrainQuadtree::_ensureBounds(TerrainTile& tile)
{
    if (tile.boundsZoneVersion == _frame.zoneVersion()) {
        return;
    }
    TileMeshBuilder::estimateBounds(tile.id, _frame.zone(), tile.minEasting, tile.maxEasting, tile.minNorthing,
                                    tile.maxNorthing);
    tile.boundsZoneVersion = _frame.zoneVersion();
}

void TerrainQuadtree::_heightRange(const TerrainTile& tile, const SelectionParams& p, double& minZ, double& maxZ) const
{
    // The tile's own height range, else its nearest known ancestor's, else a guess around the camera's ground
    const TerrainTile* source = &tile;
    TileId id = tile.id;
    while (source && !source->heightsKnown && (id.z > kRootZoom)) {
        id = id.parent();
        const auto it = _tiles.find(id);
        source = (it == _tiles.end()) ? nullptr : &it->second;
    }

    if (source && source->heightsKnown) {
        minZ = source->minHeight;
        maxZ = source->maxHeight;
    } else {
        minZ = p.referenceHeight - 300;
        maxZ = p.referenceHeight + 300;
    }
}

void TerrainQuadtree::_request(const TerrainTile& tile, const SelectionParams& p, bool inView, double dist,
                               double tileMeters)
{
    if (_isBackingOff(tile)) {
        return;
    }
    if (_requested.contains(tile.id)) {
        return;
    }
    _requested.insert(tile.id);

    // Distance in units of the tile's own size: coarse tiles near the camera come before fine ones, and within a
    // level nearer tiles come first. The level term breaks ties toward coarse coverage.
    double priority = dist / std::max(1.0, tileMeters);
    if (inView) {
        const double cx = (tile.minEasting + tile.maxEasting) / 2 - _frame.originEasting() - p.cameraX;
        const double cy = (tile.minNorthing + tile.maxNorthing) / 2 - _frame.originNorthing() - p.cameraY;
        const double cz = (tile.heightsKnown ? (tile.minHeight + tile.maxHeight) / 2.0 : p.referenceHeight) - p.cameraZ;
        const double len = std::sqrt(cx * cx + cy * cy + cz * cz);
        if (len > 1e-3) {
            const double cosAngle = (cx * p.forwardX + cy * p.forwardY + cz * p.forwardZ) / len;
            const double offAxis = std::acos(std::clamp(cosAngle, -1.0, 1.0)) / std::max(0.1, p.fovY);
            priority *= 1 + 0.75 * std::min(2.0, offAxis);
        }
    } else {
        priority += kOutOfViewPenalty;
    }
    priority += kLevelBias * (tile.id.z - kRootZoom);

    TileRequest request;
    request.id = tile.id;
    request.kind = (tile.state == TileState::Ready) ? TileJobKind::Remesh : TileJobKind::Full;
    request.priority = priority;
    request.zone = _frame.zone();
    request.zoneVersion = _frame.zoneVersion();
    request.gridN = TileMeshBuilder::gridSizeFor(tile.id);
    request.fallbackHeight = static_cast<float>(p.referenceHeight);

    // Start from the closest ancestor with heights, so a tile never pops from flat to terrain twice
    TileId id = tile.id;
    while (id.z > 0) {
        id = id.parent();
        const auto it = _tiles.find(id);
        if ((it != _tiles.end()) && it->second.heights) {
            request.fallbackHeights = it->second.heights;
            request.fallbackGridN = it->second.gridN;
            request.fallbackSource = id;
            break;
        }
        if (id.z <= kRootZoom) {
            break;
        }
    }

    _requests.append(request);
}

void TerrainQuadtree::pruneStale(int maxAgeFrames, double now)
{
    for (auto it = _tiles.begin(); it != _tiles.end();) {
        const TerrainTile& tile = it->second;
        const bool keep = (tile.state == TileState::Ready) || (_isBackingOff(tile) && (now < tile.retryAt)) ||
                          (_frameNumber - tile.lastVisitedFrame <= maxAgeFrames);
        it = keep ? std::next(it) : _tiles.erase(it);
    }
}

// ---------------------------------------------------------------------------
// TileMeshBuilder

namespace TileMeshBuilder {

namespace {

void edgeCell(int edge, int s, int gridN, int& i, int& j)
{
    switch (edge) {
        case 0:
            i = s;
            j = 0;
            break;
        case 1:
            i = s;
            j = gridN;
            break;
        case 2:
            i = 0;
            j = s;
            break;
        default:
            i = gridN;
            j = s;
            break;
    }
}

void write(std::vector<float>& v, double x, double y, double z, float u, float t)
{
    v.push_back(static_cast<float>(x));
    v.push_back(static_cast<float>(y));
    v.push_back(static_cast<float>(z));
    v.push_back(u);
    v.push_back(t);
}

}  // namespace

int gridSizeFor(const TileId& id)
{
    const double tileMeters = TileMath::tileSizeMeters(TileMath::centerLat(id), id.z);
    int n = kMinGrid;
    while ((n < kMaxGrid) && (tileMeters / n > kDemResolutionMeters)) {
        n *= 2;
    }
    return n;
}

int vertexCount(int gridN)
{
    return (gridN + 1) * (gridN + 1) + 4 * (gridN + 1);
}

int indexCount(int gridN)
{
    return gridN * gridN * 6 + 4 * gridN * 6;
}

void gridLatLngs(const TileId& id, int gridN, std::vector<double>& rowLats, std::vector<double>& colLngs)
{
    rowLats.resize(gridN + 1);
    colLngs.resize(gridN + 1);
    for (int k = 0; k <= gridN; k++) {
        rowLats[k] = TileMath::tileYToLat(id.y + static_cast<double>(k) / gridN, id.z);
        colLngs[k] = TileMath::tileXToLng(id.x + static_cast<double>(k) / gridN, id.z);
    }
}

TileMesh build(const TileId& id, int gridN, const std::vector<double>& rowLats, const std::vector<double>& colLngs,
               std::shared_ptr<const std::vector<float>> heightsPtr, int zone)
{
    const int cols = gridN + 1;
    const int gridVerts = cols * cols;
    const std::vector<float>& heights = *heightsPtr;

    TileMesh mesh;
    mesh.gridN = gridN;
    mesh.heights = heightsPtr;
    mesh.zone = zone;
    mesh.vertices.reserve(static_cast<size_t>(vertexCount(gridN)) * kFloatsPerVertex);

    Utm::forward(TileMath::centerLat(id), TileMath::centerLng(id), zone, mesh.originEasting, mesh.originNorthing);

    std::vector<double> eastings(gridVerts);
    std::vector<double> northings(gridVerts);
    double minE = std::numeric_limits<double>::max();
    double maxE = std::numeric_limits<double>::lowest();
    double minN = std::numeric_limits<double>::max();
    double maxN = std::numeric_limits<double>::lowest();
    float minH = std::numeric_limits<float>::max();
    float maxH = std::numeric_limits<float>::lowest();

    for (int j = 0; j < cols; j++) {
        for (int i = 0; i < cols; i++) {
            const int k = j * cols + i;
            Utm::forward(rowLats[j], colLngs[i], zone, eastings[k], northings[k]);
            minE = std::min(minE, eastings[k]);
            maxE = std::max(maxE, eastings[k]);
            minN = std::min(minN, northings[k]);
            maxN = std::max(maxN, northings[k]);
            minH = std::min(minH, heights[k]);
            maxH = std::max(maxH, heights[k]);
        }
    }

    mesh.minEasting = minE;
    mesh.maxEasting = maxE;
    mesh.minNorthing = minN;
    mesh.maxNorthing = maxN;

    const double cellMeters = (maxE - minE) / gridN;
    const float skirt = skirtDepth(cellMeters, minH, maxH);
    mesh.minHeight = minH - skirt;
    mesh.maxHeight = maxH;

    for (int j = 0; j < cols; j++) {
        for (int i = 0; i < cols; i++) {
            const int k = j * cols + i;
            write(mesh.vertices, eastings[k] - mesh.originEasting, northings[k] - mesh.originNorthing, heights[k],
                  static_cast<float>(i) / gridN, static_cast<float>(j) / gridN);
        }
    }

    // Skirts: north, south, west, east edges, each a copy of the edge dropped by the skirt depth
    for (int edge = 0; edge < 4; edge++) {
        for (int s = 0; s < cols; s++) {
            int i = 0;
            int j = 0;
            edgeCell(edge, s, gridN, i, j);
            const int k = j * cols + i;
            write(mesh.vertices, eastings[k] - mesh.originEasting, northings[k] - mesh.originNorthing,
                  heights[k] - skirt, static_cast<float>(i) / gridN, static_cast<float>(j) / gridN);
        }
    }

    return mesh;
}

float skirtDepth(double cellMeters, float minH, float maxH)
{
    // Deep enough to cover the largest crack a coarser neighbour can leave
    return static_cast<float>(std::max(5.0, cellMeters * 0.5 + (maxH - minH) * 0.02));
}

std::vector<quint16> buildIndices(int gridN)
{
    const int cols = gridN + 1;
    std::vector<quint16> indices;
    indices.reserve(indexCount(gridN));

    for (int j = 0; j < gridN; j++) {
        for (int i = 0; i < gridN; i++) {
            const int a = j * cols + i;
            const int b = a + 1;
            const int c = a + cols;
            const int d = c + 1;
            indices.insert(indices.end(), {static_cast<quint16>(a), static_cast<quint16>(c), static_cast<quint16>(b),
                                           static_cast<quint16>(b), static_cast<quint16>(c), static_cast<quint16>(d)});
        }
    }

    const int skirtBase = cols * cols;
    for (int edge = 0; edge < 4; edge++) {
        for (int s = 0; s < gridN; s++) {
            int i0 = 0;
            int j0 = 0;
            int i1 = 0;
            int j1 = 0;
            edgeCell(edge, s, gridN, i0, j0);
            edgeCell(edge, s + 1, gridN, i1, j1);
            const int top0 = j0 * cols + i0;
            const int top1 = j1 * cols + i1;
            const int low0 = skirtBase + edge * cols + s;
            const int low1 = low0 + 1;
            indices.insert(indices.end(),
                           {static_cast<quint16>(top0), static_cast<quint16>(low0), static_cast<quint16>(top1),
                            static_cast<quint16>(top1), static_cast<quint16>(low0), static_cast<quint16>(low1)});
        }
    }
    return indices;
}

void estimateBounds(const TileId& id, int zone, double& minE, double& maxE, double& minN, double& maxN)
{
    // From a 3x3 sample of the corners and edge midpoints
    minE = std::numeric_limits<double>::max();
    maxE = std::numeric_limits<double>::lowest();
    minN = std::numeric_limits<double>::max();
    maxN = std::numeric_limits<double>::lowest();
    for (int j = 0; j <= 2; j++) {
        const double lat = TileMath::tileYToLat(id.y + j * 0.5, id.z);
        for (int i = 0; i <= 2; i++) {
            const double lng = TileMath::tileXToLng(id.x + i * 0.5, id.z);
            double e = 0;
            double n = 0;
            Utm::forward(lat, lng, zone, e, n);
            minE = std::min(minE, e);
            maxE = std::max(maxE, e);
            minN = std::min(minN, n);
            maxN = std::max(maxN, n);
        }
    }
}

}  // namespace TileMeshBuilder

// ---------------------------------------------------------------------------
// Elevation

namespace Elevation {

namespace {

float bilinear(const std::vector<float>& grid, int cols, double u, double v)
{
    const int max = cols - 1;
    u = std::clamp(u, 0.0, static_cast<double>(max));
    v = std::clamp(v, 0.0, static_cast<double>(max));
    const int i0 = std::min(static_cast<int>(u), max - 1);
    const int j0 = std::min(static_cast<int>(v), max - 1);
    const double fu = u - i0;
    const double fv = v - j0;

    const double a = grid[j0 * cols + i0];
    const double b = grid[j0 * cols + i0 + 1];
    const double c = grid[(j0 + 1) * cols + i0];
    const double d = grid[(j0 + 1) * cols + i0 + 1];
    const double top = a + (b - a) * fu;
    const double bottom = c + (d - c) * fu;
    return static_cast<float>(top + (bottom - top) * fv);
}

}  // namespace

void sampleFromAncestor(const TileId& tile, int gridN, const TileId& ancestor,
                        const std::vector<float>& ancestorHeights, int ancestorGridN, std::vector<float>& heights)
{
    const int cols = gridN + 1;
    const int ancestorCols = ancestorGridN + 1;
    const double scale = 1.0 / (1 << (tile.z - ancestor.z));
    heights.resize(static_cast<size_t>(cols) * cols);

    for (int j = 0; j <= gridN; j++) {
        const double v = ((tile.y + static_cast<double>(j) / gridN) * scale - ancestor.y) * ancestorGridN;
        for (int i = 0; i <= gridN; i++) {
            const double u = ((tile.x + static_cast<double>(i) / gridN) * scale - ancestor.x) * ancestorGridN;
            heights[j * cols + i] = bilinear(ancestorHeights, ancestorCols, u, v);
        }
    }
}

void fillVoids(std::vector<float>& heights)
{
    double sum = 0;
    size_t count = 0;
    for (const float h : heights) {
        if (!std::isnan(h)) {
            sum += h;
            count++;
        }
    }
    if (count == heights.size()) {
        return;
    }
    const float fill = (count > 0) ? static_cast<float>(sum / count) : 0.0f;
    for (float& h : heights) {
        if (std::isnan(h)) {
            h = fill;
        }
    }
}

}  // namespace Elevation

}  // namespace Map3D
