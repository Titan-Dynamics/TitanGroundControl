#pragma once

#include <memory>
#include <rhi/qrhi.h>
#include <unordered_map>
#include <vector>

#include <QtCore/QElapsedTimer>
#include <QtCore/QFuture>
#include <QtCore/QHash>
#include <QtGui/QMatrix4x4>
#include <QtQuick/QQuickRhiItem>

#include "Map3DOverlays.h"
#include "Map3DTerrain.h"
#include "Map3DTileLoader.h"
#include "Map3DTypes.h"

/// Draws the Titan 3D map on the render thread: a port of Mission Planner's Map3D frame (Map3D.RenderFrame) and
/// terrain engine onto Qt's RHI. Pass 1 is the sky, terrain, ADS-B and clouds; pass 2 is the mission and the
/// vehicle, kept in front of the terrain by its own depth range (Mission Planner clears depth between the passes).
class Map3DRenderer : public QQuickRhiItemRenderer
{
public:
    Map3DRenderer();
    ~Map3DRenderer() override;

protected:
    void initialize(QRhiCommandBuffer* cb) override;
    void synchronize(QQuickRhiItem* item) override;
    void render(QRhiCommandBuffer* cb) override;

private:
    /// Deletes a QRhi resource when the frame being recorded ends rather than at once: a tile can be evicted or
    /// replaced in the frame that queued its texture upload, and QRhi reads the texture when it submits the batch.
    struct DeleteLater
    {
        void operator()(QRhiResource* resource) const { resource->deleteLater(); }
    };

    struct TileGpu
    {
        std::unique_ptr<QRhiTexture, DeleteLater> texture;
        std::unique_ptr<QRhiBuffer, DeleteLater> vertices;
        std::unique_ptr<QRhiBuffer, DeleteLater> uniforms;
        std::unique_ptr<QRhiShaderResourceBindings, DeleteLater> bindings;
        QRhiTexture* boundTexture = nullptr;
    };

    struct LineBatch
    {
        Map3D::LineList lines;
        std::unique_ptr<QRhiBuffer> vertices;
        std::unique_ptr<QRhiBuffer> uniforms;
        std::unique_ptr<QRhiShaderResourceBindings> bindings;
    };

    struct TexturedQuads
    {
        std::unique_ptr<QRhiTexture> texture;
        std::unique_ptr<QRhiShaderResourceBindings> bindings;
    };

    struct QuadDraw
    {
        TexturedQuads* quads = nullptr;
        int firstVertex = 0;
        bool home = false;
    };

    /// A vehicle model's vertices, shared by every vehicle drawn with it
    struct ModelMesh
    {
        std::unique_ptr<QRhiBuffer> vertices;
        int vertexCount = 0;
        float radius = 0;  ///< of its farthest vertex from its origin, in the STL's millimetres
    };

    /// The uniforms of one of the other vehicles' models
    struct GhostGpu
    {
        std::unique_ptr<QRhiBuffer> uniforms;
        std::unique_ptr<QRhiShaderResourceBindings> bindings;
    };

    struct GhostDraw
    {
        const ModelMesh* mesh = nullptr;
        size_t gpuIndex = 0;
    };

    /// Another vehicle's smoothed pose, and the terrain height last read under it
    struct OtherVehicleState
    {
        Map3D::VehicleTracker tracker;
        double groundHeight = 0;
        bool groundKnown = false;
    };

    // Scene
    bool _prepareView(double now);
    QPair<double, double> _followMap2D();
    void _updateOtherVehicles(double now);
    void _flyCamera(double now);
    void _updateGroundHeight(double lat, double lng);
    void _updateTerrain(const QMatrix4x4& terrainProjection, double viewRadius, double fovY,
                        QRhiResourceUpdateBatch* rub);
    void _uploadResults(double now, QRhiResourceUpdateBatch* rub);
    void _upload(Map3D::TerrainTile& tile, Map3D::TileLoadResult& result, QRhiResourceUpdateBatch* rub);
    void _releaseGpu(Map3D::TerrainTile& tile);
    void _releaseAllTerrain();
    void _evictOverBudget();
    QRhiBuffer* _indexBuffer(int gridN, QRhiResourceUpdateBatch* rub);
    double _terrainHeight(double lat, double lng, bool& known);
    QVector3D _missionPosition(const Map3D::MissionPoint& point);
    void _buildMission();
    void _buildMarkers(QRhiResourceUpdateBatch* rub);
    void _buildVehicleOverlays();
    void _buildAdsb(const QMatrix4x4& viewProjection, const QSize& size);
    void _updateStats(double viewRadius, double frameMs, double cpuMs);

