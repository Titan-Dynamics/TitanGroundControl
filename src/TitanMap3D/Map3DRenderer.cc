#include "Map3DRenderer.h"

#include <algorithm>
#include <cmath>

#include <QtConcurrent/QtConcurrentRun>
#include <QtCore/QFile>
#include <QtCore/QTime>
#include <QtCore/QtMath>
#include <QtGui/QFont>
#include <QtGui/QImage>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>

#include "Map3DClouds.h"
#include "Map3DItem.h"
#include "Map3DModel.h"
#include "QGCLoggingCategory.h"

QGC_LOGGING_CATEGORY(Map3DRendererLog, "TitanMap3D.Renderer")

using namespace Map3D;

namespace {

constexpr double kUploadBudgetMs = 4;
constexpr qint64 kGpuBudgetBytes = 384LL * 1024 * 1024;
constexpr int kPruneAfterFrames = 2000;
constexpr double kRemeshRetrySeconds = 2;
constexpr double kNoDataRetrySeconds = 120;
constexpr double kOfflineRetrySeconds = 30;
constexpr double kMaxDeadReckonSeconds = 5;
/// Pass 2 (mission and vehicle) draws into [0, kOverlayDepth] of the depth range, always in front of pass 1
constexpr float kOverlayDepth = 0.05f;
constexpr double kMinMarkerDistance = 61.0;  // 200 ft
constexpr double kAdsbMaxDistance = 50000;
constexpr double kAdsbMaxAgeSeconds = 30;
constexpr int kTrailMaxPoints = 200 * 15;
constexpr int kLabelTextureSize = 128;
constexpr float kLightDirection[3] = {0.4f, 0.6f, 0.7f};
constexpr int kTerrainUniformSize = 64 + 64 + 16 + 16;
constexpr int kLineUniformSize = 64 + 16;
constexpr int kModelUniformSize = 64 + 64 + 16 + 16;
constexpr int kCloudUniformSize = 64 + 9 * 16;
constexpr int kQuadUniformSize = 64;
constexpr int kSkyUniformSize = 32;

/// How far terrain is drawn: a little past the horizon for the camera's height, between 30 and 150 km.
double viewRadiusFor(double heightAboveGround)
{
    const double horizon = 3570 * std::sqrt(std::max(1.0, heightAboveGround));
    return std::clamp(horizon * 1.15, 30000.0, 150000.0);
}

/// Haze starts 30% of the way out and is opaque at 90% of the view radius, hiding the terrain's edge.
void fogRange(double viewRadius, float& start, float& end)
{
    start = static_cast<float>(viewRadius * 0.30);
    end = static_cast<float>(viewRadius * 0.90);
}

QShader loadShader(const QString& name)
{
    QFile file(QStringLiteral(":/titanmap3d/shaders/%1.qsb").arg(name));
    if (!file.open(QFile::ReadOnly)) {
        qCWarning(Map3DRendererLog) << "missing shader" << name;
        return {};
    }
    return QShader::fromSerialized(file.readAll());
}

void writeMatrix(float* out, const QMatrix4x4& m)
{
    std::copy(m.constData(), m.constData() + 16, out);
}

void writeVec4(float* out, float x, float y, float z, float w)
{
    out[0] = x;
    out[1] = y;
    out[2] = z;
    out[3] = w;
}

void writeColor(float* out, const QColor& color)
{
    writeVec4(out, color.redF(), color.greenF(), color.blueF(), color.alphaF());
}

float wrap(double value)
{
    return static_cast<float>(value - std::floor(value));
}

QImage labelImage(const QString& label)
{
    // White Arial Bold with a black outline, centred in a square, as Mission Planner draws its labels
    QImage image(kLabelTextureSize, kLabelTextureSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    QFont font(QStringLiteral("Arial"));
    font.setBold(true);
    font.setPixelSize(48);
    QPainterPath path;
    path.addText(0, 0, font, label);
    const QRectF bounds = path.boundingRect();
    path.translate(kLabelTextureSize / 2.0 - bounds.center().x(), kLabelTextureSize / 2.0 - bounds.center().y());
    painter.setPen(QPen(Qt::black, 6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);
    painter.fillPath(path, Qt::white);
    painter.end();
    return image.convertToFormat(QImage::Format_RGBA8888);
}

}  // namespace

Map3DRenderer::Map3DRenderer() = default;

Map3DRenderer::~Map3DRenderer()
{
    if (_modelFuture.isRunning()) {
        _modelFuture.waitForFinished();
    }
}

// ---------------------------------------------------------------------------
// Setup

void Map3DRenderer::initialize(QRhiCommandBuffer* cb)
{
    Q_UNUSED(cb);
    if (_rhi != rhi()) {
        _releasePipelines();
        _releaseAllTerrain();
        _rhi = rhi();
    }
    if ((_sampleCount != renderTarget()->sampleCount()) || (_colorFormat != _targetFormat()) || !_terrainPipeline) {
        _sampleCount = renderTarget()->sampleCount();
        _colorFormat = _targetFormat();
        _createPipelines();
    }
}

QRhiTexture::Format Map3DRenderer::_targetFormat() const
{
    // With multisampling the item draws into a multisample buffer resolved into resolveTexture()
    const QRhiTexture* const texture = colorTexture() ? colorTexture() : resolveTexture();
    return texture ? texture->format() : QRhiTexture::RGBA8;
}

void Map3DRenderer::_releasePipelines()
{
    _skyPipeline.reset();
    _terrainPipeline.reset();
    _linePipeline.reset();
    _blendedLinePipeline.reset();
    _quadPipeline.reset();
    _quadNoDepthPipeline.reset();
    _modelPipeline.reset();
    _cloudPipeline.reset();
}

QRhiGraphicsPipeline* Map3DRenderer::_pipeline(const QString& vertex, const QString& fragment,
                                               const QRhiVertexInputLayout& layout,
                                               QRhiShaderResourceBindings* layoutBindings,
                                               QRhiGraphicsPipeline::Topology topology, bool depthTest, bool depthWrite,
                                               bool blend)
{
    QRhiGraphicsPipeline* const pipeline = _rhi->newGraphicsPipeline();
    pipeline->setShaderStages(
        {{QRhiShaderStage::Vertex, loadShader(vertex)}, {QRhiShaderStage::Fragment, loadShader(fragment)}});
    pipeline->setVertexInputLayout(layout);
    pipeline->setShaderResourceBindings(layoutBindings);
    pipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
    pipeline->setSampleCount(_sampleCount);
    pipeline->setTopology(topology);
    pipeline->setCullMode(QRhiGraphicsPipeline::None);
    pipeline->setDepthTest(depthTest);
    pipeline->setDepthWrite(depthWrite);
    pipeline->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
    if (blend) {
        QRhiGraphicsPipeline::TargetBlend targetBlend;
        targetBlend.enable = true;
        targetBlend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
        targetBlend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        targetBlend.srcAlpha = QRhiGraphicsPipeline::One;
        targetBlend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        pipeline->setTargetBlends({targetBlend});
    }
    if (!pipeline->create()) {
        qCWarning(Map3DRendererLog) << "pipeline creation failed for" << vertex << fragment;
    }
    return pipeline;
}

void Map3DRenderer::_createPipelines()
{
    _releasePipelines();
    const auto vertexAndFragment = QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage;

    if (!_mipSampler) {
        _mipSampler.reset(_rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::Linear,
                                           QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
        _mipSampler->create();
        _repeatSampler.reset(_rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::Linear,
                                              QRhiSampler::Repeat, QRhiSampler::Repeat));
        _repeatSampler->create();
        _dummyTexture.reset(_rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1)));
        _dummyTexture->create();
        _dummyUniforms.reset(_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kCloudUniformSize));
        _dummyUniforms->create();
        _textureLayout.reset(_rhi->newShaderResourceBindings());
        _textureLayout->setBindings(
            {QRhiShaderResourceBinding::uniformBuffer(0, vertexAndFragment, _dummyUniforms.get()),
             QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, _dummyTexture.get(),
                                                       _mipSampler.get())});
        _textureLayout->create();
        _uniformLayout.reset(_rhi->newShaderResourceBindings());
        _uniformLayout->setBindings(
            {QRhiShaderResourceBinding::uniformBuffer(0, vertexAndFragment, _dummyUniforms.get())});
        _uniformLayout->create();
    }

    QRhiVertexInputLayout skyLayout;
    skyLayout.setBindings({{5 * sizeof(float)}});
    skyLayout.setAttributes(
        {{0, 0, QRhiVertexInputAttribute::Float2, 0}, {0, 1, QRhiVertexInputAttribute::Float3, 2 * sizeof(float)}});
    _skyPipeline.reset(_pipeline(QStringLiteral("sky.vert"), QStringLiteral("sky.frag"), skyLayout,
                                 _uniformLayout.get(), QRhiGraphicsPipeline::TriangleStrip, false, false, false));

    QRhiVertexInputLayout terrainLayout;
    terrainLayout.setBindings({{TileMeshBuilder::kFloatsPerVertex * sizeof(float)}});
    terrainLayout.setAttributes(
        {{0, 0, QRhiVertexInputAttribute::Float3, 0}, {0, 1, QRhiVertexInputAttribute::Float2, 3 * sizeof(float)}});
    _terrainPipeline.reset(_pipeline(QStringLiteral("terrain.vert"), QStringLiteral("terrain.frag"), terrainLayout,
                                     _textureLayout.get(), QRhiGraphicsPipeline::Triangles, true, true, false));

    QRhiVertexInputLayout lineLayout;
    lineLayout.setBindings({{LineList::kFloatsPerVertex * sizeof(float)}});
    lineLayout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float3, 0},
                              {0, 1, QRhiVertexInputAttribute::Float3, 3 * sizeof(float)},
                              {0, 2, QRhiVertexInputAttribute::Float2, 6 * sizeof(float)},
                              {0, 3, QRhiVertexInputAttribute::Float4, 8 * sizeof(float)},
                              {0, 4, QRhiVertexInputAttribute::Float, 12 * sizeof(float)}});
    _linePipeline.reset(_pipeline(QStringLiteral("line.vert"), QStringLiteral("color.frag"), lineLayout,
                                  _uniformLayout.get(), QRhiGraphicsPipeline::Triangles, true, true, false));
    _blendedLinePipeline.reset(_pipeline(QStringLiteral("line.vert"), QStringLiteral("color.frag"), lineLayout,
                                         _uniformLayout.get(), QRhiGraphicsPipeline::Triangles, true, true, true));

    QRhiVertexInputLayout quadLayout;
    quadLayout.setBindings({{5 * sizeof(float)}});
    quadLayout.setAttributes(
        {{0, 0, QRhiVertexInputAttribute::Float3, 0}, {0, 1, QRhiVertexInputAttribute::Float2, 3 * sizeof(float)}});
    _quadPipeline.reset(_pipeline(QStringLiteral("quad.vert"), QStringLiteral("quad.frag"), quadLayout,
                                  _textureLayout.get(), QRhiGraphicsPipeline::Triangles, true, false, true));
    _quadNoDepthPipeline.reset(_pipeline(QStringLiteral("quad.vert"), QStringLiteral("quad.frag"), quadLayout,
                                         _textureLayout.get(), QRhiGraphicsPipeline::Triangles, false, false, true));

    QRhiVertexInputLayout modelLayout;
    modelLayout.setBindings({{6 * sizeof(float)}});
    modelLayout.setAttributes(
        {{0, 0, QRhiVertexInputAttribute::Float3, 0}, {0, 1, QRhiVertexInputAttribute::Float3, 3 * sizeof(float)}});
    _modelPipeline.reset(_pipeline(QStringLiteral("model.vert"), QStringLiteral("color.frag"), modelLayout,
                                   _uniformLayout.get(), QRhiGraphicsPipeline::Triangles, true, true, false));

    QRhiVertexInputLayout cloudLayout;
    cloudLayout.setBindings({{3 * sizeof(float)}});
    cloudLayout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float3, 0}});
    _cloudPipeline.reset(_pipeline(QStringLiteral("cloud.vert"), QStringLiteral("cloud.frag"), cloudLayout,
                                   _textureLayout.get(), QRhiGraphicsPipeline::TriangleStrip, true, false, true));
}

