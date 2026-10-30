#include "Map3DTileLoader.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>
#include <QtCore/QThreadPool>
#include <QtGui/QImage>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>

#include "Map3DScene.h"
#include "MapProvider.h"
#include "QGCCacheTile.h"
#include "QGCLoggingCategory.h"
#include "QGCMapEngine.h"
#include "QGCMapTasks.h"
#include "QGCMapUrlEngine.h"
#include "QGeoFileTileCacheQGC.h"
#include "QGeoTileFetcherQGC.h"

QGC_LOGGING_CATEGORY(Map3DTileLoaderLog, "TitanMap3D.TileLoader")

namespace Map3D {

namespace {

const QString kElevationMapType = QStringLiteral("Terrarium");
constexpr double kElevationRetrySeconds = 30;

// Bing serves a placeholder image instead of an HTTP error where it has no imagery
bool isBingEmptyTile(int mapId, const QByteArray& data)
{
    static const QByteArray bingNoTileImage = [] {
        QFile file(QStringLiteral(":/res/BingNoTileBytes.dat"));
        return file.open(QFile::ReadOnly) ? file.readAll() : QByteArray();
    }();
    if (bingNoTileImage.isEmpty() || (data != bingNoTileImage)) {
        return false;
    }
    const SharedMapProvider provider = UrlFactory::getMapProviderFromQtMapId(mapId);
    return provider && provider->isBingProvider();
}

}  // namespace

// ---------------------------------------------------------------------------
// ElevationCache

std::shared_ptr<const ElevationTile> ElevationCache::get(const TileId& id)
{
    QMutexLocker lock(&_mutex);
    auto it = _tiles.find(id);
    if (it == _tiles.end()) {
        return nullptr;
    }
    it->lastUse = ++_tick;
    return it->tile;
}

void ElevationCache::insert(std::shared_ptr<const ElevationTile> tile)
{
    const TileId id = tile->id;  // before the move below: argument order is unspecified
    QMutexLocker lock(&_mutex);
    _tiles.insert(id, Entry{std::move(tile), ++_tick});
    while (_tiles.size() > kCapacity) {
        auto oldest = _tiles.begin();
        for (auto it = _tiles.begin(); it != _tiles.end(); ++it) {
            if (it->lastUse < oldest->lastUse) {
                oldest = it;
            }
        }
        _tiles.erase(oldest);
    }
}

void ElevationCache::clear()
{
    QMutexLocker lock(&_mutex);
    _tiles.clear();
}

double ElevationCache::sample(const ElevationTile& tile, double lat, double lng)
{
    double tx = 0;
    double ty = 0;
    TileMath::latLngToTile(lat, lng, tile.id.z, tx, ty);
    // Pixel centres sit at half-pixel offsets
    const double px = std::clamp((tx - tile.id.x) * tile.size - 0.5, 0.0, tile.size - 1.0);
    const double py = std::clamp((ty - tile.id.y) * tile.size - 0.5, 0.0, tile.size - 1.0);
    const int x0 = std::min(static_cast<int>(px), tile.size - 2);
    const int y0 = std::min(static_cast<int>(py), tile.size - 2);
    const double fx = px - x0;
    const double fy = py - y0;
    const float* row0 = tile.heights.data() + static_cast<size_t>(y0) * tile.size;
    const float* row1 = row0 + tile.size;
    const double top = row0[x0] + (row0[x0 + 1] - row0[x0]) * fx;
    const double bottom = row1[x0] + (row1[x0 + 1] - row1[x0]) * fx;
    return top + (bottom - top) * fy;
}

bool ElevationCache::tryHeight(double lat, double lng, double& height)
{
    for (int z = kMaxElevationZoom; z >= 0; z--) {
        double tx = 0;
        double ty = 0;
        TileMath::latLngToTile(lat, lng, z, tx, ty);
        const TileId id{static_cast<int>(std::floor(tx)), static_cast<int>(std::floor(ty)), z};
        if (const auto tile = get(id)) {
            height = sample(*tile, lat, lng);
            return true;
        }
    }
    return false;
}

bool ElevationCache::sampleGrid(const TileId& id, const std::vector<double>& rowLats,
                                const std::vector<double>& colLngs, std::vector<float>& heights)
{
    const auto tile = get(elevationTileFor(id));
    if (!tile) {
        return false;
    }
    const size_t cols = colLngs.size();
    heights.resize(rowLats.size() * cols);
    for (size_t j = 0; j < rowLats.size(); j++) {
        for (size_t i = 0; i < cols; i++) {
            heights[j * cols + i] = static_cast<float>(sample(*tile, rowLats[j], colLngs[i]));
        }
    }
    Elevation::fillVoids(heights);
    return true;
}

std::shared_ptr<ElevationTile> ElevationCache::decode(const TileId& id, const QByteArray& png)
{
    QImage image;
    if (!image.loadFromData(png)) {
        return nullptr;
    }
    image = image.convertToFormat(QImage::Format_RGB32);
    if ((image.width() != image.height()) || (image.width() < 2)) {
        return nullptr;
    }

    auto tile = std::make_shared<ElevationTile>();
    tile->id = id;
    tile->size = image.width();
    tile->heights.resize(static_cast<size_t>(tile->size) * tile->size);
    for (int y = 0; y < tile->size; y++) {
        const QRgb* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        float* out = tile->heights.data() + static_cast<size_t>(y) * tile->size;
        for (int x = 0; x < tile->size; x++) {
            const QRgb pixel = line[x];
            out[x] = static_cast<float>(qRed(pixel) * 256.0 + qGreen(pixel) + qBlue(pixel) / 256.0 - 32768.0);
        }
    }
    return tile;
}

// ---------------------------------------------------------------------------
// TileLoader

TileLoader::TileLoader(const QString& imageryMapType, QObject* parent)
    : QObject(parent)
    , _imageryMapType(imageryMapType)
    , _imageryMapId(UrlFactory::getQtMapIdFromProviderType(imageryMapType))
    , _elevationMapId(UrlFactory::getQtMapIdFromProviderType(kElevationMapType))
    , _network(new QNetworkAccessManager(this))
    , _shared(std::make_shared<Shared>())
{
    if (_imageryMapId < 0) {
        qCWarning(Map3DTileLoaderLog) << "unknown imagery map type" << _imageryMapType;
    }
    if (_elevationMapId < 0) {
        qCWarning(Map3DTileLoaderLog) << "elevation provider" << kElevationMapType << "is not available";
    }
}

TileLoader::~TileLoader()
{
    for (QNetworkReply* reply : std::as_const(_replies)) {
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
}

void TileLoader::setRequests(const QList<TileRequest>& requests)
{
    {
        QMutexLocker lock(&_mutex);
        _pending.clear();
        for (const TileRequest& request : requests) {
            if (_inFlight.contains(request.id) || _done.contains(request.id)) {
                continue;
            }
            _pending.insert(request.id, request);
        }
        if (_pending.isEmpty() || _scheduleQueued) {
            return;
        }
        _scheduleQueued = true;
    }
    QMetaObject::invokeMethod(this, &TileLoader::_schedule, Qt::QueuedConnection);
}

bool TileLoader::takeResult(TileLoadResult& result)
{
    {
        QMutexLocker lock(&_shared->resultsMutex);
        if (_shared->results.isEmpty()) {
            return false;
        }
        result = _shared->results.dequeue();
    }
    QMutexLocker lock(&_mutex);
    _done.remove(result.id);
    return true;
}

bool TileLoader::hasResults() const
{
    QMutexLocker lock(&_shared->resultsMutex);
    return !_shared->results.isEmpty();
}

int TileLoader::pendingCount() const
{
    QMutexLocker lock(&_mutex);
    return _pending.size();
}

int TileLoader::inFlightCount() const
{
    QMutexLocker lock(&_mutex);
    return _inFlight.size();
}

void TileLoader::requestPointElevation(double lat, double lng)
{
    double tx = 0;
    double ty = 0;
    TileMath::latLngToTile(lat, lng, kPointElevationZoom, tx, ty);
    const TileId id{static_cast<int>(std::floor(tx)), static_cast<int>(std::floor(ty)), kPointElevationZoom};
    if (!TileMath::isValid(id) || _shared->elevation.get(id)) {
        return;
    }
    QMetaObject::invokeMethod(this, [this, id] { _withElevation(id, {}); }, Qt::QueuedConnection);
}

void TileLoader::_schedule()
{
    QList<TileRequest> starting;
    {
        QMutexLocker lock(&_mutex);
        _scheduleQueued = false;
        while ((_inFlight.size() + starting.size() < kMaxJobs) && !_pending.isEmpty()) {
            auto best = _pending.begin();
            for (auto it = _pending.begin(); it != _pending.end(); ++it) {
                if (it->priority < best->priority) {
                    best = it;
                }
            }
            starting.append(*best);
            _inFlight.insert(best->id);
            _pending.erase(best);
        }
    }
    for (const TileRequest& job : std::as_const(starting)) {
        _startJob(job);
    }
}

void TileLoader::_startJob(const TileRequest& job)
{
    if (job.kind == TileJobKind::Remesh) {
        _withElevation(ElevationCache::elevationTileFor(job.id), [this, job](bool) { _build(job, {}, 0, false); });
        return;
    }

    // Elevation downloads alongside the imagery rather than after it
    const TileId elevationId = ElevationCache::elevationTileFor(job.id);
    _withElevation(elevationId, {});

    auto clock = std::make_shared<QElapsedTimer>();
    clock->start();
    _fetchTile(_imageryMapType, job.id,
               [this, job, clock, elevationId](TileLoadOutcome outcome, const QByteArray& data, const QString& error,
                                               bool fromNetwork) {
                   if (outcome != TileLoadOutcome::Loaded) {
                       TileLoadResult result;
                       result.id = job.id;
                       result.kind = job.kind;
                       result.zoneVersion = job.zoneVersion;
                       result.outcome = outcome;
                       result.error = error;
                       _pushResult(std::move(result));
                       _finishJob(job.id);
                       return;
                   }
                   const double fetchMs = clock->nsecsElapsed() / 1e6;
                   // Waits for an elevation download still in flight, so a tile does not appear flat and then pop
                   _withElevation(elevationId, [this, job, data, fetchMs, fromNetwork](bool) {
                       _build(job, data, fetchMs, fromNetwork);
                   });
               });
}

void TileLoader::_build(const TileRequest& job, const QByteArray& imagery, double fetchMs, bool cacheImagery)
{
    const std::shared_ptr<Shared> shared = _shared;
    const QPointer<TileLoader> self(this);
    const QString mapType = _imageryMapType;
    QThreadPool::globalInstance()->start([shared, self, job, imagery, fetchMs, cacheImagery, mapType] {
        QElapsedTimer clock;
        clock.start();
        TileLoadResult result;
        result.id = job.id;
        result.kind = job.kind;
        result.zoneVersion = job.zoneVersion;
        result.fetchMs = fetchMs;

        bool imageryDecoded = true;
        if (job.kind == TileJobKind::Full) {
            QImage image;
            if (!image.loadFromData(imagery)) {
                imageryDecoded = false;
                result.outcome = TileLoadOutcome::Failed;
                result.error = QStringLiteral("imagery did not decode");
            } else {
                // Only bodies that decode are cached, never an HTTP-200 error page
                if (cacheImagery && self) {
                    QMetaObject::invokeMethod(
                        self,
                        [self, mapType, id = job.id, imagery] {
                            if (self) {
                                self->_cacheTile(mapType, id, imagery);
                            }
                        },
                        Qt::QueuedConnection);
                }
                image = image.convertToFormat(QImage::Format_RGBA8888);
                result.width = image.width();
                result.height = image.height();
                result.pixels = QByteArray(reinterpret_cast<const char*>(image.constBits()),
                                           static_cast<qsizetype>(image.sizeInBytes()));
            }
        }

        if (imageryDecoded) {
            std::vector<double> rowLats;
            std::vector<double> colLngs;
            TileMeshBuilder::gridLatLngs(job.id, job.gridN, rowLats, colLngs);
            auto heights = std::make_shared<std::vector<float>>();
            const bool fallback = !shared->elevation.sampleGrid(job.id, rowLats, colLngs, *heights);
            if (fallback && job.kind == TileJobKind::Remesh) {
                result.outcome = TileLoadOutcome::StillPending;
            } else {
                if (fallback) {
                    if (job.fallbackHeights) {
                        Elevation::sampleFromAncestor(job.id, job.gridN, job.fallbackSource, *job.fallbackHeights,
                                                      job.fallbackGridN, *heights);
                    } else {
                        heights->assign(static_cast<size_t>(job.gridN + 1) * (job.gridN + 1), job.fallbackHeight);
                    }
                }
                result.mesh = TileMeshBuilder::build(job.id, job.gridN, rowLats, colLngs, heights, job.zone);
                result.heightsAreFallback = fallback;
                result.outcome = TileLoadOutcome::Loaded;
            }
        }
        result.buildMs = clock.nsecsElapsed() / 1e6;

        {
            QMutexLocker lock(&shared->resultsMutex);
            shared->results.enqueue(std::move(result));
        }
        if (self) {
            const TileId id = job.id;
            QMetaObject::invokeMethod(
                self,
                [self, id] {
                    if (self) {
                        self->_finishJob(id);
                    }
                },
                Qt::QueuedConnection);
        }
    });
}

void TileLoader::_pushResult(TileLoadResult result)
{
    QMutexLocker lock(&_shared->resultsMutex);
    _shared->results.enqueue(std::move(result));
}

void TileLoader::_finishJob(const TileId& id)
{
    {
        QMutexLocker lock(&_mutex);
        _inFlight.remove(id);
        _done.insert(id);
    }
    _schedule();
}

void TileLoader::_withElevation(const TileId& elevationId, std::function<void(bool)> done)
{
    if (_shared->elevation.get(elevationId)) {
        if (done) {
            done(true);
        }
        return;
    }
    const auto failed = _elevationFailedAt.constFind(elevationId);
    if ((failed != _elevationFailedAt.cend()) && (nowSeconds() - failed.value() < kElevationRetrySeconds)) {
        if (done) {
            done(false);
        }
        return;
    }

    auto waiters = _elevationWaiters.find(elevationId);
    if (waiters != _elevationWaiters.end()) {
        if (done) {
            waiters->append(std::move(done));
        }
        return;
    }
    _elevationWaiters.insert(
        elevationId, done ? QList<std::function<void(bool)>>{std::move(done)} : QList<std::function<void(bool)>>{});

    const auto finish = [this, elevationId](bool ok) {
        if (!ok) {
            _elevationFailedAt.insert(elevationId, nowSeconds());
        }
        const QList<std::function<void(bool)>> callbacks = _elevationWaiters.take(elevationId);
        for (const auto& callback : callbacks) {
            callback(ok);
        }
    };

    _fetchTile(
        kElevationMapType, elevationId,
        [this, elevationId, finish](TileLoadOutcome outcome, const QByteArray& data, const QString&, bool fromNetwork) {
            if (outcome != TileLoadOutcome::Loaded) {
                finish(false);
                return;
            }
            const std::shared_ptr<Shared> shared = _shared;
            const QPointer<TileLoader> self(this);
            QThreadPool::globalInstance()->start([shared, self, elevationId, data, finish, fromNetwork] {
                const auto tile = ElevationCache::decode(elevationId, data);
                if (tile) {
                    shared->elevation.insert(tile);
                }
                if (self) {
                    QMetaObject::invokeMethod(
                        self,
                        [self, finish, elevationId, data, fromNetwork, ok = (tile != nullptr)] {
                            if (self) {
                                if (ok && fromNetwork) {
                                    self->_cacheTile(kElevationMapType, elevationId, data);
                                }
                                finish(ok);
                            }
                        },
                        Qt::QueuedConnection);
                }
            });
        });
}

void TileLoader::_fetchTile(const QString& mapType, const TileId& id, FetchCallback done)
{
    const int mapId = (mapType == _imageryMapType) ? _imageryMapId : _elevationMapId;
    if (mapId < 0) {
        done(TileLoadOutcome::Failed, {}, QStringLiteral("unknown map type %1").arg(mapType), false);
        return;
    }

    QGCFetchTileTask* const task = QGeoFileTileCacheQGC::createFetchTileTask(mapType, id.x, id.y, id.z);
    auto handled = std::make_shared<bool>(false);
    (void) connect(task, &QGCFetchTileTask::tileFetched, this,
                   [this, mapType, mapId, id, done, handled](QGCCacheTile* tile) {
                       const std::unique_ptr<QGCCacheTile> guard(tile);
                       if (std::exchange(*handled, true)) {
                           return;
                       }
                       if (!tile || tile->img.isEmpty() || isBingEmptyTile(mapId, tile->img)) {
                           _fetchFromNetwork(mapType, mapId, id, done);
                           return;
                       }
                       _cacheHits.ref();
                       done(TileLoadOutcome::Loaded, tile->img, {}, false);
                   });
    (void) connect(task, &QGCMapTask::error, this,
                   [this, mapType, mapId, id, done, handled](QGCMapTask::TaskType, const QString&) {
                       if (std::exchange(*handled, true)) {
                           return;
                       }
                       _fetchFromNetwork(mapType, mapId, id, done);
                   });
    if (!getQGCMapEngine()->addTask(task)) {
        task->deleteLater();
        _fetchFromNetwork(mapType, mapId, id, done);
    }
}

void TileLoader::_fetchFromNetwork(const QString& mapType, int mapId, const TileId& id, FetchCallback done)
{
    _networkFetches.ref();
    const QNetworkRequest request = QGeoTileFetcherQGC::getNetworkRequest(mapId, id.x, id.y, id.z);
    QNetworkReply* const reply = _network->get(request);
    _replies.insert(reply);

    (void) connect(reply, &QNetworkReply::finished, this, [this, reply, mapType, mapId, id, done] {
        _replies.remove(reply);
        reply->deleteLater();

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError) {
            const bool notFound = (status == 404) || (reply->error() == QNetworkReply::ContentNotFoundError);
            done(notFound ? TileLoadOutcome::NoData : TileLoadOutcome::Failed, {},
                 QStringLiteral("%1 (http %2)").arg(reply->errorString()).arg(status), true);
            return;
        }
        const QByteArray data = reply->readAll();
        if (data.isEmpty() || isBingEmptyTile(mapId, data)) {
            done(TileLoadOutcome::NoData, {}, QStringLiteral("no imagery"), true);
            return;
        }
        done(TileLoadOutcome::Loaded, data, {}, true);
    });
}

void TileLoader::_cacheTile(const QString& mapType, const TileId& id, const QByteArray& data)
{
    // Stored back so the shared cache serves this tile from now on, for the 2D map too
    QGeoFileTileCacheQGC::cacheTile(mapType, id.x, id.y, id.z, data, UrlFactory::getImageFormat(mapType, data));
}

}  // namespace Map3D
