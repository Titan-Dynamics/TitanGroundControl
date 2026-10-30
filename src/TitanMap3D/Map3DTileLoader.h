#pragma once

#include <algorithm>
#include <functional>
#include <memory>
#include <vector>

#include <QtCore/QAtomicInt>
#include <QtCore/QHash>
#include <QtCore/QMutex>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QQueue>
#include <QtCore/QSet>
#include <QtCore/QString>

#include "Map3DTerrain.h"

class QNetworkAccessManager;
class QNetworkReply;

namespace Map3D {

/// Terrarium elevation tiles stop at this zoom; finer terrain samples their zoom-15 ancestor.
constexpr int kMaxElevationZoom = 15;
/// Zoom of the elevation tiles fetched for single points (vehicle, view centre, waypoints).
constexpr int kPointElevationZoom = 14;
/// A terrain tile samples the elevation tile this many levels up: 256 px across 8 tiles is 32 samples per tile, as
/// many as its mesh has cells, so one (large) elevation download serves 64 imagery tiles.
constexpr int kElevationZoomOffset = 3;

/// A decoded Terrarium tile: metres AMSL, row-major from the north-west corner.
struct ElevationTile
{
    TileId id;
    int size = 0;
    std::vector<float> heights;
};

/// Decoded elevation tiles shared by the loader's workers and the render thread. Thread-safe.
class ElevationCache
{
public:
    static constexpr int kCapacity = 256;

    std::shared_ptr<const ElevationTile> get(const TileId& id);
    void insert(std::shared_ptr<const ElevationTile> tile);
    void clear();

    /// Height at a position from the finest cached tile, or false while none is loaded. Never blocks on I/O.
    bool tryHeight(double lat, double lng, double& height);
    /// Heights for a terrain tile's grid from the elevation tile covering it, or false while it is not loaded.
    bool sampleGrid(const TileId& id, const std::vector<double>& rowLats, const std::vector<double>& colLngs,
                    std::vector<float>& heights);

    static TileId elevationTileFor(const TileId& id)
    {
        return id.ancestorAt(std::clamp(id.z - kElevationZoomOffset, 0, kMaxElevationZoom));
    }

    static double sample(const ElevationTile& tile, double lat, double lng);
    /// Terrarium RGB: height = (R * 256 + G + B / 256) - 32768 metres
    static std::shared_ptr<ElevationTile> decode(const TileId& id, const QByteArray& png);

private:
    struct Entry
    {
        std::shared_ptr<const ElevationTile> tile;
        quint64 lastUse = 0;
    };

    QMutex _mutex;
    QHash<TileId, Entry> _tiles;
    quint64 _tick = 0;
};

/// Loads terrain tiles: imagery from the 2D map's provider and Terrarium elevation, both cache-first through
/// QGroundControl's shared tile cache (network only on a miss, and written back), decoded and meshed on a thread
/// pool. The render thread replaces the wanted set every frame with setRequests(); the best-ranked request starts
/// next and requests that drop out of the wanted set are discarded before they start. Lives on the GUI thread;
/// the public methods are thread-safe.
class TileLoader : public QObject
{
    Q_OBJECT

public:
    static constexpr int kMaxJobs = 16;

    TileLoader(const QString& imageryMapType, QObject* parent = nullptr);
    ~TileLoader() override;

    void setRequests(const QList<TileRequest>& requests);
    bool takeResult(TileLoadResult& result);
    bool hasResults() const;
    int pendingCount() const;
    int inFlightCount() const;

    int workerCount() const { return kMaxJobs; }

    /// Tiles served by the shared disk cache and by the network since the loader started
    int cacheHits() const { return _cacheHits.loadRelaxed(); }

    int networkFetches() const { return _networkFetches.loadRelaxed(); }

    /// Fetches the elevation tile under a position, for point heights (see ElevationCache::tryHeight).
    void requestPointElevation(double lat, double lng);

    ElevationCache& elevation() { return _shared->elevation; }

    QString imageryMapType() const { return _imageryMapType; }

private:
    /// Outcome, body, error text, and whether the body came from the network (and should be cached once it decodes)
    using FetchCallback = std::function<void(TileLoadOutcome, const QByteArray&, const QString&, bool)>;

    struct Shared
    {
        ElevationCache elevation;
        mutable QMutex resultsMutex;
        QQueue<TileLoadResult> results;
    };

    void _schedule();
    void _startJob(const TileRequest& job);
    void _finishJob(const TileId& id);
    void _build(const TileRequest& job, const QByteArray& imagery, double fetchMs, bool cacheImagery);
    void _cacheTile(const QString& mapType, const TileId& id, const QByteArray& data);
    void _withElevation(const TileId& elevationId, std::function<void(bool)> done);
    void _fetchTile(const QString& mapType, const TileId& id, FetchCallback done);
    void _fetchFromNetwork(const QString& mapType, int mapId, const TileId& id, FetchCallback done);
    void _pushResult(TileLoadResult result);

    QString _imageryMapType;
    int _imageryMapId = -1;
    int _elevationMapId = -1;
    QNetworkAccessManager* _network = nullptr;
    std::shared_ptr<Shared> _shared;

    mutable QMutex _mutex;  ///< Guards _pending, _inFlight and _done
    QHash<TileId, TileRequest> _pending;
    QSet<TileId> _inFlight;
    QSet<TileId> _done;  ///< Finished but not yet collected by the render thread; not re-queued meanwhile
    bool _scheduleQueued = false;

    // GUI thread only
    QHash<TileId, QList<std::function<void(bool)>>> _elevationWaiters;
    QHash<TileId, double> _elevationFailedAt;
    QSet<QNetworkReply*> _replies;
    QAtomicInt _cacheHits;
    QAtomicInt _networkFetches;
};

}  // namespace Map3D