// ---------------------------------------------------------------------------
// Frame data

void Map3DRenderer::synchronize(QQuickRhiItem* rhiItem)
{
    Map3DItem* const item = static_cast<Map3DItem*>(rhiItem);
    _input = item->frameInput();
    if (!_loader || (_loader->imageryMapType() != _input.imageryMapType)) {
        if (_loader) {
            _releaseAllTerrain();
        }
        _loader = item->tileLoader();
    }
    item->setFrameOutput(_output);
}

// ---------------------------------------------------------------------------
// The frame (Mission Planner's Map3D.RenderFrame)

void Map3DRenderer::render(QRhiCommandBuffer* cb)
{
    const double interval = _frameClock.isValid() ? _frameClock.nsecsElapsed() / 1e6 : 0;
    _frameClock.restart();
    QElapsedTimer cpuClock;
    cpuClock.start();
    const double now = nowSeconds();

    QRhiResourceUpdateBatch* const rub = _rhi->nextResourceUpdateBatch();
    QRhiRenderTarget* const target = renderTarget();
    const QSize size = target->pixelSize();
    const QColor horizon = _input.skyHorizon;
    _output.adsb.clear();
    _output.markersDrawn = 0;

    if (!_loader || !_terrainPipeline || !_prepareView(now)) {
        cb->beginPass(target, horizon, {1.0f, 0}, rub);
        cb->endPass();
        _output.terrainBusy = true;
        return;
    }

    const double heightAboveGround = std::max(1.0, _camera.z - _groundHeight);
    const double viewRadius = viewRadiusFor(heightAboveGround);
    const double fovDegrees = _input.fieldOfView;
    const float aspect = static_cast<float>(size.width()) / std::max(1, size.height());

    // Terrain gets a near plane scaled to altitude for depth precision; overlays a close near plane
    const float terrainNear = static_cast<float>(std::clamp(heightAboveGround * 0.05, 0.25, 25.0));
    const float farPlane = static_cast<float>(viewRadius * 1.05);
    QMatrix4x4 terrainProjection;
    terrainProjection.perspective(fovDegrees, aspect, terrainNear, farPlane);
    _overlayProjection.setToIdentity();
    _overlayProjection.perspective(fovDegrees, aspect, 0.1f, farPlane);
    _view = _camera.viewMatrix();

    QElapsedTimer terrainClock;
    terrainClock.start();
    _updateTerrain(terrainProjection, viewRadius, qDegreesToRadians(fovDegrees), rub);
    _terrainMs = terrainClock.nsecsElapsed() / 1e6;

    const QMatrix4x4 correction = _rhi->clipSpaceCorrMatrix();
    const QMatrix4x4 terrainClip = correction * terrainProjection;
    const QMatrix4x4 overlayClip = correction * _overlayProjection;
    const QRhiViewport terrainViewport(0, 0, size.width(), size.height(), kOverlayDepth, 1.0f);
    const QRhiViewport overlayViewport(0, 0, size.width(), size.height(), 0.0f, kOverlayDepth);
    float fogStart = 0;
    float fogEnd = 0;
    fogRange(viewRadius, fogStart, fogEnd);

    // ---- Sky: corners carry their world view direction (translation dropped)
    QMatrix4x4 rotationOnly = _view;
    rotationOnly.setColumn(3, QVector4D(0, 0, 0, 1));
    const QMatrix4x4 inverse = (terrainProjection * rotationOnly).inverted();
    float skyVertices[4 * 5];
    for (int corner = 0; corner < 4; corner++) {
        const float x = (corner & 1) == 0 ? -1 : 1;
        const float y = (corner & 2) == 0 ? -1 : 1;
        const QVector4D direction = inverse.map(QVector4D(x, y, 1, 1));
        skyVertices[corner * 5] = x;
        skyVertices[corner * 5 + 1] = _rhi->isYUpInNDC() ? y : -y;
        skyVertices[corner * 5 + 2] = direction.x() / direction.w();
        skyVertices[corner * 5 + 3] = direction.y() / direction.w();
        skyVertices[corner * 5 + 4] = direction.z() / direction.w();
    }
    if (!_skyVertices) {
        _skyVertices.reset(_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, sizeof(skyVertices)));
        _skyVertices->create();
        _skyUniforms.reset(_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kSkyUniformSize));
        _skyUniforms->create();
        _skyBindings.reset(_rhi->newShaderResourceBindings());
        _skyBindings->setBindings({QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, _skyUniforms.get())});
        _skyBindings->create();
    }
    rub->updateDynamicBuffer(_skyVertices.get(), 0, sizeof(skyVertices), skyVertices);
    float skyUniforms[8];
    writeColor(skyUniforms, _input.skyTop);
    writeColor(skyUniforms + 4, horizon);
    rub->updateDynamicBuffer(_skyUniforms.get(), 0, sizeof(skyUniforms), skyUniforms);

    // ---- Terrain tile uniforms
    QList<QPair<TileGpu*, int>> terrainDraws;
    for (const TileId& id : _quadtree.drawList()) {
        TerrainTile* const tile = _quadtree.find(id);
        const auto gpu = _tileGpu.find(id);
        if (!tile || (gpu == _tileGpu.end()) || !gpu->second.vertices || !gpu->second.texture) {
            continue;
        }
        QMatrix4x4 model;
        model.translate(static_cast<float>(tile->originEasting - _frame.originEasting()),
                        static_cast<float>(tile->originNorthing - _frame.originNorthing()), 0);
        float uniforms[kTerrainUniformSize / 4];
        writeMatrix(uniforms, _view * model);
        writeMatrix(uniforms + 16, terrainClip);
        writeColor(uniforms + 32, horizon);
        writeVec4(uniforms + 36, fogStart, fogEnd, 0, 0);
        rub->updateDynamicBuffer(gpu->second.uniforms.get(), 0, sizeof(uniforms), uniforms);
        _indexBuffer(tile->gridN, rub);
        terrainDraws.append({&gpu->second, tile->gridN});
    }

    // ---- ADS-B, depth-tested against the terrain
    _adsbLines.lines.clear();
    _buildAdsb(terrainClip * _view, size);
    _prepareLines(_adsbLines, terrainClip * _view, size, rub);

    // ---- Clouds: after the terrain (which hides them behind hills), highest layer first
    for (int& drawn : _cloudLayersDrawn) {
        drawn = 0;
    }
    if (_input.cloudDensity > 0) {
        _ensureClouds(rub);
        const double cloudNow = nowSeconds();
        const double dt = std::min(1.0, cloudNow - _cloudLastTime);
        _cloudLastTime = cloudNow;
        const double target = Clouds::baseAltitude(_groundHeight, _input.connected ? _vehicle.z : _camera.z);
        _cloudBase = std::isnan(_cloudBase) ? target : _cloudBase + (target - _cloudBase) * (1 - std::exp(-dt / 3.0));
        const double fadeEnd = viewRadius * Clouds::kFadeEnd;

        float cloudVertices[3 * 4 * 3];
        for (int i = Clouds::kLayerCount - 1; i >= 0; i--) {
            const Clouds::Layer& layer = Clouds::kLayers[i];
            const float z = static_cast<float>(_cloudBase + layer.height);
            for (int corner = 0; corner < 4; corner++) {
                float* v = cloudVertices + (i * 4 + corner) * 3;
                v[0] = static_cast<float>(_camera.x + ((corner & 1) == 0 ? -fadeEnd : fadeEnd));
                v[1] = static_cast<float>(_camera.y + ((corner & 2) == 0 ? -fadeEnd : fadeEnd));
                v[2] = z;
            }
            if (z < _camera.z) {
                continue;
            }
            const double scaleX = 1 / layer.scaleX;
            const double scaleY = 1 / layer.scaleY;
            const double detail = 1 / layer.detailScale;
            // The scene origin's place in the texture for each sampling, carried back by the wind
            const double driftX = layer.windX * cloudNow;
            const double driftY = layer.windY * cloudNow;
            const double detailDriftX = driftX * Clouds::kDetailDrift;
            const double detailDriftY = driftY * Clouds::kDetailDrift;
            const double weatherDriftX = driftX * Clouds::kWeatherDrift;
            const double weatherDriftY = driftY * Clouds::kWeatherDrift;
            const double originE = _frame.originEasting();
            const double originN = _frame.originNorthing();

            float uniforms[kCloudUniformSize / 4];
            writeMatrix(uniforms, terrainClip * _view);
            writeVec4(uniforms + 16, static_cast<float>(_camera.x), static_cast<float>(_camera.y),
                      static_cast<float>(_camera.z), 0);
            writeVec4(uniforms + 20, static_cast<float>(scaleX), static_cast<float>(scaleY),
                      wrap((originE - driftX) * scaleX), wrap((originN - driftY) * scaleY));
            writeVec4(uniforms + 24, static_cast<float>(detail), wrap((originE - detailDriftX) * scaleX * detail),
                      wrap((originN - detailDriftY) * scaleY * detail), Clouds::kDetailWeight);
            writeVec4(uniforms + 28, static_cast<float>(Clouds::kWeatherScale),
                      wrap((originE - weatherDriftX) * scaleX * Clouds::kWeatherScale),
                      wrap((originN - weatherDriftY) * scaleY * Clouds::kWeatherScale), Clouds::kWeatherStrength);
            writeVec4(uniforms + 32, _cloudCoverages[i], layer.softness, layer.opacity, 0);
            writeVec4(uniforms + 36, static_cast<float>(viewRadius * Clouds::kFadeStart), static_cast<float>(fadeEnd),
                      fogStart, fogEnd);
            writeColor(uniforms + 40, horizon);
            writeVec4(uniforms + 44, Clouds::kLitColor[0], Clouds::kLitColor[1], Clouds::kLitColor[2], 1);
            writeVec4(uniforms + 48, Clouds::kShadeColor[0], Clouds::kShadeColor[1], Clouds::kShadeColor[2], 1);
            rub->updateDynamicBuffer(_cloudUniforms[i].get(), 0, sizeof(uniforms), uniforms);
            _cloudLayersDrawn[i] = 1;
        }
        rub->updateDynamicBuffer(_cloudVertices.get(), 0, sizeof(cloudVertices), cloudVertices);
    }

    // ---- Pass 2: mission and vehicle
    const QMatrix4x4 overlayViewProjection = overlayClip * _view;
    _planLines.lines.clear();
    _indicatorLines.lines.clear();
    _trailLines.lines.clear();
    _buildMission();
    _buildMarkers(rub);
    if (_input.connected) {
        _buildVehicleOverlays();
    }
    _prepareLines(_planLines, overlayViewProjection, size, rub);
    _prepareLines(_indicatorLines, overlayViewProjection, size, rub);
    _prepareLines(_trailLines, overlayViewProjection, size, rub);

    const bool drawModel = _input.connected && !_input.fpvMode;
    if (drawModel) {
        _ensureModel(rub);
    }
    if (drawModel && _modelVertexCount > 0) {
        QMatrix4x4 model;
        model.translate(static_cast<float>(_vehicle.x), static_cast<float>(_vehicle.y), static_cast<float>(_vehicle.z));
        model.rotate(-_vehicle.yaw, 0, 0, 1);
        model.rotate(_vehicle.pitch, 1, 0, 0);
        model.rotate(_vehicle.roll, 0, 1, 0);
        // The STL is in millimetres
        const float scale = static_cast<float>(_input.vehicleScale / 1000.0);
        model.scale(scale);
        const float lightLength =
            std::sqrt(kLightDirection[0] * kLightDirection[0] + kLightDirection[1] * kLightDirection[1] +
                      kLightDirection[2] * kLightDirection[2]);
        float uniforms[kModelUniformSize / 4];
        writeMatrix(uniforms, model);
        writeMatrix(uniforms + 16, overlayViewProjection * model);
        writeVec4(uniforms + 32, kLightDirection[0] / lightLength, kLightDirection[1] / lightLength,
                  kLightDirection[2] / lightLength, 0);
        writeColor(uniforms + 36, _input.vehicleColor);
        rub->updateDynamicBuffer(_modelUniforms.get(), 0, sizeof(uniforms), uniforms);
    }

    // ---- Record
    cb->beginPass(target, horizon, {1.0f, 0}, rub);

    cb->setGraphicsPipeline(_skyPipeline.get());
    cb->setViewport(terrainViewport);
    cb->setShaderResources(_skyBindings.get());
    const QRhiCommandBuffer::VertexInput skyInput(_skyVertices.get(), 0);
    cb->setVertexInput(0, 1, &skyInput);
    cb->draw(4);

    if (!terrainDraws.isEmpty()) {
        cb->setGraphicsPipeline(_terrainPipeline.get());
        cb->setViewport(terrainViewport);
        for (const auto& [gpu, gridN] : std::as_const(terrainDraws)) {
            cb->setShaderResources(gpu->bindings.get());
            const QRhiCommandBuffer::VertexInput input(gpu->vertices.get(), 0);
            cb->setVertexInput(0, 1, &input, _indexBuffer(gridN, nullptr), 0, QRhiCommandBuffer::IndexUInt16);
            cb->drawIndexed(TileMeshBuilder::indexCount(gridN));
        }
    }

    _drawLines(cb, _adsbLines, _linePipeline.get(), terrainViewport);

    if (_input.cloudDensity > 0) {
        cb->setGraphicsPipeline(_cloudPipeline.get());
        cb->setViewport(terrainViewport);
        const QRhiCommandBuffer::VertexInput cloudInput(_cloudVertices.get(), 0);
        cb->setVertexInput(0, 1, &cloudInput);
        for (int i = Clouds::kLayerCount - 1; i >= 0; i--) {
            if (_cloudLayersDrawn[i]) {
                cb->setShaderResources(_cloudBindings[i].get());
                cb->draw(4, 1, i * 4, 0);
            }
        }
    }

    _drawLines(cb, _planLines, _linePipeline.get(), overlayViewport);

    if (!_quadDraws.isEmpty() && _quadVertices) {
        const QRhiCommandBuffer::VertexInput quadInput(_quadVertices.get(), 0);
        bool homePhase = false;
        cb->setGraphicsPipeline(_quadPipeline.get());
        cb->setViewport(overlayViewport);
        for (const QuadDraw& draw : std::as_const(_quadDraws)) {
            if (draw.home && !homePhase) {
                // Home is on the ground, so it would be half hidden by the terrain-level depth; always drawn whole
                homePhase = true;
                cb->setGraphicsPipeline(_quadNoDepthPipeline.get());
                cb->setViewport(overlayViewport);
            }
            cb->setShaderResources(draw.quads->bindings.get());
            cb->setVertexInput(0, 1, &quadInput);
            cb->draw(6, 1, draw.firstVertex, 0);
        }
    }

    _drawLines(cb, _indicatorLines, _linePipeline.get(), overlayViewport);
    _drawLines(cb, _trailLines, _blendedLinePipeline.get(), overlayViewport);

    if (drawModel && (_modelVertexCount > 0)) {
        cb->setGraphicsPipeline(_modelPipeline.get());
        cb->setViewport(overlayViewport);
        cb->setShaderResources(_modelBindings.get());
        const QRhiCommandBuffer::VertexInput modelInput(_modelVertices.get(), 0);
        cb->setVertexInput(0, 1, &modelInput);
        cb->draw(_modelVertexCount);
    }

    cb->endPass();

    _output.terrainBusy = (_loader->pendingCount() + _loader->inFlightCount() > 0) || _loader->hasResults();
    const double cpuMs = cpuClock.nsecsElapsed() / 1e6;
    _updateStats(viewRadius, interval, cpuMs);
}