    // GPU
    QRhiTexture::Format _targetFormat() const;
    void _createPipelines();
    void _releasePipelines();
    QRhiGraphicsPipeline* _pipeline(const QString& vertex, const QString& fragment, const QRhiVertexInputLayout& layout,
                                    QRhiShaderResourceBindings* layoutBindings, QRhiGraphicsPipeline::Topology topology,
                                    bool depthTest, bool depthWrite, bool blend, bool colorWrite = true);
    void _prepareLines(LineBatch& batch, const QMatrix4x4& mvp, const QSize& size, QRhiResourceUpdateBatch* rub);
    void _drawLines(QRhiCommandBuffer* cb, LineBatch& batch, QRhiGraphicsPipeline* pipeline,
                    const QRhiViewport& viewport);
    QRhiTexture* _labelTexture(const QString& label, QRhiResourceUpdateBatch* rub);
    TexturedQuads* _vehicleLabel(const QString& label, QRhiResourceUpdateBatch* rub);
    void _ensureQuadBindings(TexturedQuads& quads);
    const ModelMesh* _modelMesh(const QString& path, QRhiResourceUpdateBatch* rub);
    void _writeModelUniforms(QRhiBuffer* buffer, const Map3D::VehicleTracker& vehicle, const QMatrix4x4& viewProjection,
                             const QColor& color, QRhiResourceUpdateBatch* rub) const;
    void _buildOtherVehicles(const QMatrix4x4& viewProjection, const QSize& size, double now,
                             QRhiResourceUpdateBatch* rub);
    void _ensureClouds(QRhiResourceUpdateBatch* rub);

    QRhi* _rhi = nullptr;
    int _sampleCount = 0;
    QRhiTexture::Format _colorFormat = QRhiTexture::RGBA8;

    // Frame data from and to the item
    Map3D::FrameInput _input;
    Map3D::FrameOutput _output;
    std::shared_ptr<Map3D::TileLoader> _loader;

    // Scene state
    Map3D::WorldFrame _frame;
    Map3D::VehicleTracker _vehicle;
    Map3D::CameraRig _camera;
    Map3D::SelectionParams _selection;
    Map3D::TerrainQuadtree _quadtree{_frame};
    Map3D::Trail _trail;
    bool _hasViewCenter = false;
    double _viewLat = 0;
    double _viewLng = 0;
    double _lastMap2DLat = 0;
    double _lastMap2DLng = 0;
    bool _hasLastMap2D = false;
    double _lastMap2DChange = 0;
    double _groundHeight = 0;
    bool _groundHeightKnown = false;  ///< _groundHeight was read near the view centre
    double _groundLat = 0;
    double _groundLng = 0;
    QMatrix4x4 _view;
    QMatrix4x4 _overlayProjection;
    int _zoneVersion = -1;
    qint64 _gpuBytes = 0;
    std::unordered_map<Map3D::TileId, TileGpu, Map3D::TileIdHash> _tileGpu;  ///< move-only values
    std::vector<std::unique_ptr<QRhiBuffer>> _indexBuffers;                  ///< indexed by log2(gridN)
    std::vector<Map3D::TerrainTile*> _evictionCandidates;

    // Terrain stats
    qint64 _loadedTotal = 0;
    qint64 _loadedAtRateStart = 0;
    double _loadedPerSecond = 0;
    double _rateStart = 0;
    double _fetchMs = 0;
    double _buildMs = 0;
    double _uploadMs = 0;
    QString _lastError;

