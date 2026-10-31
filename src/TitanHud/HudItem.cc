#include "HudItem.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include <QtConcurrent/QtConcurrentRun>
#include <QtCore/QtMath>
#include <QtGui/QFontMetricsF>
#include <QtGui/QLinearGradient>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtGui/QPen>
#include <QtGui/QPolygonF>
#include <QtQuick/QQuickWindow>
#include <QtQuick/QSGSimpleTextureNode>

#include "BatteryFactGroupListModel.h"
#include "Fact.h"
#include "FactGroup.h"
#include "FactMetaData.h"
#include "QmlObjectListModel.h"
#include "VehicleGPSFactGroup.h"
#include "VehicleLinkManager.h"

namespace {
const QColor kOutline(0x26, 0x27, 0x28);
const QColor kRed(0xFF, 0x00, 0x00);
const QColor kGreen(0x00, 0x80, 0x00);
const QColor kYellow(0xFF, 0xFF, 0x00);
const QColor kOrange(0xFF, 0xA5, 0x00);
const QColor kBlue(0x00, 0x00, 0xFF);
const QColor kWhite(0xFF, 0xFF, 0xFF);
const QColor kBadgeGrey(0x80, 0x80, 0x80);
const QColor kBadgePurple(0x8B, 0x3F, 0xB2);
const QColor kShade(0, 0, 0, 40);
const QColor kAltGround(222, 184, 135, 100);
constexpr double kCriticalAoa = 25.0;
/// Mission Planner's 2 px text outline pen
constexpr qreal kTextOutlineWidth = 2.0;
/// Plenty for every size and colour in use; dropped and rebuilt if the HUD ever churns through more
constexpr qsizetype kMaxCachedGlyphs = 4096;
/// At most about 60 frames a second
constexpr int kMinPaintIntervalMs = 16;

double wrap180(double degrees)
{
    double wrapped = std::fmod(degrees + 180.0, 360.0);
    if (wrapped < 0.0) {
        wrapped += 360.0;
    }
    return wrapped - 180.0;
}

double factValue(Fact* fact)
{
    return fact ? fact->rawValue().toDouble() : qQNaN();
}

double groupValue(FactGroup* group, const char* name)
{
    if (!group) {
        return qQNaN();
    }
    Fact* const fact = group->getFact(QString::fromLatin1(name));
    return factValue(fact);
}
}  // namespace

HudItem::HudItem(QQuickItem* parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
    _painter.setMaxThreadCount(1);
    _paintInterval.setSingleShot(true);
    _paintInterval.setInterval(kMinPaintIntervalMs);
    (void) connect(&_paintInterval, &QTimer::timeout, this, [this]() {
        if (_paintPending) {
            _requestPaint();
        }
    });
    (void) connect(this, &HudItem::optionsChanged, this, &HudItem::_requestPaint);
}

HudItem::~HudItem()
{
    // The painting thread uses this item's glyph cache
    _painter.clear();
    _painter.waitForDone();
}

void HudItem::setHudData(HudData* data)
{
    if (_data == data) {
        return;
    }
    if (_data) {
        (void) disconnect(_data, nullptr, this, nullptr);
    }
    _data = data;
    if (_data) {
        (void) connect(_data, &HudData::dataChanged, this, &HudItem::_requestPaint);
        (void) connect(_data, &HudData::vehicleChanged, this,
                       [this]() { _connectVehicle(_data ? _data->vehicle() : nullptr); });
    }
    _connectVehicle(_data ? _data->vehicle() : nullptr);
    emit hudDataChanged();
    _requestPaint();
}

void HudItem::setDrawBackground(bool draw)
{
    if (_drawBackground == draw) {
        return;
    }
    _drawBackground = draw;
    emit drawBackgroundChanged();
    _requestPaint();
}

void HudItem::_connectVehicle(Vehicle* vehicle)
{
    if (_connectedVehicle) {
        for (Fact* fact : {_connectedVehicle->roll(), _connectedVehicle->pitch(), _connectedVehicle->heading()}) {
            (void) disconnect(fact, nullptr, this, nullptr);
        }
    }
    _connectedVehicle = vehicle;
    if (_connectedVehicle) {
        // Attitude arrives faster than the data tick; repaint on it so the horizon never lags
        for (Fact* fact : {_connectedVehicle->roll(), _connectedVehicle->pitch(), _connectedVehicle->heading()}) {
            (void) connect(fact, &Fact::rawValueChanged, this, &HudItem::_requestPaint);
        }
    }
    _requestPaint();
}