bool Map3DRenderer::_prepareView(double now)
{
    if (_input.connected) {
        _viewLat = _input.fix.lat;
        _viewLng = _input.fix.lng;
        _hasViewCenter = true;
    } else {
        const auto [lat, lng] = _followMap2D();
        if (_hasViewCenter || ((lat != 0) || (lng != 0))) {
            _viewLat = lat;
            _viewLng = lng;
            _hasViewCenter = (lat != 0) || (lng != 0);
        }
    }
    if (!_hasViewCenter) {
        return false;
    }

    _frame.update(_viewLat, _viewLng);
    _updateGroundHeight(_viewLat, _viewLng);

    if (_input.connected) {
        // The vehicle is drawn at the terrain under it plus its height above home, so it sits on the rendered ground
        Fix fix = _input.fix;
        fix.alt = _groundHeight + _input.relativeAltitude;
        fix.timeSeconds = std::max(fix.timeSeconds, now - kMaxDeadReckonSeconds);
        _vehicle.update(_frame, fix, _input.roll, _input.pitch, _input.yaw, _groundHeight, _input.navBearing,
                        _input.groundCourse, _input.smoothMovement, _input.armed, now);
        _trail.record(_frame, _vehicle, _input.showTrail, _input.armed, kTrailMaxPoints);

        // In FPV the eye stays a metre above the ground under the vehicle, or a landed vehicle would look out from
        // level with the terrain mesh
        if (_input.fpvMode) {
            _camera.firstPerson(_vehicle, _groundHeight + 1);
        } else {
            _camera.chase(_vehicle, _input.cameraDistance, _input.cameraHeight, _input.cameraAngle);
        }
    } else {
        // Free-look from 100 m above the 2D map's centre, starting from the last known heading
        _vehicle.reset();
        double x = 0;
        double y = 0;
        _frame.toLocal(_viewLat, _viewLng, x, y);
        _camera.look(x, y, _groundHeight + 100, _vehicle.yaw + _input.freeLookYaw, _input.freeLookPitch);
    }
    return true;
}