    // Pipelines and shared resources
    std::unique_ptr<QRhiSampler> _mipSampler;
    std::unique_ptr<QRhiSampler> _repeatSampler;
    std::unique_ptr<QRhiTexture> _dummyTexture;
    std::unique_ptr<QRhiBuffer> _dummyUniforms;
    std::unique_ptr<QRhiShaderResourceBindings> _textureLayout;  ///< uniform buffer + sampled texture
    std::unique_ptr<QRhiShaderResourceBindings> _uniformLayout;  ///< uniform buffer only
    std::unique_ptr<QRhiGraphicsPipeline> _skyPipeline;
    std::unique_ptr<QRhiGraphicsPipeline> _terrainPipeline;
    std::unique_ptr<QRhiGraphicsPipeline> _linePipeline;
    std::unique_ptr<QRhiGraphicsPipeline> _blendedLinePipeline;
    std::unique_ptr<QRhiGraphicsPipeline> _quadPipeline;
    std::unique_ptr<QRhiGraphicsPipeline> _quadNoDepthPipeline;
    std::unique_ptr<QRhiGraphicsPipeline> _modelPipeline;
    std::unique_ptr<QRhiGraphicsPipeline> _cloudPipeline;

    // Sky
    std::unique_ptr<QRhiBuffer> _skyVertices;
    std::unique_ptr<QRhiBuffer> _skyUniforms;
    std::unique_ptr<QRhiShaderResourceBindings> _skyBindings;

    // Lines: ADS-B (pass 1), plan and indicators, and the translucent trail (pass 2)
    LineBatch _adsbLines;
    LineBatch _planLines;
    LineBatch _indicatorLines;
    LineBatch _trailLines;

    // Waypoint markers and labels (pass 2)
    std::unique_ptr<QRhiBuffer> _quadVertices;
    std::unique_ptr<QRhiBuffer> _quadUniforms;
    TexturedQuads _markerQuads;
    TexturedQuads _markerAltQuads;
    QHash<QString, std::shared_ptr<TexturedQuads>> _labelQuads;
    QList<QuadDraw> _quadDraws;  ///< in draw order: far to near, home last

    // Vehicle models, by file, loaded off the render thread
    QHash<QString, std::shared_ptr<ModelMesh>> _modelMeshes;
    QHash<QString, QFuture<std::vector<float>>> _modelLoads;
    std::unique_ptr<QRhiBuffer> _modelUniforms;
    std::unique_ptr<QRhiShaderResourceBindings> _modelBindings;

    // The other vehicles, at half opacity: depth first, then the colour blended once over the nearest surface. Each
    // sits in a ring, under its name, that keeps it in sight however far off it is.
    QHash<int, OtherVehicleState> _otherVehicles;
    std::vector<GhostGpu> _ghostGpu;
    std::vector<GhostDraw> _ghostDraws;
    std::unique_ptr<QRhiGraphicsPipeline> _ghostDepthPipeline;
    std::unique_ptr<QRhiGraphicsPipeline> _ghostPipeline;
    LineBatch _otherVehicleLines;
    QHash<QString, std::shared_ptr<TexturedQuads>> _vehicleLabels;
    std::unique_ptr<QRhiBuffer> _vehicleLabelVertices;
    QList<QuadDraw> _vehicleLabelDraws;

    // Following another vehicle
    quint64 _vehicleGeneration = 0;
    bool _followingVehicle = false;  ///< the last frame chased a vehicle
    Map3D::CameraPose _lastCamera;   ///< as last drawn, where a flight to another vehicle starts
    Map3D::CameraFlight _cameraFlight;

    // Clouds
    std::unique_ptr<QRhiTexture> _cloudTexture;
    std::vector<float> _cloudDensity;
    std::vector<float> _cloudCoverages;
    int _cloudCoveragesDensity = -1;
    std::unique_ptr<QRhiBuffer> _cloudVertices;
    std::unique_ptr<QRhiBuffer> _cloudUniforms[3];
    std::unique_ptr<QRhiShaderResourceBindings> _cloudBindings[3];
    int _cloudLayersDrawn[3] = {};
    double _cloudBase = qQNaN();
    double _cloudLastTime = 0;

    // Frame timing
    QElapsedTimer _frameClock;
    double _frameMsAverage = 0;
    double _cpuMsAverage = 0;
    double _terrainMs = 0;
    QList<double> _frameTimes;
};