HudItem::Frame HudItem::_collectFrame() const
{
    Frame f;
    f.width = width();
    f.height = height();
    f.drawBackground = _drawBackground;
    f.showHeading = _showHeading;
    f.showSpeed = _showSpeed;
    f.showAltitude = _showAltitude;
    f.showRollPitch = _showRollPitch;
    f.showXtrack = _showXtrack;
    f.showBattery = _showBattery;
    f.showGps = _showGps;
    f.showLink = _showLink;
    f.showWaypoint = _showWaypoint;
    f.showWind = _showWind;
    f.speedUnits = FactMetaData::appSettingsSpeedUnitsString();
    f.altitudeUnits = FactMetaData::appSettingsVerticalDistanceUnitsString();
    f.distanceUnits = FactMetaData::appSettingsHorizontalDistanceUnitsString();

    Vehicle* const vehicle = _data ? _data->vehicle() : nullptr;
    if (!vehicle) {
        return f;
    }

    auto speed = [](double metersSecond) {
        return std::isnan(metersSecond) ? 0.0
                                        : FactMetaData::metersSecondToAppSettingsSpeedUnits(metersSecond).toDouble();
    };
    auto degrees = [](double value) { return std::isnan(value) ? 0.0 : value; };

    f.connected = !vehicle->vehicleLinkManager()->communicationLost();
    f.armed = vehicle->armed();
    f.fixedWing = vehicle->fixedWing() || vehicle->vtol();
    f.roll = degrees(factValue(vehicle->roll()));
    f.pitch = degrees(factValue(vehicle->pitch()));
    f.heading = degrees(factValue(vehicle->heading()));
    const double course = groupValue(vehicle->gpsFactGroup(), "courseOverGround");
    f.groundCourse = std::isnan(course) ? f.heading : course;
    f.airspeed = speed(factValue(vehicle->airSpeed()));
    f.groundspeed = speed(factValue(vehicle->groundSpeed()));
    f.verticalSpeed = speed(factValue(vehicle->climbRate()));
    const double altitudeMeters = factValue(vehicle->altitudeRelative());
    f.altitude = std::isnan(altitudeMeters)
                     ? 0.0
                     : FactMetaData::metersToAppSettingsVerticalDistanceUnits(altitudeMeters).toDouble();
    f.airspeedAccel = speed(_data->airspeedAccel());
    f.targetHeading = _data->navBearing();
    f.targetAltitude = std::isnan(_data->targetAltitude())
                           ? qQNaN()
                           : FactMetaData::metersToAppSettingsVerticalDistanceUnits(_data->targetAltitude()).toDouble();
    f.targetSpeed = std::isnan(_data->targetAirspeed()) ? qQNaN() : speed(_data->targetAirspeed());
    f.xtrack = std::isnan(_data->xtrackError()) ? 0.0 : _data->xtrackError();
    f.turnRate = _data->turnRate();
    f.distanceToHome = degrees(factValue(vehicle->distanceToHome()));
    f.headingToHome = degrees(factValue(vehicle->headingToHome()));
    f.homeSet = vehicle->homePosition().isValid();
    f.wpDistanceMeters = _data->wpDistance();
    f.wpBearing = std::isnan(_data->targetBearing()) ? factValue(vehicle->headingToNextWP()) : _data->targetBearing();
    f.wpNumber = static_cast<int>(degrees(factValue(vehicle->missionItemIndex())));
    f.mode = vehicle->flightMode();
    f.windDirection = groupValue(vehicle->windFactGroup(), "direction");
    const double windSpeed = groupValue(vehicle->windFactGroup(), "speed");
    f.windSpeed = std::isnan(windSpeed) ? qQNaN() : speed(windSpeed);

    QmlObjectListModel* const batteries = vehicle->batteries();
    if (batteries && batteries->count() > 0) {
        if (auto* const battery = qobject_cast<BatteryFactGroup*>(batteries->get(0))) {
            f.batteryVoltage = factValue(battery->voltage());
            f.batteryCurrent = factValue(battery->current());
            f.batteryRemaining = factValue(battery->percentRemaining());
            f.batteryChargeState = battery->chargeState()->rawValue().toInt();
        }
    }
    if (batteries && batteries->count() > 1) {
        if (auto* const battery = qobject_cast<BatteryFactGroup*>(batteries->get(1))) {
            f.battery2Voltage = factValue(battery->voltage());
            f.battery2Current = factValue(battery->current());
            f.battery2Remaining = factValue(battery->percentRemaining());
        }
    }

    const double fix = groupValue(vehicle->gpsFactGroup(), "lock");
    f.gpsFix = std::isnan(fix) ? -1 : static_cast<int>(fix);
    const double fix2 = groupValue(vehicle->gps2FactGroup(), "lock");
    f.gps2Fix = std::isnan(fix2) ? -1 : static_cast<int>(fix2);

    FactGroup* const vibration = vehicle->vibrationFactGroup();
    const double vibeX = groupValue(vibration, "xAxis");
    const double vibeY = groupValue(vibration, "yAxis");
    const double vibeZ = groupValue(vibration, "zAxis");
    if (!std::isnan(vibeX) || !std::isnan(vibeY) || !std::isnan(vibeZ)) {
        f.vibeMax = std::max({degrees(vibeX), degrees(vibeY), degrees(vibeZ)});
    }
    f.ekfStatus = _data->ekfStatus();
    f.linkQuality = f.connected ? std::clamp(100.0 - vehicle->mavlinkLossPercent(), 0.0, 100.0) : 0.0;
    f.failsafe = _data->failsafe();
    f.safetyActive = _data->safetyActive();
    f.recentlyArmed = _data->recentlyArmed();
    f.prearmAvailable = vehicle->readyToFlyAvailable();
    f.prearmOk = vehicle->readyToFly();
    f.message = _data->message();
    f.messageSeverity = _data->messageSeverity();
    f.aoa = _data->angleOfAttack();
    f.ssa = _data->sideslip();
    return f;
}

void HudItem::_requestPaint()
{
    _paintPending = true;
    if (_painting || _paintInterval.isActive() || !window() || !isVisible() || (width() < 1.0) || (height() < 1.0)) {
        // Picked up when the running frame finishes, the interval ends, or the item is shown or resized
        return;
    }
    _paintPending = false;
    _painting = true;
    _paintInterval.start();

    const Frame f = _collectFrame();
    const qreal devicePixelRatio = window()->effectiveDevicePixelRatio();
    (void) QtConcurrent::run(&_painter, [this, f, devicePixelRatio]() {
        QImage image((QSizeF(f.width, f.height) * devicePixelRatio).toSize(), QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(devicePixelRatio);
        image.fill(Qt::transparent);
        {
            QPainter painter(&image);
            _paint(painter, f);
        }
        return image;
    }).then(this, [this](const QImage& image) {
        _image = image;
        _imageChanged = true;
        _painting = false;
        update();
        if (_paintPending) {
            _requestPaint();
        }
    });
}

void HudItem::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        _requestPaint();
    }
}

void HudItem::itemChange(ItemChange change, const ItemChangeData& value)
{
    QQuickItem::itemChange(change, value);
    if ((change == ItemSceneChange) || (change == ItemVisibleHasChanged) ||
        (change == ItemDevicePixelRatioHasChanged)) {
        _requestPaint();
    }
}

QSGNode* HudItem::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*)
{
    auto* node = static_cast<QSGSimpleTextureNode*>(oldNode);
    if (_image.isNull()) {
        delete node;
        return nullptr;
    }
    if (!node) {
        node = new QSGSimpleTextureNode();
        node->setOwnsTexture(true);
        node->setFiltering(QSGTexture::Linear);
        _imageChanged = true;
    }
    if (_imageChanged) {
        node->setTexture(window()->createTextureFromImage(_image, QQuickWindow::TextureHasAlphaChannel));
        _imageChanged = false;
    }
    // Stretched over a resize until the frame for the new size is in
    node->setRect(boundingRect());
    return node;
}

void HudItem::_paint(QPainter& p, const Frame& f) const
{
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    if (f.drawBackground) {
        _drawHorizon(p, f);
    }
    if (f.showRollPitch) {
        _drawPitchLadder(p, f);
    }
    if (f.fixedWing) {
        _drawFlightPathVector(p, f);
    }
    if (f.showHeading) {
        _drawHeadingTape(p, f);
    }
    if (f.showXtrack) {
        _drawXtrackAndTurnRate(p, f);
    }
    if (f.showSpeed) {
        _drawSpeedTape(p, f);
    }
    if (f.showAltitude) {
        _drawAltitudeTape(p, f);
    }
    if (f.showLink) {
        _drawLinkQuality(p, f);
    }
    if (f.fixedWing && !std::isnan(f.aoa)) {
        _drawAoaGauge(p, f);
    }
    if (f.showBattery) {
        _drawBattery(p, f);
    }
    if (f.showGps) {
        _drawBadges(p, f);
    }
    _drawCenterMessages(p, f);
    _drawFlightMode(p, f);
}