QPair<double, double> Map3DRenderer::_followMap2D()
{
    // While disconnected the view follows the 2D map: immediately after a big jump, otherwise once it has settled
    // for a second so panning the map doesn't thrash the terrain
    double currentLat = _viewLat;
    double currentLng = _viewLng;
    const bool hasCurrent = _hasViewCenter;
    if (!_input.hasMap2DCenter) {
        return {currentLat, currentLng};
    }
    const double mapLat = _input.map2DLat;
    const double mapLng = _input.map2DLng;
    const double now = nowSeconds();
    if (!hasCurrent) {
        _lastMap2DLat = mapLat;
        _lastMap2DLng = mapLng;
        _hasLastMap2D = true;
        _lastMap2DChange = now;
        return {mapLat, mapLng};
    }

    const bool bigJump = distanceMeters(currentLat, currentLng, mapLat, mapLng) > 1000;
    if (!_hasLastMap2D || (distanceMeters(_lastMap2DLat, _lastMap2DLng, mapLat, mapLng) > 30)) {
        _lastMap2DLat = mapLat;
        _lastMap2DLng = mapLng;
        _hasLastMap2D = true;
        _lastMap2DChange = now;
    }
    const bool settled = (now - _lastMap2DChange) >= 1.0;
    if ((distanceMeters(currentLat, currentLng, _lastMap2DLat, _lastMap2DLng) > 30) && (bigJump || settled)) {
        return {_lastMap2DLat, _lastMap2DLng};
    }
    return {currentLat, currentLng};
}

void Map3DRenderer::_updateGroundHeight(double lat, double lng)
{
    // Keeps the last known ground height until the data is in memory; never blocks the render thread
    double ground = 0;
    if (_loader->elevation().tryHeight(lat, lng, ground)) {
        _groundHeight = ground;
    }
    _loader->requestPointElevation(lat, lng);
}

double Map3DRenderer::_terrainHeight(double lat, double lng, bool& known)
{
    double height = 0;
    known = _loader->elevation().tryHeight(lat, lng, height);
    if (!known) {
        _loader->requestPointElevation(lat, lng);
        return 0;
    }
    return height;
}

// ---------------------------------------------------------------------------
// Terrain (Mission Planner's TerrainEngine)

void Map3DRenderer::_updateTerrain(const QMatrix4x4& terrainProjection, double viewRadius, double fovY,
                                   QRhiResourceUpdateBatch* rub)
{
    if (_frame.zoneVersion() != _zoneVersion) {
        // Built geometry belongs to the old zone
        _releaseAllTerrain();
        _zoneVersion = _frame.zoneVersion();
    }

    const double now = nowSeconds();
    _uploadResults(now, rub);

    double cameraLat = 0;
    double cameraLng = 0;
    _frame.toLatLng(_camera.x, _camera.y, cameraLat, cameraLng);
    double fx = 0;
    double fy = 0;
    double fz = 0;
    _camera.forward(fx, fy, fz);

    _selection.cameraX = _camera.x;
    _selection.cameraY = _camera.y;
    _selection.cameraZ = _camera.z;
    _selection.forwardX = fx;
    _selection.forwardY = fy;
    _selection.forwardZ = fz;
    _selection.cameraLat = cameraLat;
    _selection.cameraLng = cameraLng;
    _selection.frustum = Frustum::fromViewProjection(terrainProjection * _view);
    _selection.hasFrustum = true;
    _selection.viewRadius = viewRadius;
    _selection.viewportHeight = renderTarget()->pixelSize().height();
    _selection.fovY = fovY;
    _selection.referenceHeight = _groundHeight;
    _selection.maxZoom = SelectionParams::maxZoomFor(_camera.z - _groundHeight, _selection.maxZoom);

    _quadtree.select(_selection, now);
    _loader->setRequests(_quadtree.requests());

    _evictOverBudget();
    if (_quadtree.frame() % 120 == 0) {
        _quadtree.pruneStale(kPruneAfterFrames, now);
    }
}

