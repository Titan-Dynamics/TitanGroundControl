#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include <QtCore/QHash>
#include <QtCore/QList>
#include <QtCore/QSet>
#include <QtCore/QString>
#include <QtCore/QtMath>

#include "Map3DGeo.h"

/// Terrain tile selection and meshing, ported from Mission Planner's Map3D terrain engine (TerrainQuadtree,
/// TileMeshBuilder, ElevationSampler). Pure logic with no GPU calls; the quadtree is owned by the render thread.
namespace Map3D {

/// Per-frame inputs to tile selection, in local scene coordinates.
struct SelectionParams
{
    double cameraX = 0;
    double cameraY = 0;
    double cameraZ = 0;
    /// Unit view direction
    double forwardX = 0;
    double forwardY = 1;
    double forwardZ = 0;
    double cameraLat = 0;
    double cameraLng = 0;
    Frustum frustum;
    bool hasFrustum = false;
    /// Terrain is selected out to this distance and fully fogged just before it
    double viewRadius = 40000;
    int viewportHeight = 600;
    double fovY = M_PI / 3;
    /// Finest imagery level; screen-space error decides how much of it is used
    int maxZoom = 20;
    /// Refine until one imagery texel covers at most this many screen pixels
    double maxTexelPixels = 1.25;
    /// Ground height near the camera, used for tiles whose heights are not known yet
    double referenceHeight = 0;

    /// 24 within 100 m of the ground, 22 up to 500 m, 20 above, with 10% hysteresis at each boundary.
    static int maxZoomFor(double heightAboveGround, int current);
};

enum class TileState
{
    Unloaded,     ///< Not on the GPU (never loaded, or evicted)
    Ready,        ///< Texture and mesh are on the GPU
    Failed,       ///< The last load failed; retried after retryAt
    NoData,       ///< The imagery provider has nothing for this tile, so nothing deeper either
    Unavailable,  ///< Not cached while offline; deeper tiles may still be cached
};

enum class TileJobKind
{
    Full,    ///< Fetch imagery, sample elevation and build the mesh
    Remesh,  ///< Rebuild the mesh only, once elevation that was missing has arrived
};

enum class TileLoadOutcome
{
    Loaded,
    NoData,
    Offline,
    Failed,
    StillPending
};

/// CPU-side geometry for one terrain tile, ready to upload.
struct TileMesh
{
    /// x, y, z, u, v per vertex; x/y are metres from (originEasting, originNorthing), z is AMSL
    std::vector<float> vertices;
    int gridN = 0;
    /// (gridN + 1)^2 heights, row-major from the north-west corner
    std::shared_ptr<const std::vector<float>> heights;
    float minHeight = 0;
    float maxHeight = 0;
    int zone = 0;
    double originEasting = 0;
    double originNorthing = 0;
    double minEasting = 0;
    double maxEasting = 0;
    double minNorthing = 0;
    double maxNorthing = 0;
};

/// One node of the terrain quadtree. Owned by the render thread; loaders only ever see TileRequests.
struct TerrainTile
{
    TileId id;
    TileState state = TileState::Unloaded;

    /// Absolute UTM bounds, valid while boundsZoneVersion matches the world frame
    int boundsZoneVersion = -1;
    double minEasting = 0;
    double maxEasting = 0;
    double minNorthing = 0;
    double maxNorthing = 0;

    /// Height range including skirts, known once a mesh has been built
    bool heightsKnown = false;
    float minHeight = 0;
    float maxHeight = 0;
    /// The last built height grid, kept so children can start from it while their data loads
    std::shared_ptr<const std::vector<float>> heights;
    int gridN = 0;

    /// True while the mesh uses approximate heights because elevation data was not ready
    bool heightsAreFallback = false;
    double remeshAt = 0;

    double originEasting = 0;
    double originNorthing = 0;
    bool onGpu = false;
    qint64 gpuBytes = 0;