// ---------------------------------------------------------------------------
// Text

qreal HudItem::_textWidth(qreal size, const QString& text) const
{
    QFont font(_fontFamily);
    font.setPixelSize(qMax(1, qRound(size)));
    const QFontMetricsF metrics(font);
    // Character by character, as _drawText places them
    qreal width = 0.0;
    for (const QChar character : text) {
        width += metrics.horizontalAdvance(character);
    }
    return width;
}

const HudItem::Glyph& HudItem::_glyph(QChar character, int pixelSize, QRgb color, qreal devicePixelRatio) const
{
    if ((devicePixelRatio != _glyphsDevicePixelRatio) || (_glyphs.size() > kMaxCachedGlyphs)) {
        _glyphs.clear();
        _glyphsDevicePixelRatio = devicePixelRatio;
    }
    const quint64 key =
        (static_cast<quint64>(character.unicode()) << 48) | (static_cast<quint64>(pixelSize & 0xFFFF) << 32) | color;
    const auto cached = _glyphs.constFind(key);
    if (cached != _glyphs.cend()) {
        return *cached;
    }

    QFont font(_fontFamily);
    font.setPixelSize(pixelSize);
    const QFontMetricsF metrics(font);
    Glyph glyph;
    glyph.advance = metrics.horizontalAdvance(character);

    QPainterPath path;
    path.addText(0.0, metrics.ascent(), font, QString(character));
    if (!path.isEmpty()) {
        const QRectF bounds =
            path.boundingRect().adjusted(-kTextOutlineWidth, -kTextOutlineWidth, kTextOutlineWidth, kTextOutlineWidth);
        const QRect pixels =
            QRectF(bounds.topLeft() * devicePixelRatio, bounds.size() * devicePixelRatio).toAlignedRect();
        glyph.image = QImage(pixels.size(), QImage::Format_ARGB32_Premultiplied);
        glyph.image.fill(Qt::transparent);
        QPainter painter(&glyph.image);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.translate(-pixels.topLeft());
        painter.scale(devicePixelRatio, devicePixelRatio);
        // The outline first and the fill over its inner half, as Mission Planner draws text, so the glyph stays
        // its own colour with a dark border
        painter.strokePath(path, QPen(kOutline, kTextOutlineWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.fillPath(path, QColor::fromRgba(color));
        painter.end();
        glyph.image.setDevicePixelRatio(devicePixelRatio);
        glyph.origin = QPointF(pixels.left() / devicePixelRatio, pixels.top() / devicePixelRatio);
    }
    return *_glyphs.insert(key, glyph);
}

void HudItem::_drawText(QPainter& p, qreal x, qreal y, qreal size, const QString& text, const QColor& color,
                        bool centered) const
{
    if (text.isEmpty() || size <= 1.0) {
        return;
    }
    const int pixelSize = qMax(1, qRound(size));
    const qreal devicePixelRatio = p.device() ? p.device()->devicePixelRatio() : 1.0;
    const QTransform device = p.deviceTransform();
    const QTransform toLogical = device.inverted();
    // y is the top of the em box, as in Mission Planner
    qreal pen = centered ? x - _textWidth(size, text) / 2.0 : x;
    for (const QChar character : text) {
        const Glyph& glyph = _glyph(character, pixelSize, color.rgba(), devicePixelRatio);
        if (!glyph.image.isNull()) {
            QPointF topLeft = QPointF(pen, y) + glyph.origin;
            if (device.type() <= QTransform::TxScale) {
                // On whole device pixels, so the glyph is copied rather than resampled
                const QPointF pixel = device.map(topLeft);
                topLeft = toLogical.map(QPointF(std::round(pixel.x()), std::round(pixel.y())));
            }
            p.drawImage(topLeft, glyph.image);
        }
        pen += glyph.advance;
    }
}

// ---------------------------------------------------------------------------
// Horizon and attitude

void HudItem::_drawHorizon(QPainter& p, const Frame& f) const
{
    const qreal W = f.width;
    const qreal H = f.height;
    const qreal pxPerDeg = std::floor(H / 65.0);
    const qreal pitchOffset = f.pitch * pxPerDeg;
    const QColor lineColor = f.connected ? _hudColor : QColor(0xD3, 0xD3, 0xD3);

    p.save();
    p.translate(W / 2.0, H / 2.0);
    p.rotate(-f.roll);

    QLinearGradient sky(0, -H, 0, pitchOffset);
    sky.setColorAt(0.0, _skyTop);
    sky.setColorAt(1.0, _skyBottom);
    p.setPen(Qt::NoPen);
    p.setBrush(sky);
    p.drawRect(QRectF(-W, -H, 2 * W, H + pitchOffset));

    QLinearGradient ground(0, pitchOffset, 0, H);
    ground.setColorAt(0.0, _groundTop);
    ground.setColorAt(1.0, _groundBottom);
    p.setBrush(ground);
    p.drawRect(QRectF(-W, pitchOffset, 2 * W, H - pitchOffset));

    p.setPen(QPen(lineColor, 2));
    p.drawLine(QPointF(-W, pitchOffset), QPointF(W, pitchOffset));
    p.restore();
}

void HudItem::_drawPitchLadder(QPainter& p, const Frame& f) const
{
    const qreal W = f.width;
    const qreal H = f.height;
    const qreal fs = std::floor(H / 28.0);
    const qreal pxPerDeg = std::floor(H / 65.0);
    const QColor whiteColor = f.connected ? _hudColor : QColor(0xD3, 0xD3, 0xD3);
    const QPen whitePen(whiteColor, 2);

    p.save();
    p.translate(W / 2.0, H / 2.0);

    // Ladder rotates with the roll
    p.save();
    p.rotate(-f.roll);
    for (int a = -90; a <= 90; a += 5) {
        if (a < f.pitch - 29 || a > f.pitch + 20) {
            continue;
        }
        const qreal y = (f.pitch - a) * pxPerDeg;
        if (a % 10 == 0) {
            p.setPen(a == 0 ? QPen(kGreen, 2) : whitePen);
            p.drawLine(QPointF(-W / 10.0, y), QPointF(W / 10.0, y));
            _drawText(p, -W / 10.0 - fs, y - fs / 1.5, fs + 2, QString::number(a), whiteColor, true);
        } else {
            p.setPen(whitePen);
            p.drawLine(QPointF(-W / 14.0, y), QPointF(W / 14.0, y));
        }
    }
    p.restore();

    // Fixed roll pointer, red and thicker past 45 degrees of bank
    const qreal ll = H / 66.0;
    const qreal extra = std::floor(H / 15.0 * 4.9);
    const qreal lx = ll + 2;
    p.setPen(QPen(kRed, std::abs(f.roll) > 45 ? 4 : 2));
    p.setBrush(Qt::NoBrush);
    p.drawPolygon(QPolygonF({QPointF(0, -2 * lx - extra), QPointF(-lx, -lx - extra), QPointF(lx, -lx - extra)}));

    // Roll scale ticks and arc, counter-rotated so they stay earth-referenced
    for (int a : {-60, -45, -30, -15, 0, 15, 30, 45, 60}) {
        p.save();
        p.rotate(a - f.roll);
        p.setPen(whitePen);
        p.drawLine(QPointF(0, -(3 * ll + extra)), QPointF(0, -(4 * ll + extra)));
        const QString label = QString::number(std::abs(a));
        _drawText(p, -fs * label.length() / 4.0, -(4 * ll + extra) - fs - 4, fs, label, whiteColor);
        p.restore();
    }
    const qreal arcRadius = extra + 3 * ll;
    p.setPen(whitePen);
    p.drawArc(QRectF(-arcRadius, -arcRadius, 2 * arcRadius, 2 * arcRadius), qRound(-(210 - f.roll) * 16), -120 * 16);

    // Aircraft reference symbol
    p.setPen(QPen(QColor(255, 0, 0, 200), 4));
    p.drawLine(QPointF(-W / 4.0 - W / 10.0, 0), QPointF(-W / 4.0, 0));
    p.drawLine(QPointF(W / 4.0, 0), QPointF(W / 4.0 + W / 10.0, 0));
    p.drawLine(QPointF(-1, 0), QPointF(W / 12.0, H / 20.0));
    p.drawLine(QPointF(1, 0), QPointF(-W / 12.0, H / 20.0));
    p.restore();
}

void HudItem::_drawFlightPathVector(QPainter& p, const Frame& f) const
{
    const qreal W = f.width;
    const qreal H = f.height;
    const qreal pxPerDeg = std::floor(H / 65.0);
    const qreal aoa = std::isnan(f.aoa) ? 0.0 : f.aoa;
    const qreal ssa = std::isnan(f.ssa) ? 0.0 : f.ssa;

    p.save();
    p.translate(W / 2.0 + ssa * pxPerDeg, H / 2.0 + aoa * pxPerDeg);
    p.setPen(QPen(kRed, 2));
    p.setBrush(Qt::NoBrush);
    const qreal r = W / 80.0;
    p.drawEllipse(QPointF(0, 0), r, r);
    p.drawLine(QPointF(-W / 40.0, 0), QPointF(-W / 80.0, 0));
    p.drawLine(QPointF(W / 80.0, 0), QPointF(W / 40.0, 0));
    p.drawLine(QPointF(0, -W / 40.0), QPointF(0, -W / 80.0));
    p.restore();
}

// ---------------------------------------------------------------------------
// Heading

void HudItem::_drawHeadingTape(QPainter& p, const Frame& f) const
{
    const qreal W = f.width;
    const qreal H = f.height;
    const qreal fs = std::floor(H / 28.0);
    const qreal barH = std::floor(1.75 * fs);
    const QColor whiteColor = f.connected ? _hudColor : QColor(0xD3, 0xD3, 0xD3);
    const QPen whitePen(whiteColor, 2);

    p.setPen(Qt::NoPen);
    p.setBrush(kShade);
    p.drawRect(QRectF(0, 0, W, barH));
    p.setPen(QPen(Qt::black, 2));
    p.drawLine(QPointF(0, barH), QPointF(W, barH));

    const qreal space = W / 120.0;
    const int start = static_cast<int>(std::floor(f.heading - 60));
    const int courseDeg = static_cast<int>(std::floor(f.groundCourse));
    static const QStringList cardinals = {QStringLiteral("N"),  QStringLiteral("NE"), QStringLiteral("E"),
                                          QStringLiteral("SE"), QStringLiteral("S"),  QStringLiteral("SW"),
                                          QStringLiteral("W"),  QStringLiteral("NW")};
    for (int a = start; a <= start + 120; a++) {
        const qreal x = space * (a - start);
        const int wrapped = ((a % 360) + 360) % 360;
        if (wrapped == courseDeg) {
            p.setPen(QPen(Qt::black, 6));
            p.drawLine(QPointF(x, 0), QPointF(x, barH));
        }
        if (a % 15 == 0) {
            p.setPen(whitePen);
            p.drawLine(QPointF(x, barH - 5), QPointF(x, barH - 10));
            if (wrapped % 45 == 0) {
                _drawText(p, x, fs / 5.0, fs, cardinals.at(wrapped / 45), whiteColor, true);
            } else {
                _drawText(p, x, fs / 5.0, fs - 2, QString::number(wrapped), whiteColor, true);
            }
        } else if (a % 5 == 0) {
            p.setPen(whitePen);
            p.drawLine(QPointF(x, barH - 5), QPointF(x, barH - 10));
        }
    }

    // Center box
    p.setPen(Qt::NoPen);
    p.setBrush(kShade);
    p.drawRect(QRectF(W / 2.0 - 1.5 * fs, 0, 3 * fs, barH));

    // Target heading from the navigation controller, pinned to the edges when off the tape
    if (!std::isnan(f.targetHeading)) {
        const double diff = wrap180(f.targetHeading - f.heading);
        qreal x = 5 + space * (diff + 60) - 3;
        if (diff < -60) {
            x = 3;
        } else if (diff > 60) {
            x = W - 3;
        }
        p.setPen(QPen(_targetHeadingColor, 6));
        p.drawLine(QPointF(x, 0), QPointF(x, barH));
    }

    // Home marker
    if (f.distanceToHome > 0) {
        const double diff = wrap180(f.headingToHome - f.heading);
        if (std::abs(diff) <= 60) {
            _drawText(p, 5 + space * (diff + 60), fs / 5.0, fs, QStringLiteral("H"), kGreen, true);
        }
    }

    const int headingInt = ((static_cast<int>(f.heading) % 360) + 360) % 360;
    _drawText(p, W / 2.0, fs / 5.0, fs, QStringLiteral("%1").arg(headingInt, 3), whiteColor, true);
}

// ---------------------------------------------------------------------------
// Cross-track and turn rate

void HudItem::_drawXtrackAndTurnRate(QPainter& p, const Frame& f) const
{
    const qreal W = f.width;
    const qreal H = f.height;
    const qreal fs = std::floor(H / 28.0);
    const qreal barBottom = std::floor(1.75 * fs);
    const QColor whiteColor = f.connected ? _hudColor : QColor(0xD3, 0xD3, 0xD3);
    const qreal x0 = W / 10.0;
    const qreal xs = W / 30.0;
    const qreal pad = 10;
    const qreal top = barBottom + 5;
    const qreal bot = barBottom + H / 10.0;

    p.save();
    p.translate(0, 5);
    p.setPen(QPen(whiteColor, 2));
    p.drawLine(QPointF(x0, top), QPointF(x0, bot));
    for (int k : {-2, -1, 1, 2}) {
        p.drawLine(QPointF(x0 + k * xs, top + pad), QPointF(x0 + k * xs, bot - pad));
    }

    const double xtrack = std::clamp(f.xtrack, -40.0, 40.0);
    QColor needle = kGreen;
    if (std::abs(f.xtrack) > 40) {
        needle.setAlpha(128);
    }
    p.setPen(QPen(needle, 2));
    const qreal needleX = x0 + xtrack / 20.0 * xs;
    p.drawLine(QPointF(needleX, top), QPointF(needleX, bot));

    // Turn rate: fixed reference marks and a sliding green T
    const qreal y = bot + 10;
    p.setPen(QPen(whiteColor, 4));
    for (int k : {-2, 0, 2}) {
        p.drawLine(QPointF(x0 + k * xs - xs / 2.0, y), QPointF(x0 + k * xs + xs / 2.0, y));
    }
    const double turnRate = std::clamp(f.turnRate, -6.0, 6.0);
    QColor tee = kGreen;
    if (std::abs(f.turnRate) > 6) {
        tee.setAlpha(128);
    }
    p.setPen(QPen(tee, 4));
    const qreal teeX = x0 + turnRate / 12.0 * 4 * xs;
    p.drawLine(QPointF(teeX - xs / 2.0, y + 3), QPointF(teeX + xs / 2.0, y + 3));
    p.drawLine(QPointF(teeX, y + 3), QPointF(teeX, y + 10));
    p.restore();
}

// ---------------------------------------------------------------------------
// Tapes

void HudItem::_drawTapePointer(QPainter& p, qreal tapeWidth, qreal fs, bool rightSide, bool extended) const
{
    // Origin is at the tape's vertical center on the window edge; the point faces into the HUD
    const qreal aH = std::floor(0.625 * fs);
    const qreal notch = tapeWidth / 6.0;
    const qreal tip = tapeWidth / 10.0;
    const qreal dir = rightSide ? -1.0 : 1.0;
    const qreal bottom = extended ? aH + 1.25 * fs : aH;
    QPolygonF shape({QPointF(0, -aH), QPointF(dir * (tapeWidth - notch), -aH), QPointF(dir * (tapeWidth - tip), 0),
                     QPointF(dir * (tapeWidth - notch), bottom), QPointF(0, bottom)});
    p.setPen(extended ? Qt::NoPen : QPen(Qt::black, 2));
    p.setBrush(extended ? QColor(0, 0, 0, 150) : QColor(Qt::black));
    p.drawPolygon(shape);
}

void HudItem::_drawSpeedTape(QPainter& p, const Frame& f) const
{
    const qreal W = f.width;
    const qreal H = f.height;
    const qreal fs = std::floor(H / 28.0);
    const QColor whiteColor = f.connected ? _hudColor : QColor(0xD3, 0xD3, 0xD3);
    const QRectF tape(0, H / 4.0, W / 10.0, H / 2.0);
    const qreal sbW = tape.width();
    const qreal sbH = tape.height();
    const qreal R = tape.right();
    const qreal T = tape.top();
    const qreal B = tape.bottom();

    p.setPen(QPen(whiteColor, 2));
    p.setBrush(kShade);
    p.drawRect(tape);

    const double speed = f.airspeed > 0 ? f.airspeed : f.groundspeed;
    const qreal space = sbH / 26.0;
    const long start = static_cast<long>(std::floor(speed - 13));
    auto yFor = [&](double value) { return 3 * H / 4.0 - space * (value - start); };

    if (!std::isnan(f.targetSpeed) && f.targetSpeed != 0) {
        p.setPen(QPen(QColor(0, 128, 0, 128), 6));
        if (start > f.targetSpeed) {
            p.drawLine(QPointF(tape.left(), B), QPointF(R, B));
        } else if (speed + 13 < f.targetSpeed) {
            p.drawLine(QPointF(tape.left(), T), QPointF(R, T));
        } else {
            const qreal y = yFor(static_cast<long>(f.targetSpeed));
            p.drawLine(QPointF(tape.left(), y), QPointF(R, y));
        }
    }

    p.setPen(QPen(whiteColor, 2));
    for (long a = start; a <= start + 26; a++) {
        if (a % 5 == 0) {
            const qreal y = yFor(static_cast<double>(a));
            p.drawLine(QPointF(R, y), QPointF(R - sbW / 5.0, y));
            _drawText(p, R - sbW / 5.0 - 2.25 * fs, y - fs / 1.5, fs, QStringLiteral("%1").arg(a, 5), whiteColor);
        }
    }

    // Pointer with the airspeed and groundspeed readouts
    p.save();
    p.translate(0, H / 2.0);
    _drawTapePointer(p, sbW, fs, false, false);
    _drawTapePointer(p, sbW, fs, false, true);
    const qreal notch = sbW / 6.0;
    const qreal aH = std::floor(0.625 * fs);
    _drawText(p, notch / 3.0, -0.4 * fs - 2, 0.75 * fs,
              QStringLiteral("A:%1%2").arg(qRound(f.airspeed)).arg(f.speedUnits), kWhite);
    _drawText(p, notch / 3.0, aH + 0.125 * fs, 0.75 * fs,
              QStringLiteral("G:%1%2").arg(qRound(f.groundspeed)).arg(f.speedUnits), kWhite);
    p.restore();

    // Airspeed trend strip to the right of the tape
    p.setPen(QPen(whiteColor, 2));
    p.setBrush(Qt::NoBrush);
    p.drawPolygon(QPolygonF(
        {QPointF(R, T), QPointF(R + sbW / 4.0, T + sbW / 4.0), QPointF(R + sbW / 4.0, B - sbW / 4.0), QPointF(R, B)}));
    for (int i = 1; i <= 11; i++) {
        const qreal y = T + i * sbH / 12.0;
        p.drawLine(QPointF(R + sbW / 4.0, y), QPointF(R + sbW / 8.0, y));
    }
    if (f.armed) {
        const qreal mid = (T + B) / 2.0;
        const qreal accelBar = -std::clamp(f.airspeedAccel, -6.0, 6.0) / 12.0 * sbH;
        p.setPen(Qt::NoPen);
        p.setBrush(kBlue);
        p.drawRect(QRectF(QPointF(R, mid), QPointF(R + sbW / 4.0, mid + accelBar)).normalized());
    }

    // Waypoint and wind readouts under the tape
    const qreal readoutSize = 0.85 * fs;
    qreal y = B + fs / 2.0;
    if (f.showWaypoint) {
        QString label = QStringLiteral("WP");
        const QString mode = f.mode.toLower();
        if (mode == QLatin1String("auto") && f.wpNumber > 0) {
            label = QStringLiteral("WP %1").arg(f.wpNumber);
        } else if (mode == QLatin1String("loiter")) {
            label = QStringLiteral("↻");
        } else if (mode == QLatin1String("rtl")) {
            label = QStringLiteral("H");
        } else if (mode == QLatin1String("guided")) {
            label = QStringLiteral("G");
        }
        QString value;
        if (!std::isnan(f.wpDistanceMeters)) {
            const double distance =
                FactMetaData::metersToAppSettingsHorizontalDistanceUnits(f.wpDistanceMeters).toDouble();
            if (f.distanceUnits == QLatin1String("m") && distance >= 1000) {
                value = QStringLiteral("%1km").arg(distance / 1000.0, 0, 'f', 1);
            } else if (f.distanceUnits == QLatin1String("ft") && distance >= 5280) {
                value = QStringLiteral("%1mi").arg(distance / 5280.0, 0, 'f', 1);
            } else {
                value = QStringLiteral("%1%2").arg(qRound(distance)).arg(f.distanceUnits);
            }
        }
        const bool showArrow = !std::isnan(f.wpBearing) && !std::isnan(f.wpDistanceMeters) && f.wpDistanceMeters > 0;
        _drawArrowReadout(p, fs / 2.0, y, readoutSize, label, f.wpBearing, showArrow, value, f);
        y += 1.5 * readoutSize;
    }
    if (f.showWind && !std::isnan(f.windSpeed)) {
        const bool showArrow = !std::isnan(f.windDirection) && f.windSpeed >= 0.05;
        _drawArrowReadout(p, fs / 2.0, y, readoutSize, QStringLiteral("Wind"), f.windDirection + 180.0, showArrow,
                          QStringLiteral("%1%2").arg(f.windSpeed, 0, 'f', 1).arg(f.speedUnits), f);
    }
}

void HudItem::_drawArrowReadout(QPainter& p, qreal x, qreal y, qreal size, const QString& label, double direction,
                                bool showArrow, const QString& value, const Frame& f) const
{
    const QColor whiteColor = f.connected ? _hudColor : QColor(0xD3, 0xD3, 0xD3);
    _drawText(p, x, y, size, label, whiteColor);
    qreal cursor = x + _textWidth(size, label) + 0.35 * size;
    if (showArrow) {
        const qreal s = 0.9 * size;
        const qreal centerX = cursor + 0.45 * size;
        p.save();
        p.translate(centerX, y + 0.65 * size);
        p.rotate(direction - f.heading);
        const QPolygonF shaft({QPointF(0, s / 2.0), QPointF(0, -s / 2.0)});
        const QPolygonF leftBarb({QPointF(-0.32 * s, -0.12 * s), QPointF(0, -s / 2.0)});
        const QPolygonF rightBarb({QPointF(0.32 * s, -0.12 * s), QPointF(0, -s / 2.0)});
        const qreal lineWidth = qMax(2.0, size / 9.0);
        for (const auto& [color, widthAdjust] : {std::pair{kOutline, 2.0}, std::pair{kWhite, 0.0}}) {
            p.setPen(QPen(color, lineWidth + widthAdjust, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawPolyline(shaft);
            p.drawPolyline(leftBarb);
            p.drawPolyline(rightBarb);
        }
        p.restore();
        cursor = centerX + 0.45 * size + 0.35 * size;
    }
    _drawText(p, cursor, y, size, value, whiteColor);
}

void HudItem::_drawAltitudeTape(QPainter& p, const Frame& f) const
{
    const qreal W = f.width;
    const qreal H = f.height;
    const qreal fs = std::floor(H / 28.0);
    const QColor whiteColor = f.connected ? _hudColor : QColor(0xD3, 0xD3, 0xD3);
    const QRectF tape(W - W / 10.0, H / 4.0, W / 10.0, H / 2.0);
    const qreal sbW = tape.width();
    const qreal sbH = tape.height();
    const qreal L = tape.left();
    const qreal T = tape.top();
    const qreal B = tape.bottom();

    p.setPen(QPen(whiteColor, 2));
    p.setBrush(kShade);
    p.drawRect(tape);

    const qreal space = sbH / 26.0;
    const long start = static_cast<long>(std::floor(f.altitude)) - 13;
    auto yFor = [&](double value) { return 3 * H / 4.0 - space * (value - start); };

    // Ground band below the zero line once home is set
    if (f.homeSet && start <= 0 && start + 26 >= 0) {
        p.setPen(Qt::NoPen);
        p.setBrush(kAltGround);
        p.drawRect(QRectF(QPointF(L, yFor(0)), QPointF(tape.right(), B)));
    }

    if (!std::isnan(f.targetAltitude)) {
        p.setPen(QPen(QColor(0, 128, 0, 128), 6));
        if (start > f.targetAltitude) {
            p.drawLine(QPointF(L, B), QPointF(tape.right(), B));
        } else if (f.altitude + 13 < f.targetAltitude) {
            p.drawLine(QPointF(L, T), QPointF(tape.right(), T));
        } else {
            const qreal y = yFor(std::round(f.targetAltitude));
            p.drawLine(QPointF(L, y), QPointF(tape.right(), y));
        }
    }

    p.setPen(QPen(whiteColor, 2));
    for (long a = start; a <= start + 26; a++) {
        if (a % 5 == 0) {
            const qreal y = yFor(static_cast<double>(a));
            p.drawLine(QPointF(L, y), QPointF(L + sbW / 5.0, y));
            _drawText(p, L + 1.75 * sbW / 5.0, y - fs / 1.5, fs, QString::number(a), whiteColor);
        }
    }

    // Vertical speed strip to the left of the tape
    p.setPen(QPen(whiteColor, 2));
    p.setBrush(Qt::NoBrush);
    p.drawPolygon(QPolygonF(
        {QPointF(L, T), QPointF(L - sbW / 4.0, T + sbW / 4.0), QPointF(L - sbW / 4.0, B - sbW / 4.0), QPointF(L, B)}));
    for (int i = 1; i <= 11; i++) {
        const qreal y = T + i * sbH / 12.0;
        p.drawLine(QPointF(L - sbW / 4.0, y), QPointF(L - sbW / 8.0, y));
    }
    {
        const qreal mid = (T + B) / 2.0;
        const qreal climbBar = -std::clamp(f.verticalSpeed, -6.0, 6.0) / 12.0 * sbH;
        p.setPen(Qt::NoPen);
        p.setBrush(kBlue);
        p.drawRect(QRectF(QPointF(L - sbW / 4.0, mid), QPointF(L, mid + climbBar)).normalized());
    }

    // Pointer with the altitude readout
    p.save();
    p.translate(W, H / 2.0);
    _drawTapePointer(p, sbW, fs, true, false);
    p.restore();
    const qreal notch = sbW / 6.0;
    _drawText(p, L + 1.25 * notch, H / 2.0 - 0.4 * fs - 2, 0.75 * fs,
              QStringLiteral("%1%2").arg(static_cast<int>(f.altitude)).arg(f.altitudeUnits), kWhite);
}

// ---------------------------------------------------------------------------
// Link quality, AOA, battery, badges

void HudItem::_drawLinkQuality(QPainter& p, const Frame& f) const
{
    const qreal W = f.width;
    const qreal H = f.height;
    const qreal fs = std::floor(H / 28.0);
    const qreal L = W - W / 10.0;
    const qreal base = H / 4.0 - 0.3 * fs;
    const qreal x0 = L + 0.2 * fs;
    const double quality = std::isnan(f.linkQuality) ? 0.0 : f.linkQuality;

    p.setPen(Qt::NoPen);
    const qreal heights[3] = {0.4 * fs, 0.7 * fs, 1.0 * fs};
    const double thresholds[3] = {20, 50, 80};
    for (int i = 0; i < 3; i++) {
        p.setBrush(quality > thresholds[i] ? kGreen : kRed);
        p.drawRect(QRectF(x0 + i * 0.35 * fs, base - heights[i], 0.2 * fs, heights[i]));
    }
    _drawText(p, x0 + 1.25 * fs, base - fs, fs - 2, QStringLiteral("%1%").arg(qRound(quality)),
              f.connected ? _hudColor : QColor(0xD3, 0xD3, 0xD3));
    if (quality <= 0) {
        p.setPen(QPen(kRed, 2));
        const qreal s = 0.7 * fs;
        p.drawLine(QPointF(x0, base - fs), QPointF(x0 + s, base - fs + s));
        p.drawLine(QPointF(x0 + s, base - fs), QPointF(x0, base - fs + s));
    }
}

void HudItem::_drawAoaGauge(QPainter& p, const Frame& f) const
{
    const qreal W = f.width;
    const qreal H = f.height;
    const QRectF bar(std::floor(W * 6 / 7.0) - W / 80.0, H / 2.0 + H / 20.0 - W / 40.0, W / 50.0, H / 5.0);

    p.setPen(Qt::NoPen);
    p.setBrush(kRed);
    p.drawRect(QRectF(bar.left(), bar.top(), bar.width(), bar.height() * 0.15));
    p.setBrush(kYellow);
    p.drawRect(QRectF(bar.left(), bar.top() + bar.height() * 0.15, bar.width(), bar.height() * 0.25));
    p.setBrush(kGreen);
    p.drawRect(QRectF(bar.left(), bar.top() + bar.height() * 0.40, bar.width(), bar.height() * 0.60));
    p.setPen(QPen(kWhite, 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(bar);

    const double ratio = std::clamp(f.aoa / kCriticalAoa, 0.0, 1.0);
    const qreal tipY = bar.top() + bar.height() * (1.0 - ratio);
    const qreal s = bar.width();
    p.setPen(QPen(kWhite, 1));
    p.setBrush(Qt::black);
    p.drawPolygon(QPolygonF({QPointF(bar.left(), tipY), QPointF(bar.left() - 0.75 * s, tipY - s / 2.0),
                             QPointF(bar.left() - 0.75 * s, tipY + s / 2.0)}));
}

void HudItem::_drawBatteryIcon(QPainter& p, const QRectF& rect, int bars, const QColor& barColor) const
{
    const qreal capH = rect.height() * 0.06;
    p.setPen(QPen(kWhite, 2));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(rect.left(), rect.top() + capH, rect.width(), rect.height() - capH), 2, 2);
    p.setPen(Qt::NoPen);
    p.setBrush(kWhite);
    p.drawRect(QRectF(rect.left() + rect.width() * 0.3, rect.top(), rect.width() * 0.4, capH));
    const qreal inset = rect.width() * 0.15;
    const qreal barH = (rect.height() - capH - 2 * inset) / 4.0;
    p.setBrush(barColor);
    for (int i = 0; i < std::clamp(bars, 0, 4); i++) {
        const qreal top = rect.bottom() - inset - (i + 1) * barH + barH * 0.15;
        p.drawRect(QRectF(rect.left() + inset, top, rect.width() - 2 * inset, barH * 0.7));
    }
}

void HudItem::_drawBattery(QPainter& p, const Frame& f) const
{
    if (std::isnan(f.batteryVoltage) && std::isnan(f.batteryRemaining)) {
        return;
    }
    const qreal W = f.width;
    const qreal H = f.height;
    const qreal fs = std::floor(H / 28.0);
    const qreal bs = (fs + 2) * 3;

    QColor textColor = f.connected ? _hudColor : QColor(0xD3, 0xD3, 0xD3);
    QColor barColor = kGreen;
    if (f.batteryChargeState >= 3) {  // MAV_BATTERY_CHARGE_STATE_CRITICAL and worse
        textColor = kRed;
        barColor = kRed;
    } else if (f.batteryChargeState == 2) {  // MAV_BATTERY_CHARGE_STATE_LOW
        textColor = kOrange;
        barColor = kOrange;
    }
    auto barsFor = [](double remaining) {
        if (std::isnan(remaining)) {
            return 0;
        }
        return remaining > 75 ? 4 : remaining > 50 ? 3 : remaining > 25 ? 2 : 1;
    };

    auto drawOne = [&](qreal left, double voltage, double current, double remaining) -> qreal {
        _drawBatteryIcon(p, QRectF(left, H - bs, bs / 2.0, bs), barsFor(remaining), barColor);
        const qreal textX = left + bs / 2.0 + 6;
        const QString voltageText = QStringLiteral("%1v").arg(std::isnan(voltage) ? 0.0 : voltage, 0, 'f', 2);
        const QString currentText = QStringLiteral("%1A").arg(std::isnan(current) ? 0.0 : current, 0, 'f', 1);
        const QString remainingText = QStringLiteral("%1%").arg(std::isnan(remaining) ? 0 : qRound(remaining));
        _drawText(p, textX, H - 6 - 3 * fs, fs, voltageText, textColor);
        _drawText(p, textX, H - 6 - 2 * fs, fs, currentText, textColor);
        _drawText(p, textX, H - 6 - fs, fs, remainingText, textColor);
        return textX +
               std::max({_textWidth(fs, voltageText), _textWidth(fs, currentText), _textWidth(fs, remainingText)});
    };

    const qreal end = drawOne(3, f.batteryVoltage, f.batteryCurrent, f.batteryRemaining);
    if (!std::isnan(f.battery2Voltage) && f.battery2Voltage > 0) {
        drawOne(end + W / 64.0, f.battery2Voltage, f.battery2Current, f.battery2Remaining);
    }
}

void HudItem::_drawBadge(QPainter& p, const QRectF& rect, const QColor& fill, const QString& text) const
{
    p.setPen(Qt::NoPen);
    p.setBrush(fill);
    p.drawRoundedRect(rect, rect.height() * 0.3, rect.height() * 0.3);
    QFont font(_fontFamily);
    font.setPixelSize(qMax(1, qRound(rect.height() * 0.62)));
    font.setBold(true);
    p.setFont(font);
    p.setPen(kWhite);
    p.drawText(rect, Qt::AlignCenter, text);
}

void HudItem::_drawBadges(QPainter& p, const Frame& f) const
{
    const qreal W = f.width;
    const qreal H = f.height;
    const qreal fs = std::floor(H / 28.0);
    const qreal w = std::floor(2.5 * fs);
    const qreal y = H - fs - 5;
    auto slotRect = [&](int slot) { return QRectF(W - 3 - slot * w - (slot - 1) * 5, y, w, fs); };

    auto gpsBadge = [&](int fix, const QRectF& rect) {
        QString text;
        QColor color;
        switch (fix) {
            case 0:
                text = QStringLiteral("NO GPS");
                color = kRed;
                break;
            case 1:
                text = QStringLiteral("NO FIX");
                color = kRed;
                break;
            case 2:
                text = QStringLiteral("2D FIX");
                color = kOrange;
                break;
            case 3:
                text = QStringLiteral("3D FIX");
                color = kGreen;
                break;
            case 4:
                text = QStringLiteral("DGPS");
                color = kGreen;
                break;
            case 5:
                text = QStringLiteral("RTK Flt");
                color = kBadgePurple;
                break;
            case 6:
                text = QStringLiteral("RTK Fix");
                color = kBadgePurple;
                break;
            default:
                text = QStringLiteral("GPS ?");
                color = kBadgeGrey;
                break;
        }
        _drawBadge(p, rect, color, text);
    };

    int slot = 1;
    if (f.gps2Fix > 0) {
        gpsBadge(f.gps2Fix, slotRect(slot++));
    }
    if (f.gpsFix >= 0) {
        gpsBadge(f.gpsFix, slotRect(slot++));
    }

    if (!std::isnan(f.vibeMax)) {
        const QColor color = f.vibeMax > 60 ? kRed : f.vibeMax > 30 ? kYellow : kGreen;
        _drawBadge(p, slotRect(slot++), color, QStringLiteral("VIBE"));
    }
    if (!std::isnan(f.ekfStatus)) {
        const QColor color = f.ekfStatus > 0.8 ? kRed : f.ekfStatus > 0.5 ? kYellow : kGreen;
        _drawBadge(p, slotRect(slot++), color, QStringLiteral("EKF"));
    }

    if (!f.armed && f.prearmAvailable) {
        if (f.prearmOk) {
            _drawText(p, W / 2.0, H - 4 * fs, fs, QStringLiteral("Ready to Arm"),
                      f.connected ? _hudColor : QColor(0xD3, 0xD3, 0xD3), true);
        } else {
            _drawText(p, W / 2.0, H - 4 * fs, fs, QStringLiteral("Not Ready to Arm"), kRed, true);
        }
    }
}

// ---------------------------------------------------------------------------
// Messages and mode

void HudItem::_drawCenterMessages(QPainter& p, const Frame& f) const
{
    const qreal W = f.width;
    const qreal H = f.height;
    const qreal fs = std::floor(H / 28.0);
    const qreal cx = W / 2.0;
    const qreal cy = H / 2.0;

    if (!f.armed) {
        _drawText(p, cx, cy - H / 6.0, fs + 10, QStringLiteral("DISARMED"), kRed, true);
    } else if (f.recentlyArmed) {
        _drawText(p, cx, cy - H / 6.0, fs + 20, QStringLiteral("ARMED"), kRed, true);
    }
    if (f.safetyActive) {
        _drawText(p, cx, cy - H / 4.0, fs + 10, QStringLiteral("SAFE"), kRed, true);
    }
    if (f.failsafe) {
        _drawText(p, cx, cy - H / 10.0, fs + 20, QStringLiteral("FAILSAFE"), kRed, true);
    }
    if (!f.message.isEmpty()) {
        const QColor color = f.messageSeverity <= 3   ? kRed
                             : f.messageSeverity == 4 ? kYellow
                                                      : (f.connected ? _hudColor : QColor(0xD3, 0xD3, 0xD3));
        _drawText(p, cx, cy + H / 6.0, 1.25 * fs, f.message, color, true);
    }
}

void HudItem::_drawFlightMode(QPainter& p, const Frame& f) const
{
    const qreal W = f.width;
    const qreal H = f.height;
    const qreal fs = std::floor(H / 28.0);
    const QString mode = f.mode.isEmpty() ? QStringLiteral("Unknown") : f.mode;
    const qreal tsh = _textWidth(fs, mode) / 2.0 + 0.25 * fs;
    const qreal mh = 2 * fs;

    p.setPen(QPen(f.connected ? _hudColor : QColor(0xD3, 0xD3, 0xD3), 2));
    p.setBrush(kShade);
    p.drawPolygon(QPolygonF({QPointF(W / 2.0 - tsh - mh, H), QPointF(W / 2.0 - tsh - mh / 2.0, H - mh),
                             QPointF(W / 2.0 + tsh + mh / 2.0, H - mh), QPointF(W / 2.0 + tsh + mh, H)}));
    _drawText(p, W / 2.0, H - 1.5 * fs, fs, mode, f.connected ? _hudColor : QColor(0xD3, 0xD3, 0xD3), true);
}