void Map3DRenderer::_uploadResults(double now, QRhiResourceUpdateBatch* rub)
{
    QElapsedTimer clock;
    clock.start();
    TileLoadResult result;
    while ((clock.nsecsElapsed() / 1e6 < kUploadBudgetMs) && _loader->takeResult(result)) {
        if (result.zoneVersion != _zoneVersion) {
            continue;
        }
        TerrainTile& tile = _quadtree.getOrCreate(result.id);
        switch (result.outcome) {
            case TileLoadOutcome::Loaded:
                _upload(tile, result, rub);
                _loadedTotal++;
                if (result.kind == TileJobKind::Full) {
                    _fetchMs = (_fetchMs <= 0) ? result.fetchMs : _fetchMs * 0.9 + result.fetchMs * 0.1;
                }
                _buildMs = (_buildMs <= 0) ? result.buildMs : _buildMs * 0.9 + result.buildMs * 0.1;
                break;
            case TileLoadOutcome::StillPending:
                tile.remeshAt = now + kRemeshRetrySeconds;
                break;
            case TileLoadOutcome::NoData:
                // Usually permanent (no imagery at this zoom), but check again now and then
                tile.state = TileState::NoData;
                tile.retryAt = now + kNoDataRetrySeconds;
                break;
            case TileLoadOutcome::Offline:
                if (tile.state != TileState::Ready) {
                    tile.state = TileState::Unavailable;
                    tile.retryAt = now + kOfflineRetrySeconds;
                }
                break;
            case TileLoadOutcome::Failed:
                _lastError = result.error;
                if (tile.state == TileState::Ready) {
                    // A remesh failed; keep the tile and try again later
                    tile.remeshAt = now + kRemeshRetrySeconds;
                } else {
                    tile.state = TileState::Failed;
                    tile.failCount++;
                    tile.retryAt = now + std::min(60, 2 << std::min(5, tile.failCount));
                }
                break;
        }
    }
    _uploadMs = clock.nsecsElapsed() / 1e6;
}

void Map3DRenderer::_upload(TerrainTile& tile, TileLoadResult& result, QRhiResourceUpdateBatch* rub)
{
    TileGpu& gpu = _tileGpu[tile.id];
    const TileMesh& mesh = result.mesh;

    if (!result.pixels.isEmpty()) {
        gpu.texture.reset(_rhi->newTexture(QRhiTexture::RGBA8, QSize(result.width, result.height), 1,
                                           QRhiTexture::MipMapped | QRhiTexture::UsedWithGenerateMips));
        gpu.texture->create();
        rub->uploadTexture(gpu.texture.get(), QRhiTextureUploadDescription(QRhiTextureUploadEntry(
                                                  0, 0, QRhiTextureSubresourceUploadDescription(result.pixels))));
        rub->generateMips(gpu.texture.get());
    }
    if (!gpu.texture) {
        // A remesh for a tile whose texture was evicted meanwhile: load it again from scratch
        _releaseGpu(tile);
        return;
    }

    const quint32 vertexBytes = static_cast<quint32>(mesh.vertices.size() * sizeof(float));
    if (!gpu.vertices || (gpu.vertices->size() != vertexBytes)) {
        gpu.vertices.reset(_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, vertexBytes));
        gpu.vertices->create();
    }
    rub->uploadStaticBuffer(gpu.vertices.get(), mesh.vertices.data());
    if (!gpu.uniforms) {
        gpu.uniforms.reset(_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kTerrainUniformSize));
        gpu.uniforms->create();
    }
    if (!gpu.bindings || (gpu.boundTexture != gpu.texture.get())) {
        gpu.bindings.reset(_rhi->newShaderResourceBindings());
        gpu.bindings->setBindings(
            {QRhiShaderResourceBinding::uniformBuffer(
                 0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
                 gpu.uniforms.get()),
             QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, gpu.texture.get(),
                                                       _mipSampler.get())});
        gpu.bindings->create();
        gpu.boundTexture = gpu.texture.get();
    }

    _gpuBytes -= tile.gpuBytes;
    const qint64 textureBytes =
        static_cast<qint64>(gpu.texture->pixelSize().width()) * gpu.texture->pixelSize().height() * 4 * 4 / 3;
    tile.gpuBytes = textureBytes + vertexBytes;
    _gpuBytes += tile.gpuBytes;

    tile.gridN = mesh.gridN;
    tile.heights = mesh.heights;
    tile.heightsKnown = true;
    tile.minHeight = mesh.minHeight;
    tile.maxHeight = mesh.maxHeight;
    tile.heightsAreFallback = result.heightsAreFallback;
    tile.remeshAt = 0;
    tile.originEasting = mesh.originEasting;
    tile.originNorthing = mesh.originNorthing;
    tile.minEasting = mesh.minEasting;
    tile.maxEasting = mesh.maxEasting;
    tile.minNorthing = mesh.minNorthing;
    tile.maxNorthing = mesh.maxNorthing;
    tile.boundsZoneVersion = result.zoneVersion;
    tile.failCount = 0;
    tile.onGpu = true;
    tile.state = TileState::Ready;
}

void Map3DRenderer::_releaseGpu(TerrainTile& tile)
{
    _tileGpu.erase(tile.id);
    _gpuBytes -= tile.gpuBytes;
    tile.gpuBytes = 0;
    tile.onGpu = false;
    if (tile.state == TileState::Ready) {
        tile.state = TileState::Unloaded;
    }
}

void Map3DRenderer::_releaseAllTerrain()
{
    _tileGpu.clear();
    _quadtree.clear();
    _gpuBytes = 0;
}

void Map3DRenderer::_evictOverBudget()
{
    if (_gpuBytes <= kGpuBudgetBytes) {
        return;
    }
    _evictionCandidates.clear();
    for (auto& [id, tile] : _quadtree.tiles()) {
        if ((tile.state == TileState::Ready) && (tile.lastVisitedFrame != _quadtree.frame())) {
            _evictionCandidates.push_back(&tile);
        }
    }
    std::sort(_evictionCandidates.begin(), _evictionCandidates.end(),
              [](const TerrainTile* a, const TerrainTile* b) { return a->lastVisitedFrame < b->lastVisitedFrame; });
    const qint64 target = kGpuBudgetBytes * 9 / 10;
    for (TerrainTile* tile : _evictionCandidates) {
        if (_gpuBytes <= target) {
            break;
        }
        _releaseGpu(*tile);
    }
}

QRhiBuffer* Map3DRenderer::_indexBuffer(int gridN, QRhiResourceUpdateBatch* rub)
{
    int slot = 0;
    while ((1 << slot) < gridN) {
        slot++;
    }
    if (static_cast<int>(_indexBuffers.size()) <= slot) {
        _indexBuffers.resize(slot + 1);
    }
    if (!_indexBuffers[slot] && rub) {
        const std::vector<quint16> indices = TileMeshBuilder::buildIndices(gridN);
        _indexBuffers[slot].reset(_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::IndexBuffer,
                                                  static_cast<quint32>(indices.size() * sizeof(quint16))));
        _indexBuffers[slot]->create();
        rub->uploadStaticBuffer(_indexBuffers[slot].get(), indices.data());
    }
    return _indexBuffers[slot].get();
}

// ---------------------------------------------------------------------------
// Mission (Mission Planner's MissionOverlay)