    int lastVisitedFrame = 0;
    int failCount = 0;
    double retryAt = 0;
};

/// Work for the loader: immutable once queued.
struct TileRequest
{
    TileId id;
    TileJobKind kind = TileJobKind::Full;
    double priority = 0;
    int zone = 0;
    int zoneVersion = 0;
    int gridN = 0;
    /// Heights to approximate from while elevation is missing (an ancestor's grid), or null
    std::shared_ptr<const std::vector<float>> fallbackHeights;
    TileId fallbackSource;
    int fallbackGridN = 0;
    /// Flat height used when there is no ancestor grid either
    float fallbackHeight = 0;
};

struct TileLoadResult
{
    TileId id;
    TileJobKind kind = TileJobKind::Full;
    TileLoadOutcome outcome = TileLoadOutcome::Failed;
    int zoneVersion = 0;
    /// RGBA8888 pixels, or empty for a remesh
    QByteArray pixels;
    int width = 0;
    int height = 0;
    TileMesh mesh;
    bool heightsAreFallback = false;
    QString error;
    double fetchMs = 0;
    double buildMs = 0;
};

struct TileIdHash
{
    size_t operator()(const TileId& id) const noexcept { return qHash(id); }
};

/// Chooses which terrain tiles to draw and which to load, Google Earth style: refined from coarse roots until each
/// tile's texels are about a pixel on screen; a tile is only replaced by its children once all four are loaded;
/// load requests are ranked by distance relative to tile size and angle from the view centre.
class TerrainQuadtree
{
public:
    static constexpr int kRootZoom = 9;
    static constexpr double kPrefetchFactor = 4;
    static constexpr double kOutOfViewPenalty = 8;
    static constexpr double kLevelBias = 0.1;
    static constexpr int kMaxRequests = 256;

    explicit TerrainQuadtree(const WorldFrame& frame)
        : _frame(frame)
    {}

    int frame() const { return _frameNumber; }

    int count() const { return static_cast<int>(_tiles.size()); }

    /// Node-based, so references to tiles stay valid while the tree grows
    std::unordered_map<TileId, TerrainTile, TileIdHash>& tiles() { return _tiles; }

    TerrainTile* find(const TileId& id);
    TerrainTile& getOrCreate(const TileId& id);
    void clear();

    void select(const SelectionParams& p, double now);

    /// Tiles to draw this frame
    const QList<TileId>& drawList() const { return _drawList; }

    /// Tiles to load, best first
    const QList<TileRequest>& requests() const { return _requests; }

    /// Forgets tiles not visited for a while that hold no GPU resources and are not waiting out a retry.
    void pruneStale(int maxAgeFrames, double now);

private:
    void _visit(const TileId& id, const SelectionParams& p, double now);
    void _use(TerrainTile& tile, const SelectionParams& p, bool inView, double dist, double tileMeters, double now);
    void _refreshState(TerrainTile& tile, double now);
    bool _childrenUnavailable(const TileId& id, double now) const;
    bool _measure(TerrainTile& tile, const SelectionParams& p, double& dist, bool& inView, double& tileMeters);
    void _ensureBounds(TerrainTile& tile);
    void _heightRange(const TerrainTile& tile, const SelectionParams& p, double& minZ, double& maxZ) const;
    void _request(const TerrainTile& tile, const SelectionParams& p, bool inView, double dist, double tileMeters);

    static bool _isBackingOff(const TerrainTile& tile);

    const WorldFrame& _frame;
    std::unordered_map<TileId, TerrainTile, TileIdHash> _tiles;
    QSet<TileId> _requested;
    QList<TileId> _drawList;
    QList<TileRequest> _requests;
    int _frameNumber = 0;
};

/// Builds a regular (N+1) x (N+1) height grid per tile plus skirts hanging below the edges to hide cracks between
/// neighbours at different levels of detail. Tiles with the same N share one index buffer.
namespace TileMeshBuilder {

constexpr int kFloatsPerVertex = 5;
constexpr int kMinGrid = 4;
constexpr int kMaxGrid = 32;
/// Elevation is ~30 m (SRTM1-class) at best; finer grids add vertices without detail
constexpr double kDemResolutionMeters = 30;

int gridSizeFor(const TileId& id);
int vertexCount(int gridN);
int indexCount(int gridN);
void gridLatLngs(const TileId& id, int gridN, std::vector<double>& rowLats, std::vector<double>& colLngs);
TileMesh build(const TileId& id, int gridN, const std::vector<double>& rowLats, const std::vector<double>& colLngs,
               std::shared_ptr<const std::vector<float>> heights, int zone);
float skirtDepth(double cellMeters, float minH, float maxH);
std::vector<quint16> buildIndices(int gridN);
void estimateBounds(const TileId& id, int zone, double& minE, double& maxE, double& minN, double& maxN);

}  // namespace TileMeshBuilder

/// Elevation helpers for the tile grids.
namespace Elevation {

/// Approximate heights for a tile from an ancestor's grid, used until real elevation data arrives.
void sampleFromAncestor(const TileId& tile, int gridN, const TileId& ancestor,
                        const std::vector<float>& ancestorHeights, int ancestorGridN, std::vector<float>& heights);
/// Replaces voids (NaN) with the mean of the valid samples so a void never drops a tile.
void fillVoids(std::vector<float>& heights);

}  // namespace Elevation

}  // namespace Map3D