QVector3D Map3DRenderer::_missionPosition(const MissionPoint& point)
{
    // Home on the ground, guided at the guided height above the ground, everything else at its relative
    // altitude above the terrain below it
    bool known = false;
    const double terrain = _terrainHeight(point.lat, point.lng, known);
    double x = 0;
    double y = 0;
    _frame.toLocal(point.lat, point.lng, x, y);
    const double z = (point.kind == MissionPoint::Kind::Home) ? terrain : terrain + point.alt;
    return QVector3D(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
}

void Map3DRenderer::_buildMission()
{
    // The line runs from waypoint 1 on, as on the 2D map; home keeps its marker but isn't joined to it
    std::vector<QVector3D> points;
    for (const MissionPoint& point : std::as_const(_input.waypoints)) {
        if ((point.kind == MissionPoint::Kind::None) || (point.kind == MissionPoint::Kind::Home)) {
            continue;
        }
        points.push_back(_missionPosition(point));
    }
    if (points.size() >= 2) {
        _planLines.lines.addStrip(points, {1, 1, 0, 1}, 3.0f * static_cast<float>(_input.devicePixelRatio));
    }
}

QRhiTexture* Map3DRenderer::_labelTexture(const QString& label, QRhiResourceUpdateBatch* rub)
{
    auto it = _labelQuads.find(label);
    if (it == _labelQuads.end()) {
        auto quads = std::make_shared<TexturedQuads>();
        const QImage image = labelImage(label);
        quads->texture.reset(_rhi->newTexture(QRhiTexture::RGBA8, image.size(), 1,
                                              QRhiTexture::MipMapped | QRhiTexture::UsedWithGenerateMips));
        quads->texture->create();
        rub->uploadTexture(quads->texture.get(), image);
        rub->generateMips(quads->texture.get());
        it = _labelQuads.insert(label, quads);
    }
    return it.value()->texture.get();
}

void Map3DRenderer::_buildMarkers(QRhiResourceUpdateBatch* rub)
{
    _quadDraws.clear();
    if (!_quadUniforms) {
        _quadUniforms.reset(_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kQuadUniformSize));
        _quadUniforms->create();
    }
    const auto ensureTexture = [this, rub](TexturedQuads& quads, const QString& resource) {
        if (quads.texture) {
            return;
        }
        QImage image(resource);
        image = image.convertToFormat(QImage::Format_RGBA8888);
        quads.texture.reset(_rhi->newTexture(QRhiTexture::RGBA8, image.size(), 1,
                                             QRhiTexture::MipMapped | QRhiTexture::UsedWithGenerateMips));
        quads.texture->create();
        rub->uploadTexture(quads.texture.get(), image);
        rub->generateMips(quads.texture.get());
    };
    ensureTexture(_markerQuads, QStringLiteral(":/qml/QGroundControl/TitanMap3D/images/wp_3d.png"));
    ensureTexture(_markerAltQuads, QStringLiteral(":/qml/QGroundControl/TitanMap3D/images/wp_3d_alt.png"));

    struct Marker
    {
        MissionPoint point;
        QString label;
        bool special = false;
        double distance = 0;
    };

    // Numbered as Mission Planner numbers them: by position among the items that have one, home first
    QList<Marker> markers;
    int number = 0;
    for (const MissionPoint& point : std::as_const(_input.waypoints)) {
        if (point.kind == MissionPoint::Kind::None) {
            continue;
        }
        Marker marker;
        marker.point = point;
        if (point.kind == MissionPoint::Kind::Home) {
            marker.label = QStringLiteral("H");
        } else if (point.kind == MissionPoint::Kind::Roi) {
            marker.label = QStringLiteral("R");
        } else {
            marker.label = QString::number(number);
        }
        number++;
        markers.append(marker);
    }
    if (_input.hasGuidedTarget) {
        Marker marker;
        marker.point = _input.guidedTarget;
        marker.label = QStringLiteral("G");
        markers.append(marker);
    }

    const double vehicleLat = _input.connected ? _input.fix.lat : _viewLat;
    const double vehicleLng = _input.connected ? _input.fix.lng : _viewLng;
    for (Marker& marker : markers) {
        marker.special = !marker.label.isEmpty() && !marker.label.at(0).isDigit();
        marker.distance = distanceMeters(marker.point.lat, marker.point.lng, vehicleLat, vehicleLng);
    }
    // Far to near so nearer markers blend over farther ones
    std::stable_sort(markers.begin(), markers.end(),
                     [](const Marker& a, const Marker& b) { return a.distance > b.distance; });

    const double spin =
        qDegreesToRadians(std::fmod(QTime::currentTime().msecsSinceStartOfDay() / 1000.0 * 30.0, 360.0));
    const double cosSpin = std::cos(spin);
    const double sinSpin = std::sin(spin);
    const double markerHalfSize = _input.waypointMarkerSize;

    std::vector<float> vertices;

    struct PendingQuad
    {
        TexturedQuads* quads;
        int firstVertex;
    };

    QList<PendingQuad> normal;
    QList<PendingQuad> home;
    QHash<QString, int> labelUse;

    const auto addQuad = [&vertices](const QVector3D& topLeft, const QVector3D& topRight, const QVector3D& bottomLeft,
                                     const QVector3D& bottomRight) {
        const int first = static_cast<int>(vertices.size() / 5);
        const auto corner = [&vertices](const QVector3D& p, float u, float v) {
            vertices.insert(vertices.end(), {p.x(), p.y(), p.z(), u, v});
        };
        corner(topLeft, 0, 0);
        corner(bottomLeft, 0, 1);
        corner(topRight, 1, 0);
        corner(topRight, 1, 0);
        corner(bottomLeft, 0, 1);
        corner(bottomRight, 1, 1);
        return first;
    };

    for (const Marker& marker : std::as_const(markers)) {
        if ((marker.point.lat == 0) && (marker.point.lng == 0)) {
            continue;
        }
        if (distanceMeters(marker.point.lat, marker.point.lng, _viewLat, _viewLng) < kMinMarkerDistance) {
            continue;
        }
        const QVector3D position = _missionPosition(marker.point);
        const double x = position.x();
        const double y = position.y();
        const double z = position.z();
        const bool isHome = marker.point.kind == MissionPoint::Kind::Home;
        QList<PendingQuad>& batch = isHome ? home : normal;

        // Face the camera: the billboard's horizontal axis is perpendicular to the view direction
        const double toCamera = std::atan2(_camera.x - x, _camera.y - y);
        const double axisX = std::sin(toCamera + M_PI / 2);
        const double axisY = std::cos(toCamera + M_PI / 2);
        const auto spun = [&](double h, double v) {
            const double rh = h * cosSpin - v * sinSpin;
            const double rv = h * sinSpin + v * cosSpin;
            return QVector3D(x + axisX * rh * markerHalfSize, y + axisY * rh * markerHalfSize, z + rv * markerHalfSize);
        };
        TexturedQuads* const markerQuads = marker.special ? &_markerAltQuads : &_markerQuads;
        batch.append({markerQuads, addQuad(spun(-1, 1), spun(1, 1), spun(-1, -1), spun(1, -1))});
        _output.markersDrawn++;

        // Letters sit centred in the marker; numbers sit above it. Mirrored so text reads correctly.
        const double labelHalfSize = marker.special ? markerHalfSize * 0.6 : markerHalfSize * 0.4;
        const double labelOffset = marker.special ? -(markerHalfSize / 20) : markerHalfSize * 0.5;
        const auto upright = [&](double h, double v) {
            return QVector3D(x - axisX * h * labelHalfSize, y - axisY * h * labelHalfSize,
                             z + v * labelHalfSize + labelOffset);
        };
        _labelTexture(marker.label, rub);
        TexturedQuads* const labelQuads = _labelQuads.value(marker.label).get();
        batch.append({labelQuads, addQuad(upright(-1, 1), upright(1, 1), upright(-1, -1), upright(1, -1))});
    }
    if (vertices.empty()) {
        return;
    }

    const quint32 bytes = static_cast<quint32>(vertices.size() * sizeof(float));
    if (!_quadVertices || (_quadVertices->size() < bytes)) {
        _quadVertices.reset(
            _rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, std::max<quint32>(bytes, 64 * 1024)));
        _quadVertices->create();
    }
    rub->updateDynamicBuffer(_quadVertices.get(), 0, bytes, vertices.data());
    float uniforms[16];
    writeMatrix(uniforms, _rhi->clipSpaceCorrMatrix() * _overlayProjection * _view);
    rub->updateDynamicBuffer(_quadUniforms.get(), 0, sizeof(uniforms), uniforms);

    // Every quad is its own draw so the far-to-near order holds across textures; the quads of one texture
    // share bindings
    const auto bindingsFor = [this](TexturedQuads* quads) {
        if (!quads->bindings) {
            quads->bindings.reset(_rhi->newShaderResourceBindings());
            quads->bindings->setBindings(
                {QRhiShaderResourceBinding::uniformBuffer(
                     0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
                     _quadUniforms.get()),
                 QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                           quads->texture.get(), _mipSampler.get())});
            quads->bindings->create();
        }
    };
    for (const PendingQuad& quad : std::as_const(normal)) {
        bindingsFor(quad.quads);
        _quadDraws.append({quad.quads, quad.firstVertex, false});
    }
    for (const PendingQuad& quad : std::as_const(home)) {
        bindingsFor(quad.quads);
        _quadDraws.append({quad.quads, quad.firstVertex, true});
    }
}

// ---------------------------------------------------------------------------
// Vehicle (Mission Planner's VehicleOverlay)

void Map3DRenderer::_buildVehicleOverlays()
{
    const float dpr = static_cast<float>(_input.devicePixelRatio);
    if (!_input.fpvMode) {
        const double pitch = qDegreesToRadians(static_cast<double>(_vehicle.pitch));
        if (_input.showHeadingLine) {
            Overlays::addDirection(_indicatorLines.lines, _vehicle, _vehicle.yaw, pitch, {1, 0, 0, 1}, 1.5f * dpr);
        }
        if (_input.showNavBearingLine) {
            // To the navigation target when there is one, else along the navigation bearing
            const MissionPoint* target = nullptr;
            if (_input.navMode == NavMode::Guided && _input.hasGuidedTarget) {
                target = &_input.guidedTarget;
            } else if (_input.navMode == NavMode::Return) {
                for (const MissionPoint& point : std::as_const(_input.waypoints)) {
                    if (point.kind == MissionPoint::Kind::Home) {
                        target = &point;
                        break;
                    }
                }
            } else if ((_input.navMode == NavMode::Auto) && (_input.currentWaypoint > 0) &&
                       (_input.currentWaypoint < _input.waypoints.size()) &&
                       (_input.waypoints[_input.currentWaypoint].kind != MissionPoint::Kind::None)) {
                target = &_input.waypoints[_input.currentWaypoint];
            }
            if (target && ((target->lat != 0) || (target->lng != 0))) {
                _indicatorLines.lines.addSegment(QVector3D(_vehicle.x, _vehicle.y, _vehicle.z),
                                                 _missionPosition(*target), {1, 0.5f, 0, 1}, 1.5f * dpr);
            } else {
                Overlays::addDirection(_indicatorLines.lines, _vehicle, _vehicle.navBearing, pitch, {1, 0.5f, 0, 1},
                                       1.5f * dpr);
            }
        }
        if (_input.showGpsHeadingLine) {
            Overlays::addDirection(_indicatorLines.lines, _vehicle, _vehicle.gpsHeading, 0, {0, 0, 0, 1}, 1.5f * dpr);
        }
        if (_input.showTurnRadius && (std::abs(_input.turnRadius) > 1)) {
            Overlays::addTurnArc(_indicatorLines.lines, _vehicle, _input.turnRadius, pitch, 2.0f * dpr);
        }
    }
    if (_input.showTrail) {
        _trail.draw(_trailLines.lines, _frame, _vehicle, _input.armed, _input.smoothMovement, _input.fix.velocityNorth,
                    _input.fix.velocityEast, _input.fix.velocityDown, 5.0f * dpr);
    }
}

void Map3DRenderer::_ensureModel(QRhiResourceUpdateBatch* rub)
{
    if ((_input.modelPath != _modelPath) && (_input.modelPath != _pendingModelPath)) {
        if (_modelFuture.isRunning()) {
            _modelFuture.waitForFinished();
        }
        _pendingModelPath = _input.modelPath;
        // Parsed off the render thread: a large custom ASCII STL can take a second
        _modelFuture = QtConcurrent::run([path = _pendingModelPath] {
            QString error;
            std::vector<float> vertices = Model::loadStl(path, &error);
            if (vertices.empty()) {
                qCWarning(Map3DRendererLog) << "could not load vehicle model" << path << error;
            }
            return vertices;
        });
    }
    if (_pendingModelPath.isEmpty() || !_modelFuture.isFinished()) {
        return;
    }

    const std::vector<float> vertices = _modelFuture.result();
    _modelPath = _pendingModelPath;
    _pendingModelPath.clear();
    _modelVertexCount = static_cast<int>(vertices.size() / 6);
    if (_modelVertexCount == 0) {
        return;
    }
    _modelVertices.reset(_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer,
                                         static_cast<quint32>(vertices.size() * sizeof(float))));
    _modelVertices->create();
    rub->uploadStaticBuffer(_modelVertices.get(), vertices.data());
    if (!_modelUniforms) {
        _modelUniforms.reset(_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kModelUniformSize));
        _modelUniforms->create();
        _modelBindings.reset(_rhi->newShaderResourceBindings());
        _modelBindings->setBindings({QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
            _modelUniforms.get())});
        _modelBindings->create();
    }
}

// ---------------------------------------------------------------------------
// ADS-B (Mission Planner's AdsbOverlay)

void Map3DRenderer::_buildAdsb(const QMatrix4x4& viewProjection, const QSize& size)
{
    struct Plane
    {
        int index;
        double lat;
        double lng;
        double alt;
        double distance;
    };

    QList<Plane> planes;
    const double now = nowSeconds();
    const double ownLat = _input.connected ? _input.fix.lat : _viewLat;
    const double ownLng = _input.connected ? _input.fix.lng : _viewLng;
    for (int i = 0; i < _input.adsb.size(); i++) {
        const AdsbAircraft& aircraft = _input.adsb[i];
        const double age = now - aircraft.timeSeconds;
        if ((age > kAdsbMaxAgeSeconds) || ((aircraft.lat == 0) && (aircraft.lng == 0))) {
            continue;
        }
        Plane plane{i, aircraft.lat, aircraft.lng, aircraft.alt, 0};
        // Carried forward between feed updates so traffic moves at the frame rate
        if (_input.smoothMovement && (aircraft.speed > 0)) {
            newPosition(aircraft.lat, aircraft.lng, aircraft.heading, aircraft.speed * std::max(0.0, age), plane.lat,
                        plane.lng);
        }
        plane.distance = distanceMeters(ownLat, ownLng, plane.lat, plane.lng);
        if (plane.distance <= kAdsbMaxDistance) {
            planes.append(plane);
        }
    }
    if (planes.isEmpty()) {
        return;
    }

    // Farthest first so nearer circles draw over farther ones
    std::sort(planes.begin(), planes.end(), [](const Plane& a, const Plane& b) { return a.distance > b.distance; });
    const double tanHalfFov = std::tan(qDegreesToRadians(_input.fieldOfView) / 2);
    const float width = 3.0f * static_cast<float>(_input.devicePixelRatio);
    for (const Plane& plane : std::as_const(planes)) {
        const double radius = _input.adsbCircleSize / 2.0;
        double x = 0;
        double y = 0;
        _frame.toLocal(plane.lat, plane.lng, x, y);
        const double z = plane.alt;
        const double dx = x - _camera.x;
        const double dy = y - _camera.y;
        const double dz = z - _camera.z;
        const double distanceToCamera = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (distanceToCamera < 1.0) {
            continue;
        }
        const QVector4D clip = viewProjection.map(QVector4D(x, y, z, 1));
        if (clip.w() > 0) {
            const float screenRadius =
                static_cast<float>(radius / distanceToCamera * (size.height() / 2.0) / tanHalfFov);
            AdsbScreenPosition screen;
            screen.x = (clip.x() / clip.w() + 1) * 0.5f * size.width();
            const float ndcY = _rhi->isYUpInNDC() ? clip.y() / clip.w() : -clip.y() / clip.w();
            screen.y = (1 - ndcY) * 0.5f * size.height();
            screen.radius = std::max(screenRadius, 20.0f * static_cast<float>(_input.devicePixelRatio));
            screen.icao = _input.adsb[plane.index].icao;
            screen.distance = plane.distance;
            _output.adsb.append(screen);
        }
        Overlays::addBillboardCircle(_adsbLines.lines, QVector3D(x, y, z), radius, dx / distanceToCamera,
                                     dy / distanceToCamera, dz / distanceToCamera, Overlays::adsbColor(plane.distance),
                                     width);
    }
}

// ---------------------------------------------------------------------------
// Clouds (Mission Planner's CloudRenderer)

void Map3DRenderer::_ensureClouds(QRhiResourceUpdateBatch* rub)
{
    if (!_cloudTexture) {
        const QByteArray pixels = Clouds::texture(Clouds::kTextureSize, Clouds::kTextureSeed, _cloudDensity);
        _cloudTexture.reset(_rhi->newTexture(QRhiTexture::R8, QSize(Clouds::kTextureSize, Clouds::kTextureSize), 1,
                                             QRhiTexture::MipMapped | QRhiTexture::UsedWithGenerateMips));
        _cloudTexture->create();
        rub->uploadTexture(_cloudTexture.get(), QRhiTextureUploadDescription(QRhiTextureUploadEntry(
                                                    0, 0, QRhiTextureSubresourceUploadDescription(pixels))));
        rub->generateMips(_cloudTexture.get());
        _cloudVertices.reset(_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, 3 * 4 * 3 * sizeof(float)));
        _cloudVertices->create();
        for (int i = 0; i < Clouds::kLayerCount; i++) {
            _cloudUniforms[i].reset(_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kCloudUniformSize));
            _cloudUniforms[i]->create();
            _cloudBindings[i].reset(_rhi->newShaderResourceBindings());
            _cloudBindings[i]->setBindings(
                {QRhiShaderResourceBinding::uniformBuffer(
                     0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
                     _cloudUniforms[i].get()),
                 QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                           _cloudTexture.get(), _repeatSampler.get())});
            _cloudBindings[i]->create();
        }
    }
    if (_input.cloudDensity != _cloudCoveragesDensity) {
        _cloudCoverages = Clouds::coverages(_cloudDensity, Clouds::kTextureSize, _input.cloudDensity);
        _cloudCoveragesDensity = _input.cloudDensity;
    }
}

// ---------------------------------------------------------------------------
// Lines

void Map3DRenderer::_prepareLines(LineBatch& batch, const QMatrix4x4& mvp, const QSize& size,
                                  QRhiResourceUpdateBatch* rub)
{
    if (batch.lines.isEmpty()) {
        return;
    }
    const quint32 bytes = static_cast<quint32>(batch.lines.data().size() * sizeof(float));
    if (!batch.vertices || (batch.vertices->size() < bytes)) {
        batch.vertices.reset(
            _rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, std::max<quint32>(bytes * 2, 64 * 1024)));
        batch.vertices->create();
    }
    if (!batch.uniforms) {
        batch.uniforms.reset(_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kLineUniformSize));
        batch.uniforms->create();
        batch.bindings.reset(_rhi->newShaderResourceBindings());
        batch.bindings->setBindings({QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
            batch.uniforms.get())});
        batch.bindings->create();
    }
    rub->updateDynamicBuffer(batch.vertices.get(), 0, bytes, batch.lines.data().data());
    float uniforms[kLineUniformSize / 4];
    writeMatrix(uniforms, mvp);
    writeVec4(uniforms + 16, static_cast<float>(size.width()), static_cast<float>(size.height()), 0, 0);
    rub->updateDynamicBuffer(batch.uniforms.get(), 0, sizeof(uniforms), uniforms);
}

void Map3DRenderer::_drawLines(QRhiCommandBuffer* cb, LineBatch& batch, QRhiGraphicsPipeline* pipeline,
                               const QRhiViewport& viewport)
{
    if (batch.lines.isEmpty() || !batch.vertices) {
        return;
    }
    cb->setGraphicsPipeline(pipeline);
    cb->setViewport(viewport);
    cb->setShaderResources(batch.bindings.get());
    const QRhiCommandBuffer::VertexInput input(batch.vertices.get(), 0);
    cb->setVertexInput(0, 1, &input);
    cb->draw(batch.lines.vertexCount());
}

// ---------------------------------------------------------------------------
// Stats for nerds

void Map3DRenderer::_updateStats(double viewRadius, double frameMs, double cpuMs)
{
    if (frameMs > 0) {
        _frameMsAverage = (_frameMsAverage <= 0) ? frameMs : _frameMsAverage * 0.95 + frameMs * 0.05;
        _frameTimes.append(frameMs);
        while (_frameTimes.size() > 120) {
            _frameTimes.removeFirst();
        }
    }
    _cpuMsAverage = (_cpuMsAverage <= 0) ? cpuMs : _cpuMsAverage * 0.95 + cpuMs * 0.05;

    const double now = nowSeconds();
    if (now - _rateStart >= 1.0) {
        _loadedPerSecond = (_loadedTotal - _loadedAtRateStart) / std::max(1e-3, now - _rateStart);
        _loadedAtRateStart = _loadedTotal;
        _rateStart = now;
    }
    if (!_input.statsForNerds) {
        return;
    }

    int resident = 0;
    int failed = 0;
    int noData = 0;
    int waitingForElevation = 0;
    for (const auto& [id, tile] : _quadtree.tiles()) {
        switch (tile.state) {
            case TileState::Ready:
                resident++;
                waitingForElevation += tile.heightsAreFallback ? 1 : 0;
                break;
            case TileState::Failed:
                failed++;
                break;
            case TileState::NoData:
                noData++;
                break;
            default:
                break;
        }
    }
    int minZoom = 99;
    int maxZoom = 0;
    for (const TileId& id : _quadtree.drawList()) {
        minZoom = std::min(minZoom, id.z);
        maxZoom = std::max(maxZoom, id.z);
    }
    double fx = 0;
    double fy = 0;
    double fz = 0;
    _camera.forward(fx, fy, fz);
    float fogStart = 0;
    float fogEnd = 0;
    fogRange(viewRadius, fogStart, fogEnd);

    QVariantList graph;
    for (const double sample : std::as_const(_frameTimes)) {
        graph.append(sample);
    }
    QVariantMap stats;
    stats[QStringLiteral("fps")] = _frameMsAverage > 0 ? 1000.0 / _frameMsAverage : 0.0;
    stats[QStringLiteral("frameMs")] = _frameMsAverage;
    stats[QStringLiteral("cpuMs")] = _cpuMsAverage;
    stats[QStringLiteral("terrainMs")] = _terrainMs;
    stats[QStringLiteral("frameGraph")] = graph;
    stats[QStringLiteral("tilesDrawn")] = _quadtree.drawList().size();
    stats[QStringLiteral("tilesResident")] = resident;
    stats[QStringLiteral("nodes")] = _quadtree.count();
    stats[QStringLiteral("queued")] = _loader->pendingCount();
    stats[QStringLiteral("inFlight")] = _loader->inFlightCount();
    stats[QStringLiteral("workers")] = _loader->workerCount();
    stats[QStringLiteral("loadedTotal")] = _loadedTotal;
    stats[QStringLiteral("loadedPerSecond")] = _loadedPerSecond;
    stats[QStringLiteral("fetchMs")] = _fetchMs;
    stats[QStringLiteral("buildMs")] = _buildMs;
    stats[QStringLiteral("uploadMs")] = _uploadMs;
    stats[QStringLiteral("failed")] = failed;
    stats[QStringLiteral("noData")] = noData;
    stats[QStringLiteral("waitingForElevation")] = waitingForElevation;
    stats[QStringLiteral("lastError")] = _lastError;
    stats[QStringLiteral("gpuMB")] = _gpuBytes / (1024.0 * 1024.0);
    stats[QStringLiteral("gpuBudgetMB")] = kGpuBudgetBytes / (1024.0 * 1024.0);
    stats[QStringLiteral("minZoom")] = minZoom == 99 ? 0 : minZoom;
    stats[QStringLiteral("maxZoom")] = maxZoom;
    stats[QStringLiteral("mode")] = !_input.connected
                                        ? QStringLiteral("Free look")
                                        : (_input.fpvMode ? QStringLiteral("FPV") : QStringLiteral("Chase"));
    stats[QStringLiteral("connected")] = _input.connected;
    stats[QStringLiteral("heading")] = std::fmod(qRadiansToDegrees(std::atan2(fx, fy)) + 360.0, 360.0);
    stats[QStringLiteral("pitch")] = qRadiansToDegrees(std::asin(std::clamp(fz, -1.0, 1.0)));
    stats[QStringLiteral("viewRadius")] = viewRadius;
    stats[QStringLiteral("fogStart")] = fogStart;
    stats[QStringLiteral("fogEnd")] = fogEnd;
    stats[QStringLiteral("fov")] = _input.fieldOfView;
    stats[QStringLiteral("maxZoomSetting")] = _selection.maxZoom;
    stats[QStringLiteral("utmZone")] = _frame.zone();
    stats[QStringLiteral("originMoves")] = std::max(0, _frame.version() - 1);
    stats[QStringLiteral("backend")] = QString::fromLatin1(_rhi->backendName());
    stats[QStringLiteral("gpu")] = QString::fromUtf8(_rhi->driverInfo().deviceName);
    stats[QStringLiteral("imagery")] = _input.imageryMapType;
    _output.stats = stats;
}
