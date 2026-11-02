#include "HudPainter.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>

#include <QtCore/QList>
#include <QtCore/QtMath>
#include <QtGui/QFont>
#include <QtGui/QFontDatabase>
#include <QtGui/QFontMetricsF>
#include <QtGui/QLinearGradient>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtGui/QPen>
#include <QtGui/QRadialGradient>
#include <QtGui/QTransform>

#include "QGCLoggingCategory.h"

QGC_LOGGING_CATEGORY(HudPainterLog, "TitanHud.HudPainter")

namespace {
const QColor kSymbol(0x66, 0xFF, 0x99);
const QColor kSymbolLost(0x9A, 0xA6, 0x9F);
const QColor kCaution(0xFF, 0xC2, 0x3D);
const QColor kWarning(0xFF, 0x50, 0x46);
const QColor kHalo(0, 0, 0, 150);
const QColor kPlate(0, 0, 0, 105);
const QColor kSkyZenith(0x02, 0x09, 0x13);
const QColor kSkyHorizon(0x13, 0x38, 0x4D);
const QColor kGroundHorizon(0x26, 0x29, 0x16);
const QColor kGroundNadir(0x06, 0x07, 0x04);

const QString kFontFamily = QStringLiteral("B612 Mono");

constexpr double kHeadingHalfRange = 30.0;
constexpr double kTurnTrendSeconds = 6.0;
constexpr double kXtrackMetersPerDot = 20.0;
constexpr double kAoaCaution = 15.0;
constexpr double kAoaWarning = 21.0;
constexpr double kVibrationCaution = 30.0;
constexpr double kVibrationWarning = 60.0;
constexpr double kEkfCaution = 0.5;
constexpr double kEkfWarning = 0.8;
constexpr double kMaxReadout = 1e9;
/// Share of a unit over which a rolling digit turns to the next, centred on the half
constexpr double kRollBand = 0.2;
constexpr int kBlinkMs = 400;
constexpr qsizetype kMaxCachedGlyphs = 4096;

double wrap180(double degrees)
{
    double wrapped = std::fmod(degrees + 180.0, 360.0);
    if (wrapped < 0.0) {
        wrapped += 360.0;
    }
    return wrapped - 180.0;
}

double wrap360(double degrees)
{
    const double wrapped = std::fmod(degrees, 360.0);
    return wrapped < 0.0 ? wrapped + 360.0 : wrapped;
}

double finiteOr(double value, double fallback)
{
    return std::isfinite(value) ? value : fallback;
}

QColor withAlpha(QColor color, qreal alpha)
{
    color.setAlphaF(std::clamp(color.alphaF() * alpha, 0.0, 1.0));
    return color;
}

bool blinkOn(const HudFrame& f)
{
    return (f.clockMs / kBlinkMs) % 2 == 0;
}

/// The smallest 1-2-5 step of at least minimum
double niceStep(double minimum)
{
    const double magnitude = std::pow(10.0, std::floor(std::log10(std::max(minimum, 1e-6))));
    for (const double factor : {1.0, 2.0, 5.0}) {
        if (factor * magnitude >= minimum) {
            return factor * magnitude;
        }
    }
    return 10.0 * magnitude;
}

bool isMultiple(double value, double step)
{
    const double ratio = value / step;
    return std::abs(ratio - std::round(ratio)) < 1e-6;
}

QColor severityColor(int severity, const QColor& normal)
{
    // MAV_SEVERITY: lower is worse; ERROR and worse are warnings, WARNING a caution
    return severity <= 3 ? kWarning : severity == 4 ? kCaution : normal;
}

QString formatTime(double seconds)
{
    const qint64 total = qRound64(std::clamp(seconds, 0.0, 359999.0));
    if (total >= 3600) {
        return QStringLiteral("%1:%2:%3")
            .arg(total / 3600)
            .arg((total / 60) % 60, 2, 10, QLatin1Char('0'))
            .arg(total % 60, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2").arg(total / 60, 2, 10, QLatin1Char('0')).arg(total % 60, 2, 10, QLatin1Char('0'));
}
}  // namespace

// ---------------------------------------------------------------------------
// Fades

qreal HudPainter::Fade::alphaAt(QPointF point) const
{
    qreal distance = 0.0;
    switch (kind) {
        case Kind::None:
            return 1.0;
        case Kind::Radial:
            distance = std::hypot(point.x() - center.x(), point.y() - center.y());
            break;
        case Kind::Vertical:
            distance = std::abs(point.y() - center.y());
            break;
        case Kind::Horizontal:
            distance = std::abs(point.x() - center.x());
            break;
    }
    if (distance <= inner) {
        return 1.0;
    }
    if (distance >= outer) {
        return 0.0;
    }
    return (outer - distance) / (outer - inner);
}

QBrush HudPainter::Fade::brush(const QColor& color) const
{
    const QColor clear = withAlpha(color, 0.0);
    switch (kind) {
        case Kind::None:
            break;
        case Kind::Radial: {
            QRadialGradient gradient(center, outer);
            gradient.setColorAt(0.0, color);
            gradient.setColorAt(inner / outer, color);
            gradient.setColorAt(1.0, clear);
            return gradient;
        }
        case Kind::Vertical:
        case Kind::Horizontal: {
            const QPointF axis = (kind == Kind::Vertical) ? QPointF(0.0, outer) : QPointF(outer, 0.0);
            QLinearGradient gradient(center - axis, center + axis);
            const qreal edge = (outer - inner) / (2.0 * outer);
            gradient.setColorAt(0.0, clear);
            gradient.setColorAt(edge, color);
            gradient.setColorAt(1.0 - edge, color);
            gradient.setColorAt(1.0, clear);
            return gradient;
        }
    }
    return color;
}

size_t qHash(const HudPainter::GlyphKey& key, size_t seed)
{
    return qHashMulti(seed, key.character, key.pixelSize, key.color, key.glow);
}

// ---------------------------------------------------------------------------
// Frame

void HudPainter::registerFont()
{
    static const int fontId = []() {
        const int id = QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/B612Mono-Bold.ttf"));
        if (id < 0) {
            qCWarning(HudPainterLog) << "Could not load the HUD font";
        }
        return id;
    }();
    Q_UNUSED(fontId);
}

void HudPainter::paint(QPainter& p, const HudFrame& frame)
{
    if (!std::isfinite(frame.width) || !std::isfinite(frame.height) || (frame.width < 1.0) || (frame.height < 1.0)) {
        return;
    }

    // Anything the vehicle has not reported yet is drawn as zero, and anything absurd is held in range, rather than
    // reaching QPainter or qRound as NaN or infinity
    auto readout = [](double value) { return std::clamp(finiteOr(value, 0.0), -kMaxReadout, kMaxReadout); };
    auto optional = [](double value) {
        return std::isfinite(value) ? std::clamp(value, -kMaxReadout, kMaxReadout) : qQNaN();
    };
    HudFrame f = frame;
    f.roll = wrap180(finiteOr(f.roll, 0.0));
    f.pitch = std::clamp(finiteOr(f.pitch, 0.0), -90.0, 90.0);
    f.heading = wrap360(finiteOr(f.heading, 0.0));
    f.flightPathValid = f.flightPathValid && std::isfinite(f.flightPathAngle) && std::isfinite(f.drift);
    f.flightPathAngle = std::clamp(finiteOr(f.flightPathAngle, 0.0), -90.0, 90.0);
    f.drift = wrap180(finiteOr(f.drift, 0.0));
    f.turnRate = readout(f.turnRate);
    f.airspeed = optional(f.airspeed);
    f.groundspeed = readout(f.groundspeed);
    f.speedTrend = readout(f.speedTrend);
    f.targetSpeed = optional(f.targetSpeed);
    f.verticalSpeed = readout(f.verticalSpeed);
    f.altitude = readout(f.altitude);
    f.altitudeTrend = readout(f.altitudeTrend);
    f.targetAltitude = optional(f.targetAltitude);
    f.speedRange = (std::isfinite(f.speedRange) && (f.speedRange > 0.0)) ? std::min(f.speedRange, 1e6) : 12.0;
    f.altitudeRange =
        (std::isfinite(f.altitudeRange) && (f.altitudeRange > 0.0)) ? std::min(f.altitudeRange, 1e6) : 15.0;
    f.distanceToHome = optional(f.distanceToHome);
    f.wpDistance = optional(f.wpDistance);
    f.linkQuality = std::isfinite(f.linkQuality) ? std::clamp(f.linkQuality, 0.0, 100.0) : qQNaN();
    f.vibration = optional(f.vibration);

    _layout(f);
    _devicePixelRatio = p.device() ? p.device()->devicePixelRatio() : 1.0;
    p.setRenderHint(QPainter::Antialiasing, true);

    if (f.drawBackground) {
        _drawBackground(p, f);
    }
    _drawPitchLadder(p, f);
    _drawBankScale(p, f);
    _drawHeadingTape(p, f);
    _drawSpeedTape(p, f);
    _drawAltitudeTape(p, f);
    _drawWaterline(p, f);
    _drawFlightPathMarker(p, f);
    _drawSystems(p, f);
    _drawBatteries(p, f);
    _drawNavigation(p, f);
    _drawArming(p, f);
    _drawFlightMode(p, f);
    _drawAlerts(p, f);
}

void HudPainter::_layout(const HudFrame& f)
{
    _cx = f.width / 2.0;
    _cy = f.height / 2.0;
    _unit = std::min(f.height, f.width * 0.75) / 100.0;
    _detail = (_unit >= 4.4) ? Detail::Full : (_unit >= 2.2) ? Detail::Compact : Detail::Minimal;
    const bool minimal = _detail == Detail::Minimal;
    _lineWidth = std::max(1.25, 0.24 * _unit);
    _haloWidth = std::max(1.0, 0.2 * _unit);
    _textSize = std::max(minimal ? 7.5 : 8.5, 2.6 * _unit);
    _smallTextSize = std::max(minimal ? 7.0 : 7.5, 2.05 * _unit);
    _largeTextSize = std::max(minimal ? 9.0 : 10.0, 3.3 * _unit);
    _pxPerDegree = 1.75 * _unit;
    _tapeOffset = std::min(0.31 * f.width, 44.0 * _unit);
    _tapeHalfHeight = std::min(0.27 * f.height, 27.0 * _unit);
    _fieldRadius = std::max(10.0 * _unit, std::min(0.36 * f.height, _tapeOffset - 5.0 * _unit));
    _margin = std::max(3.0, 1.6 * _unit);
    _glow = f.drawBackground;
}

QColor HudPainter::_symbolColor(const HudFrame& f) const
{
    return f.connected ? kSymbol : kSymbolLost;
}

// ---------------------------------------------------------------------------
// Primitives

void HudPainter::_stroke(QPainter& p, const QPainterPath& path, const QColor& color, qreal width) const
{
    _stroke(p, path, color, width, Fade{});
}

void HudPainter::_stroke(QPainter& p, const QPainterPath& path, const QColor& color, qreal width,
                         const Fade& fade) const
{
    const qreal lineWidth = width > 0.0 ? width : _lineWidth;
    p.setBrush(Qt::NoBrush);
    // A glow on the dark synthetic sky; over video a dark halo, so the line holds against bright sky and cloud
    const QColor under = _glow ? withAlpha(color, 0.2) : withAlpha(kHalo, color.alphaF());
    p.setPen(QPen(fade.brush(under), lineWidth + (_glow ? 2.5 : 2.0) * _haloWidth, Qt::SolidLine, Qt::RoundCap,
                  Qt::RoundJoin));
    p.drawPath(path);
    p.setPen(QPen(fade.brush(color), lineWidth, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin));
    p.drawPath(path);
}

void HudPainter::_fill(QPainter& p, const QPainterPath& path, const QColor& color) const
{
    p.setBrush(Qt::NoBrush);
    const QColor under = _glow ? withAlpha(color, 0.2) : withAlpha(kHalo, color.alphaF());
    p.setPen(QPen(under, (_glow ? 2.5 : 2.0) * _haloWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPath(path);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawPath(path);
}

void HudPainter::_plate(QPainter& p, const QPainterPath& path) const
{
    p.setPen(Qt::NoPen);
    p.setBrush(kPlate);
    p.drawPath(path);
}

void HudPainter::_arrow(QPainter& p, QPointF center, qreal size, double angle, const QColor& color) const
{
    // Points along angle, clockwise from straight up
    QTransform transform;
    transform.translate(center.x(), center.y());
    transform.rotate(angle);
    QPainterPath arrow;
    arrow.moveTo(0.0, 0.5 * size);
    arrow.lineTo(0.0, -0.5 * size);
    arrow.moveTo(-0.32 * size, -0.14 * size);
    arrow.lineTo(0.0, -0.5 * size);
    arrow.lineTo(0.32 * size, -0.14 * size);
    _stroke(p, transform.map(arrow), color);
}

QRectF HudPainter::_callout(QPainter& p, qreal centerY, qreal size, const QString& text, const QColor& color,
                            bool framed)
{
    const qreal width = _textWidth(size, text) + 2.0 * size;
    const qreal height = 1.6 * size;
    const QRectF rect(_cx - width / 2.0, centerY - height / 2.0, width, height);
    QPainterPath box;
    box.addRect(rect);
    _plate(p, box);
    if (framed) {
        _stroke(p, box, color);
    }
    _text(p, _cx, centerY, size, text, color, Align::Center);
    return rect;
}

void HudPainter::_rollingNumber(QPainter& p, const QRectF& box, double value, qreal size, const QColor& color)
{
    // An odometer that rests on the rounded value and turns over quickly around each half, the higher digits along
    // with it while every digit below shows 9, so changes roll in without the readout sitting between two numbers
    const double magnitude = std::min(std::abs(value), kMaxReadout);
    const qint64 whole = static_cast<qint64>(std::floor(magnitude));
    const double fraction = magnitude - static_cast<double>(whole);
    double turn = std::clamp((fraction - 0.5) / kRollBand + 0.5, 0.0, 1.0);
    if (turn < 0.001) {
        turn = 0.0;
    }
    const qreal advance = _textWidth(size, QStringLiteral("0"));
    // Far enough apart that a resting digit's neighbours stay outside the box
    const qreal row = 1.2 * size;
    const qreal centerY = box.center().y();

    p.save();
    p.setClipRect(box);
    qreal x = box.right() - advance;
    qint64 rest = whole;
    bool lowerAllNines = true;
    for (int position = 0; position < 12; position++) {
        const bool beyond = (position > 0) && (rest == 0);
        const double digitTurn = lowerAllNines ? turn : 0.0;
        if (beyond && digitTurn <= 0.0) {
            break;
        }
        const int digit = static_cast<int>(rest % 10);
        if (!beyond) {
            _text(p, x, centerY - digitTurn * row, size, QString::number(digit), color);
        }
        if (digitTurn > 0.0) {
            const int next = beyond ? 1 : (digit + 1) % 10;
            _text(p, x, centerY + (1.0 - digitTurn) * row, size, QString::number(next), color);
        }
        x -= advance;
        if (beyond) {
            break;
        }
        lowerAllNines = lowerAllNines && (digit == 9);
        rest /= 10;
    }
    if (value <= -0.5) {
        _text(p, x, centerY, size, QStringLiteral("-"), color);
    }
    p.restore();
}

// ---------------------------------------------------------------------------
// Text

qreal HudPainter::_textWidth(qreal size, const QString& text)
{
    const int pixelSize = qMax(1, qRound(size));
    qreal width = 0.0;
    for (const QChar character : text) {
        width += _glyph(character, pixelSize, kSymbol, _devicePixelRatio).advance;
    }
    return width;
}

const HudPainter::Glyph& HudPainter::_glyph(QChar character, int pixelSize, const QColor& color, qreal devicePixelRatio)
{
    if ((devicePixelRatio != _glyphsDevicePixelRatio) || (_glyphs.size() > kMaxCachedGlyphs)) {
        _glyphs.clear();
        _glyphsDevicePixelRatio = devicePixelRatio;
    }
    const GlyphKey key{character.unicode(), pixelSize, color.rgba(), _glow};
    const auto cached = _glyphs.constFind(key);
    if (cached != _glyphs.cend()) {
        return *cached;
    }

    QFont font(kFontFamily);
    font.setPixelSize(pixelSize);
    font.setBold(true);
    const QFontMetricsF metrics(font);
    Glyph glyph;
    glyph.advance = metrics.horizontalAdvance(character);

    QPainterPath path;
    path.addText(0.0, 0.0, font, QString(character));
    if (!path.isEmpty()) {
        const qreal halo = std::max(1.0, 0.075 * pixelSize) * (_glow ? 1.25 : 1.0);
        const QRectF bounds = path.boundingRect().adjusted(-halo, -halo, halo, halo);
        const QRect pixels =
            QRectF(bounds.topLeft() * devicePixelRatio, bounds.size() * devicePixelRatio).toAlignedRect();
        glyph.image = QImage(pixels.size(), QImage::Format_ARGB32_Premultiplied);
        glyph.image.fill(Qt::transparent);
        QPainter painter(&glyph.image);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.translate(-pixels.topLeft());
        painter.scale(devicePixelRatio, devicePixelRatio);
        const QColor haloColor = _glow ? withAlpha(color, 0.25) : withAlpha(kHalo, 1.25 * color.alphaF());
        painter.strokePath(path, QPen(haloColor, 2.0 * halo, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.fillPath(path, color);
        painter.end();
        glyph.image.setDevicePixelRatio(devicePixelRatio);
        glyph.origin = QPointF(pixels.left() / devicePixelRatio, pixels.top() / devicePixelRatio);
    }
    return *_glyphs.insert(key, glyph);
}

qreal HudPainter::_text(QPainter& p, qreal x, qreal y, qreal size, const QString& text, const QColor& color,
                        Align align)
{
    if (text.isEmpty() || (size < 1.0)) {
        return 0.0;
    }
    const int pixelSize = qMax(1, qRound(size));
    const qreal width = _textWidth(size, text);
    qreal pen = x;
    if (align == Align::Center) {
        pen -= width / 2.0;
    } else if (align == Align::Right) {
        pen -= width;
    }
    // Centred on y by the height of the digits and capitals
    const qreal baseline = y + 0.36 * pixelSize;
    const QTransform device = p.deviceTransform();
    const bool snap = device.type() <= QTransform::TxScale;
    const QTransform toLogical = device.inverted();
    for (const QChar character : text) {
        const Glyph& glyph = _glyph(character, pixelSize, color, _devicePixelRatio);
        if (!glyph.image.isNull()) {
            QPointF topLeft = QPointF(pen, baseline) + glyph.origin;
            if (snap) {
                // On whole device pixels, so the glyph is copied rather than resampled
                const QPointF pixel = device.map(topLeft);
                topLeft = toLogical.map(QPointF(std::round(pixel.x()), std::round(pixel.y())));
            }
            p.drawImage(topLeft, glyph.image);
        }
        pen += glyph.advance;
    }
    return width;
}

QString HudPainter::_formatDistance(double distance, const HudFrame& f) const
{
    if (f.distanceUnits == QLatin1String("m") && distance >= 1000.0) {
        return QStringLiteral("%1km").arg(distance / 1000.0, 0, 'f', 1);
    }
    if (f.distanceUnits == QLatin1String("ft") && distance >= 5280.0) {
        return QStringLiteral("%1mi").arg(distance / 5280.0, 0, 'f', 1);
    }
    return QStringLiteral("%1%2").arg(qRound64(distance)).arg(f.distanceUnits);
}

// ---------------------------------------------------------------------------
// Synthetic vision

void HudPainter::_drawBackground(QPainter& p, const HudFrame& f) const
{
    const qreal reach = std::hypot(f.width, f.height);
    const qreal horizonY = f.pitch * _pxPerDegree;

    p.save();
    p.translate(_cx, _cy);
    p.rotate(-f.roll);
    p.setPen(Qt::NoPen);

    QLinearGradient sky(0.0, horizonY - 0.75 * f.height, 0.0, horizonY);
    sky.setColorAt(0.0, kSkyZenith);
    sky.setColorAt(1.0, kSkyHorizon);
    p.fillRect(QRectF(-reach, horizonY - 2.0 * reach, 2.0 * reach, 2.0 * reach), sky);

    QLinearGradient ground(0.0, horizonY, 0.0, horizonY + 0.75 * f.height);
    ground.setColorAt(0.0, kGroundHorizon);
    ground.setColorAt(1.0, kGroundNadir);
    p.fillRect(QRectF(-reach, horizonY, 2.0 * reach, 2.0 * reach), ground);

    // A thin band of light along the horizon
    const QColor symbol = _symbolColor(f);
    const qreal band = 2.5 * _unit;
    QLinearGradient glow(0.0, horizonY - band, 0.0, horizonY + band);
    glow.setColorAt(0.0, withAlpha(symbol, 0.0));
    glow.setColorAt(0.5, withAlpha(symbol, 0.3));
    glow.setColorAt(1.0, withAlpha(symbol, 0.0));
    p.fillRect(QRectF(-reach, horizonY - band, 2.0 * reach, 2.0 * band), glow);
    p.setPen(QPen(withAlpha(symbol, 0.55), 1.0));
    p.drawLine(QPointF(-reach, horizonY), QPointF(reach, horizonY));
    p.restore();
}

// ---------------------------------------------------------------------------
// Attitude

void HudPainter::_drawPitchLadder(QPainter& p, const HudFrame& f)
{
    const QColor color = _symbolColor(f);
    const Fade fade{Fade::Kind::Radial, QPointF(0.0, 0.0), 0.68 * _fieldRadius, _fieldRadius};
    const qreal gap = 4.2 * _unit;
    const qreal tick = 0.9 * _unit;

    p.save();
    p.translate(_cx, _cy);
    p.rotate(-f.roll);

    QPainterPath horizon;
    QPainterPath rungs;

    struct Label
    {
        QPointF position;
        QString text;
        Align align;
    };

    QList<Label> labels;
    for (int angle = -90; angle <= 90; angle += 5) {
        const qreal y = (f.pitch - angle) * _pxPerDegree;
        if (std::abs(y) > _fieldRadius) {
            continue;
        }
        if (angle == 0) {
            horizon.moveTo(-_fieldRadius, y);
            horizon.lineTo(-gap, y);
            horizon.moveTo(gap, y);
            horizon.lineTo(_fieldRadius, y);
            continue;
        }
        if (std::abs(angle) == 90) {
            // Zenith and nadir
            const qreal r = 1.2 * _unit;
            rungs.addEllipse(QPointF(0.0, y), r, r);
            const qreal d = (angle > 0 ? 0.35 : 0.7) * r;
            rungs.moveTo(-d, y - d);
            rungs.lineTo(d, y + d);
            rungs.moveTo(d, y - d);
            rungs.lineTo(-d, y + d);
            continue;
        }

        if (angle % 10 != 0) {
            // Five-degree marks: short, plain, and only with room for them
            if (_detail != Detail::Full) {
                continue;
            }
            for (const qreal side : {-1.0, 1.0}) {
                const qreal length = 2.2 * _unit;
                if (angle > 0) {
                    rungs.moveTo(side * gap, y);
                    rungs.lineTo(side * (gap + length), y);
                } else {
                    rungs.moveTo(side * gap, y);
                    rungs.lineTo(side * (gap + 0.4 * length), y);
                    rungs.moveTo(side * (gap + 0.6 * length), y);
                    rungs.lineTo(side * (gap + length), y);
                }
            }
            continue;
        }

        const qreal length = 7.0 * _unit;
        // Climb lines are solid; dive lines are dashed and bent by half the pitch angle, ends toward the horizon
        const double bend = angle < 0 ? qDegreesToRadians(-angle / 2.0) : 0.0;
        const qreal towardHorizon = angle > 0 ? 1.0 : -1.0;
        for (const qreal side : {-1.0, 1.0}) {
            const QPointF inner(side * gap, y);
            const QPointF direction(side * std::cos(bend), -std::sin(bend));
            const QPointF outer = inner + direction * length;
            const QPointF tickEnd = outer + QPointF(0.0, towardHorizon * tick);
            if (angle > 0) {
                rungs.moveTo(inner);
                rungs.lineTo(outer);
                rungs.lineTo(tickEnd);
            } else {
                const qreal dash = length / 4.2;
                for (int i = 0; i < 3; i++) {
                    const qreal start = i * 1.6 * dash;
                    rungs.moveTo(inner + direction * start);
                    rungs.lineTo(inner + direction * (start + dash));
                }
                rungs.lineTo(tickEnd);
            }
            if (_detail != Detail::Minimal) {
                const QPointF at = outer + direction * (0.8 * _unit) + QPointF(0.0, towardHorizon * tick * 0.5);
                labels.append({at, QString::number(angle), side < 0 ? Align::Right : Align::Left});
            }
        }
    }
    _stroke(p, horizon, color, 1.25 * _lineWidth, fade);
    _stroke(p, rungs, color, 0.0, fade);
    for (const Label& label : labels) {
        p.setOpacity(fade.alphaAt(label.position));
        _text(p, label.position.x(), label.position.y(), _smallTextSize, label.text, color, label.align);
    }
    p.setOpacity(1.0);
    p.restore();
}

void HudPainter::_drawBankScale(QPainter& p, const HudFrame& f) const
{
    const QColor color = _symbolColor(f);
    const qreal r = _fieldRadius;
    const QPointF center(_cx, _cy);
    auto along = [](double degrees) {
        // From straight down, toward the right for positive angles
        const double radians = qDegreesToRadians(degrees);
        return QPointF(std::sin(radians), std::cos(radians));
    };

    QPainterPath scale;
    const QRectF circle(_cx - r, _cy - r, 2.0 * r, 2.0 * r);
    scale.arcMoveTo(circle, 210.0);
    scale.arcTo(circle, 210.0, 120.0);
    for (const int angle : {-60, -45, -30, -20, -10, 10, 20, 30, 45, 60}) {
        const qreal length = (std::abs(angle) % 30 == 0) ? 1.7 * _unit : 0.9 * _unit;
        scale.moveTo(center + along(angle) * r);
        scale.lineTo(center + along(angle) * (r + length));
    }
    // Fixed reference under the scale's zero
    scale.moveTo(_cx, _cy + r + 0.25 * _unit);
    scale.lineTo(_cx - 0.7 * _unit, _cy + r + 1.5 * _unit);
    scale.lineTo(_cx + 0.7 * _unit, _cy + r + 1.5 * _unit);
    scale.closeSubpath();
    _stroke(p, scale, color);

    // The pointer turns with the horizon
    const QPointF direction = along(f.roll);
    const QPointF across(direction.y(), -direction.x());
    const QPointF tip = center + direction * (r - 0.25 * _unit);
    const QPointF back = center + direction * (r - 1.7 * _unit);
    QPainterPath pointer;
    pointer.moveTo(tip);
    pointer.lineTo(back + across * (0.8 * _unit));
    pointer.lineTo(back - across * (0.8 * _unit));
    pointer.closeSubpath();
    _fill(p, pointer, std::abs(f.roll) > 45.0 ? kCaution : color);
}

void HudPainter::_drawWaterline(QPainter& p, const HudFrame& f) const
{
    // The nose: where the camera and the airframe point
    const qreal u = _unit;
    QPainterPath w;
    w.moveTo(_cx - 3.4 * u, _cy);
    w.lineTo(_cx - 1.7 * u, _cy);
    w.lineTo(_cx - 0.85 * u, _cy + 1.3 * u);
    w.lineTo(_cx, _cy);
    w.lineTo(_cx + 0.85 * u, _cy + 1.3 * u);
    w.lineTo(_cx + 1.7 * u, _cy);
    w.lineTo(_cx + 3.4 * u, _cy);
    _stroke(p, w, _symbolColor(f));
}

void HudPainter::_drawFlightPathMarker(QPainter& p, const HudFrame& f) const
{
    if (!f.flightPathValid) {
        return;
    }
    // Where the vehicle is actually going, on the ladder's frame: across by the drift, up by the climb angle
    QTransform roll;
    roll.rotate(-f.roll);
    QPointF offset = roll.map(QPointF(f.drift * _pxPerDegree, (f.pitch - f.flightPathAngle) * _pxPerDegree));
    const qreal limit = _fieldRadius - 3.0 * _unit;
    const qreal distance = std::hypot(offset.x(), offset.y());
    const bool limited = distance > limit;
    if (limited) {
        offset *= limit / distance;
    }

    const QPointF c = QPointF(_cx, _cy) + offset;
    const qreal r = 1.05 * _unit;
    QPainterPath marker;
    marker.addEllipse(c, r, r);
    marker.moveTo(c.x() - r - 1.9 * _unit, c.y());
    marker.lineTo(c.x() - r, c.y());
    marker.moveTo(c.x() + r, c.y());
    marker.lineTo(c.x() + r + 1.9 * _unit, c.y());
    marker.moveTo(c.x(), c.y() - r);
    marker.lineTo(c.x(), c.y() - r - 1.2 * _unit);
    if (limited) {
        // Off the field of view: held at its edge and crossed out
        const qreal d = 0.7 * r;
        marker.moveTo(c.x() - d, c.y() - d);
        marker.lineTo(c.x() + d, c.y() + d);
        marker.moveTo(c.x() + d, c.y() - d);
        marker.lineTo(c.x() - d, c.y() + d);
    }
    _stroke(p, marker, _symbolColor(f), 1.15 * _lineWidth);
}

// ---------------------------------------------------------------------------
// Heading

void HudPainter::_drawHeadingTape(QPainter& p, const HudFrame& f)
{
    const QColor color = _symbolColor(f);
    const qreal halfWidth = std::max(10.0 * _unit, std::min(0.27 * f.width, _tapeOffset - 6.0 * _unit));
    const qreal pxPerDegree = halfWidth / kHeadingHalfRange;
    const Fade fade{Fade::Kind::Horizontal, QPointF(_cx, 0.0), 0.7 * halfWidth, halfWidth};
    const double heading = f.heading;
    const qreal labelY = _margin + 0.5 * _smallTextSize + 0.2 * _unit;
    const qreal baseY = labelY + 0.6 * _smallTextSize + 1.5 * _unit;
    auto xFor = [&](double bearing) {
        return _cx + std::clamp(wrap180(bearing - heading), -kHeadingHalfRange, kHeadingHalfRange) * pxPerDegree;
    };

    // The readout sits on the tape's centre, over the marks it would hide, its caret on the base line
    const QString text = QStringLiteral("%1").arg(qRound(heading) % 360, 3, 10, QLatin1Char('0'));
    const qreal boxHalfWidth = (_textWidth(_largeTextSize, text) + 1.6 * _unit) / 2.0;
    const qreal boxHeight = 1.3 * _largeTextSize;
    const qreal boxTop = labelY - 0.65 * _largeTextSize;
    const qreal boxBottom = boxTop + boxHeight;

    QPainterPath ticks;
    const bool labelled = _detail != Detail::Minimal;
    const int labelStep = (10.0 * pxPerDegree >= 2.4 * _textWidth(_smallTextSize, QStringLiteral("00"))) ? 10 : 30;
    const int first = static_cast<int>(std::ceil((heading - kHeadingHalfRange) / 5.0)) * 5;
    for (int degrees = first; degrees <= heading + kHeadingHalfRange; degrees += 5) {
        const qreal x = _cx + (degrees - heading) * pxPerDegree;
        if (std::abs(x - _cx) < boxHalfWidth + 0.4 * _unit) {
            continue;
        }
        const int wrapped = ((degrees % 360) + 360) % 360;
        ticks.moveTo(x, baseY);
        ticks.lineTo(x, baseY - ((wrapped % 10 == 0) ? 1.4 : 0.7) * _unit);
        if (labelled && (wrapped % labelStep == 0) && (std::abs(x - _cx) > boxHalfWidth + 0.8 * _smallTextSize)) {
            static const QStringList cardinals = {QStringLiteral("N"), QStringLiteral("E"), QStringLiteral("S"),
                                                  QStringLiteral("W")};
            const QString label = (wrapped % 90 == 0) ? cardinals.at(wrapped / 90)
                                                      : QStringLiteral("%1").arg(wrapped / 10, 2, 10, QLatin1Char('0'));
            p.setOpacity(fade.alphaAt(QPointF(x, labelY)));
            _text(p, x, labelY, _smallTextSize, label, color, Align::Center);
        }
    }
    p.setOpacity(1.0);
    _stroke(p, ticks, color, 0.0, fade);

    // Where the heading will be in a few seconds at this turn rate
    const double turn = std::clamp(f.turnRate * kTurnTrendSeconds, -kHeadingHalfRange, kHeadingHalfRange);
    if (std::abs(turn) * pxPerDegree > boxHalfWidth) {
        const qreal end = _cx + turn * pxPerDegree;
        QPainterPath trend;
        trend.moveTo(_cx + std::copysign(boxHalfWidth, turn), baseY);
        trend.lineTo(end, baseY);
        trend.moveTo(end, baseY - 0.7 * _unit);
        trend.lineTo(end, baseY + 0.7 * _unit);
        _stroke(p, trend, color, 1.6 * _lineWidth);
    }

    const qreal markerY = baseY + 0.5 * _unit;
    if (f.flightPathValid && std::isfinite(f.groundTrack) &&
        std::abs(wrap180(f.groundTrack - heading)) <= kHeadingHalfRange) {
        const qreal x = xFor(f.groundTrack);
        QPainterPath track;
        track.moveTo(x, markerY);
        track.lineTo(x - 0.6 * _unit, markerY + 1.0 * _unit);
        track.lineTo(x + 0.6 * _unit, markerY + 1.0 * _unit);
        track.closeSubpath();
        _fill(p, track, color);
    }
    if (std::isfinite(f.targetHeading)) {
        // Notched bug on the heading the autopilot is steering for, held at the tape's end when off it
        const qreal x = xFor(f.targetHeading);
        const qreal half = 1.0 * _unit;
        QPainterPath bug;
        bug.moveTo(x - 0.4 * _unit, markerY);
        bug.lineTo(x - half, markerY);
        bug.lineTo(x - half, markerY + 1.1 * _unit);
        bug.lineTo(x + half, markerY + 1.1 * _unit);
        bug.lineTo(x + half, markerY);
        bug.lineTo(x + 0.4 * _unit, markerY);
        bug.lineTo(x, markerY + 0.55 * _unit);
        bug.closeSubpath();
        _stroke(p, bug, color);
    }
    if (f.homeSet && std::isfinite(f.headingToHome) && std::isfinite(f.distanceToHome) && f.distanceToHome > 0.0) {
        // Home, with a chevron pointing on round when it is off the tape
        const double relative = wrap180(f.headingToHome - heading);
        const qreal x = xFor(f.headingToHome);
        const qreal y = markerY + 1.8 * _unit + 0.5 * _smallTextSize;
        _text(p, x, y, _smallTextSize, QStringLiteral("H"), color, Align::Center);
        if (std::abs(relative) > kHeadingHalfRange) {
            const qreal side = relative > 0.0 ? 1.0 : -1.0;
            const qreal chevronX = x + side * 0.75 * _smallTextSize;
            const qreal s = 0.3 * _smallTextSize;
            QPainterPath chevron;
            chevron.moveTo(chevronX - side * s, y - s);
            chevron.lineTo(chevronX, y);
            chevron.lineTo(chevronX - side * s, y + s);
            _stroke(p, chevron, color);
        }
    }

    QPainterPath box;
    const qreal caret = 0.7 * _unit;
    box.moveTo(_cx - boxHalfWidth, boxTop);
    box.lineTo(_cx + boxHalfWidth, boxTop);
    box.lineTo(_cx + boxHalfWidth, boxBottom);
    box.lineTo(_cx + caret, boxBottom);
    box.lineTo(_cx, std::max(boxBottom + caret, baseY + 0.3 * _unit));
    box.lineTo(_cx - caret, boxBottom);
    box.lineTo(_cx - boxHalfWidth, boxBottom);
    box.closeSubpath();
    _plate(p, box);
    _stroke(p, box, color);
    _text(p, _cx, boxTop + boxHeight / 2.0, _largeTextSize, text, color, Align::Center);

    if (_detail != Detail::Minimal) {
        _drawCrossTrack(p, f, markerY + 2.0 * _unit + _smallTextSize);
    }
}

void HudPainter::_drawCrossTrack(QPainter& p, const HudFrame& f, qreal top)
{
    if (!std::isfinite(f.xtrack)) {
        return;
    }
    // Course deviation: dots every 20 m, the diamond on the track
    const QColor color = _symbolColor(f);
    const qreal dot = 3.0 * _unit;
    const qreal y = top + 0.6 * _unit;
    QPainterPath scale;
    for (const int k : {-2, -1, 1, 2}) {
        scale.addEllipse(QPointF(_cx + k * dot, y), 0.32 * _unit, 0.32 * _unit);
    }
    scale.moveTo(_cx, y - 0.8 * _unit);
    scale.lineTo(_cx, y + 0.8 * _unit);
    _stroke(p, scale, withAlpha(color, 0.8));

    const double dots = f.xtrack / kXtrackMetersPerDot;
    const bool limited = std::abs(dots) > 2.4;
    const qreal x = _cx + std::clamp(dots, -2.4, 2.4) * dot;
    const qreal d = 0.75 * _unit;
    QPainterPath diamond;
    diamond.moveTo(x, y - d);
    diamond.lineTo(x + d, y);
    diamond.lineTo(x, y + d);
    diamond.lineTo(x - d, y);
    diamond.closeSubpath();
    if (limited) {
        _stroke(p, diamond, color);
    } else {
        _fill(p, diamond, color);
    }
    if (_detail == Detail::Full) {
        _text(p, _cx - 2.0 * dot - 1.4 * _unit, y, 0.85 * _smallTextSize, QStringLiteral("XTK"), withAlpha(color, 0.8),
              Align::Right);
    }
}

// ---------------------------------------------------------------------------
// Tapes

void HudPainter::_drawTape(QPainter& p, const HudFrame& f, const Tape& tape)
{
    const QColor color = _symbolColor(f);
    const qreal outward = tape.rightSide ? 1.0 : -1.0;
    const qreal spineX = _cx + outward * _tapeOffset;
    const qreal top = _cy - _tapeHalfHeight;
    const qreal bottom = _cy + _tapeHalfHeight;
    const qreal pxPerUnit = _tapeHalfHeight / tape.range;
    auto yFor = [&](double value) { return _cy - (value - tape.value) * pxPerUnit; };
    const Fade fade{Fade::Kind::Vertical, QPointF(spineX, _cy), 0.55 * _tapeHalfHeight, _tapeHalfHeight};
    const Align labelAlign = tape.rightSide ? Align::Left : Align::Right;
    const qreal boxHeight = 1.4 * _largeTextSize;

    // Labels at least two and a half text heights apart, ticks at least one and a half units
    const double major = niceStep(2.5 * _smallTextSize / pxPerUnit);
    double minor = major;
    for (const double divisor : {5.0, 2.0}) {
        if (major / divisor * pxPerUnit >= 1.5 * _unit) {
            minor = major / divisor;
            break;
        }
    }

    // A speed never goes below zero, so the tape stops there
    const qreal end = tape.positiveOnly ? std::min(bottom, yFor(0.0)) : bottom;
    QPainterPath ticks;
    if (end > top) {
        ticks.moveTo(spineX, top);
        ticks.lineTo(spineX, end);
    }
    const double lowest = tape.positiveOnly ? std::max(0.0, tape.value - tape.range) : tape.value - tape.range;
    const double firstValue = std::ceil(lowest / minor) * minor;
    for (double value = firstValue; value <= tape.value + tape.range; value += minor) {
        const qreal y = yFor(value);
        const bool isMajor = isMultiple(value, major);
        ticks.moveTo(spineX, y);
        ticks.lineTo(spineX + outward * (isMajor ? 1.6 : 0.8) * _unit, y);
        if (isMajor && (_detail != Detail::Minimal) && (std::abs(y - _cy) > boxHeight / 2.0 + 0.5 * _smallTextSize)) {
            const qreal x = spineX + outward * 2.3 * _unit;
            p.setOpacity(fade.alphaAt(QPointF(x, y)));
            _text(p, x, y, _smallTextSize, QString::number(qRound64(value)), color, labelAlign);
        }
    }
    p.setOpacity(1.0);
    _stroke(p, ticks, color, 0.0, fade);

    if (tape.ground && (tape.value - tape.range < 0.0)) {
        // Hatched below the home altitude
        const qreal groundY = std::max(top, yFor(0.0));
        const qreal width = 1.6 * _unit;
        const qreal spacing = 0.9 * _unit;
        QPainterPath hatch;
        hatch.moveTo(spineX, groundY);
        hatch.lineTo(spineX + outward * width, groundY);
        for (qreal y = groundY + spacing; y < bottom + width; y += spacing) {
            const qreal startY = std::min(y, bottom);
            const qreal endY = std::max(groundY, y - width);
            hatch.moveTo(spineX + outward * (y - startY), startY);
            hatch.lineTo(spineX + outward * (y - endY), endY);
        }
        _stroke(p, hatch, withAlpha(color, 0.75), 0.6 * _lineWidth, fade);
    }

    if (std::isfinite(tape.target)) {
        // Hollow bug on the autopilot's target, held at the tape's end with its value when off it
        const qreal targetY = yFor(tape.target);
        const qreal y = std::clamp(targetY, top, bottom);
        const qreal inward = -outward;
        QPainterPath bug;
        bug.moveTo(spineX + inward * 0.35 * _unit, y);
        bug.lineTo(spineX + inward * 1.7 * _unit, y - 0.85 * _unit);
        bug.lineTo(spineX + inward * 1.7 * _unit, y + 0.85 * _unit);
        bug.closeSubpath();
        _stroke(p, bug, color);
        if ((targetY != y) && (_detail != Detail::Minimal)) {
            _text(p, spineX + inward * 2.3 * _unit, y, _smallTextSize, QString::number(qRound64(tape.target)), color,
                  tape.rightSide ? Align::Right : Align::Left);
        }
    }

    const qreal trendY = std::clamp(yFor(tape.value + tape.trend), top, bottom);
    if (std::abs(trendY - _cy) > boxHeight / 2.0 + 0.5 * _unit) {
        // Where the value is heading over the next few seconds
        const qreal x = spineX - outward * 0.55 * _unit;
        const qreal head = trendY < _cy ? 0.7 * _unit : -0.7 * _unit;
        QPainterPath trend;
        trend.moveTo(x, _cy);
        trend.lineTo(x, trendY);
        trend.moveTo(x - 0.45 * _unit, trendY + head);
        trend.lineTo(x, trendY);
        trend.lineTo(x + 0.45 * _unit, trendY + head);
        _stroke(p, trend, color, 1.3 * _lineWidth);
    }

    // Readout over the tape's centre, pointing at the spine
    const qreal digitsWidth = tape.digits * _textWidth(_largeTextSize, QStringLiteral("0"));
    const qreal tipX = spineX + outward * 0.3 * _unit;
    const qreal neckX = spineX + outward * 1.4 * _unit;
    const qreal farX = neckX + outward * (digitsWidth + 1.4 * _unit);
    QPainterPath box;
    box.moveTo(tipX, _cy);
    box.lineTo(neckX, _cy - 0.32 * boxHeight);
    box.lineTo(neckX, _cy - boxHeight / 2.0);
    box.lineTo(farX, _cy - boxHeight / 2.0);
    box.lineTo(farX, _cy + boxHeight / 2.0);
    box.lineTo(neckX, _cy + boxHeight / 2.0);
    box.lineTo(neckX, _cy + 0.32 * boxHeight);
    box.closeSubpath();
    _plate(p, box);
    _stroke(p, box, color);
    const qreal digitsRight = std::max(neckX, farX) - 0.7 * _unit;
    const QRectF digits(digitsRight - digitsWidth - 0.3 * _unit, _cy - boxHeight / 2.0 + 0.5 * _lineWidth,
                        digitsWidth + 0.3 * _unit, boxHeight - _lineWidth);
    _rollingNumber(p, digits, tape.value, _largeTextSize, color);

    if (_detail != Detail::Minimal) {
        _text(p, spineX, top - 2.2 * _unit, _smallTextSize, tape.caption, withAlpha(color, 0.85),
              tape.rightSide ? Align::Left : Align::Right);
    }
}

void HudPainter::_drawSpeedTape(QPainter& p, const HudFrame& f)
{
    const bool air = std::isfinite(f.airspeed) && (f.airspeed > 0.0);
    const double speed = air ? f.airspeed : f.groundspeed;
    _drawTape(
        p, f,
        Tape{.rightSide = false,
             .value = speed,
             .range = f.speedRange,
             .trend = f.armed ? f.speedTrend : 0.0,
             .target = f.targetSpeed,
             .ground = false,
             .positiveOnly = true,
             .caption = QStringLiteral("%1 %2").arg(air ? QStringLiteral("AS") : QStringLiteral("GS"), f.speedUnits),
             .digits = 3});

    if (_detail == Detail::Minimal) {
        return;
    }
    const QColor color = _symbolColor(f);
    const qreal x = _cx - _tapeOffset;
    qreal y = _cy + _tapeHalfHeight + 1.4 * _unit;
    if (air) {
        _text(p, x, y, _smallTextSize, QStringLiteral("GS %1").arg(qRound64(f.groundspeed)), color, Align::Right);
        y += 1.5 * _smallTextSize;
    }
    if (f.fixedWing && std::isfinite(f.aoa)) {
        const QColor aoaColor = f.aoa >= kAoaWarning ? kWarning : f.aoa >= kAoaCaution ? kCaution : color;
        _text(p, x, y, _smallTextSize, QStringLiteral("AOA %1").arg(f.aoa, 0, 'f', 1), aoaColor, Align::Right);
    }
}

void HudPainter::_drawAltitudeTape(QPainter& p, const HudFrame& f)
{
    _drawTape(p, f,
              Tape{.rightSide = true,
                   .value = f.altitude,
                   .range = f.altitudeRange,
                   .trend = f.altitudeTrend,
                   .target = f.targetAltitude,
                   .ground = f.homeSet,
                   .positiveOnly = false,
                   .caption = QStringLiteral("ALT %1").arg(f.altitudeUnits),
                   .digits = 4});

    if (_detail == Detail::Minimal) {
        return;
    }
    const QString climb = QStringLiteral("%1%2")
                              .arg(f.verticalSpeed >= 0.0 ? QStringLiteral("+") : QString())
                              .arg(f.verticalSpeed, 0, 'f', 1);
    _text(p, _cx + _tapeOffset, _cy + _tapeHalfHeight + 1.4 * _unit, _smallTextSize, QStringLiteral("VS %1").arg(climb),
          _symbolColor(f), Align::Left);
}

// ---------------------------------------------------------------------------
// Corners

void HudPainter::_drawSystems(QPainter& p, const HudFrame& f)
{
    if (!f.hasVehicle || (_detail == Detail::Minimal)) {
        return;
    }
    const QColor normal = _symbolColor(f);

    struct Row
    {
        QString label;
        QString value;
        int level = 0;  ///< 0 nominal, 1 caution, 2 warning
    };

    QList<Row> rows;

    if (!f.connected) {
        rows.append({QStringLiteral("LNK"), QStringLiteral("LOST"), 2});
    } else if (std::isfinite(f.linkQuality)) {
        rows.append({QStringLiteral("LNK"), QStringLiteral("%1%").arg(qRound(f.linkQuality)),
                     f.linkQuality >= 80.0   ? 0
                     : f.linkQuality >= 50.0 ? 1
                                             : 2});
    }
    auto gpsRow = [](const QString& label, int fix, int satellites) {
        static const QStringList names = {
            QStringLiteral("NO GPS"),  QStringLiteral("NO FIX"), QStringLiteral("2D"),
            QStringLiteral("3D"),      QStringLiteral("DGPS"),   QStringLiteral("RTK FLT"),
            QStringLiteral("RTK FIX"), QStringLiteral("STATIC"), QStringLiteral("PPP")};
        QString value = (fix >= 0 && fix < names.size()) ? names.at(fix) : QStringLiteral("?");
        if (satellites >= 0 && fix >= 2) {
            value += QStringLiteral(" %1 SAT").arg(satellites);
        }
        return Row{label, value, fix >= 3 ? 0 : fix == 2 ? 1 : 2};
    };
    if (f.gpsFix >= 0) {
        rows.append(gpsRow(QStringLiteral("GPS"), f.gpsFix, f.gpsSatellites));
    }
    if (f.gps2Fix > 0) {
        rows.append(gpsRow(QStringLiteral("GPS2"), f.gps2Fix, -1));
    }
    if (std::isfinite(f.ekfStatus)) {
        const int level = f.ekfStatus > kEkfWarning ? 2 : f.ekfStatus > kEkfCaution ? 1 : 0;
        rows.append({QStringLiteral("EKF"),
                     level == 0 ? QStringLiteral("OK") : QStringLiteral("%1").arg(f.ekfStatus, 0, 'f', 2), level});
    }
    if (std::isfinite(f.vibration)) {
        rows.append({QStringLiteral("VIB"), QString::number(qRound(f.vibration)),
                     f.vibration > kVibrationWarning   ? 2
                     : f.vibration > kVibrationCaution ? 1
                                                       : 0});
    }

    const qreal size = _smallTextSize;
    const qreal valueX = _margin + _textWidth(size, QStringLiteral("GPS2 "));
    qreal y = _margin + 0.5 * size + 0.2 * _unit;
    for (const Row& row : rows) {
        // Nominal rows recede, or are left out on a small HUD; anything off is boxed in its colour, warnings flashing
        if ((row.level == 0) && (_detail != Detail::Full)) {
            continue;
        }
        const QColor color = row.level == 2 ? kWarning : row.level == 1 ? kCaution : withAlpha(normal, 0.8);
        if ((row.level == 1) || ((row.level == 2) && blinkOn(f))) {
            const qreal width = valueX - _margin + _textWidth(size, row.value) + 0.8 * _unit;
            QPainterPath box;
            box.addRect(QRectF(_margin - 0.4 * _unit, y - 0.75 * size, width, 1.5 * size));
            _plate(p, box);
            _stroke(p, box, color, 0.8 * _lineWidth);
        }
        _text(p, _margin, y, size, row.label, color);
        _text(p, valueX, y, size, row.value, color);
        y += 1.8 * size;
    }
}

void HudPainter::_drawBatteries(QPainter& p, const HudFrame& f)
{
    if (!f.hasVehicle) {
        return;
    }
    const QColor normal = _symbolColor(f);
    const qreal right = f.width - _margin;
    qreal y = _margin + 0.5 * _textSize + 0.2 * _unit;

    auto drawBattery = [&](const QString& label, const HudFrame::Battery& battery) {
        if (!std::isfinite(battery.voltage) && !std::isfinite(battery.remaining)) {
            return;
        }
        // MAV_BATTERY_CHARGE_STATE: LOW is a caution, CRITICAL through UNHEALTHY a warning
        const int state = battery.chargeState;
        const QColor color = (state >= 3 && state <= 6) ? kWarning : state == 2 ? kCaution : normal;
        const QString percent = std::isfinite(battery.remaining)
                                    ? QStringLiteral("%1%").arg(qRound(std::clamp(battery.remaining, 0.0, 100.0)))
                                    : QStringLiteral("%1V").arg(battery.voltage, 0, 'f', 1);
        if (_detail == Detail::Minimal) {
            _text(p, right, y, _textSize, percent, color, Align::Right);
            y += 1.6 * _textSize;
            return;
        }
        const qreal labelWidth = _textWidth(_smallTextSize, label + QStringLiteral(" "));
        const qreal percentWidth = _textWidth(_textSize, QStringLiteral("100%"));
        _text(p, right, y, _textSize, percent, color, Align::Right);
        _text(p, right - percentWidth - 0.4 * _unit, y, _smallTextSize, label, withAlpha(color, 0.85), Align::Right);
        y += 0.5 * _textSize + 0.7 * _unit;

        // Ten-segment gauge under the percentage
        if (std::isfinite(battery.remaining)) {
            const qreal width = percentWidth + labelWidth;
            const qreal height = 0.75 * _unit;
            const qreal gap = 0.3 * _unit;
            const qreal segment = (width - 9.0 * gap) / 10.0;
            const int lit = qRound(std::clamp(battery.remaining, 0.0, 100.0) / 10.0);
            QPainterPath on;
            QPainterPath off;
            for (int i = 0; i < 10; i++) {
                (i < lit ? on : off).addRect(QRectF(right - width + i * (segment + gap), y, segment, height));
            }
            _fill(p, on, color);
            _stroke(p, off, withAlpha(color, 0.5), 0.5 * _lineWidth);
            y += height + 0.6 * _unit;
        }

        QStringList details;
        if (std::isfinite(battery.voltage)) {
            details.append(QStringLiteral("%1V").arg(battery.voltage, 0, 'f', 1));
        }
        if (std::isfinite(battery.current) && (_detail == Detail::Full)) {
            details.append(QStringLiteral("%1A").arg(battery.current, 0, 'f', 1));
        }
        y += 0.5 * _smallTextSize;
        _text(p, right, y, _smallTextSize, details.join(QStringLiteral(" ")), withAlpha(color, 0.85), Align::Right);
        y += 0.5 * _smallTextSize + 1.6 * _unit + 0.5 * _textSize;
    };

    drawBattery(QStringLiteral("BAT"), f.battery);
    if (std::isfinite(f.battery2.voltage) && f.battery2.voltage > 0.0) {
        drawBattery(QStringLiteral("BAT2"), f.battery2);
    }
}

void HudPainter::_drawNavigation(QPainter& p, const HudFrame& f)
{
    if (!f.hasVehicle || (_detail == Detail::Minimal)) {
        return;
    }
    const bool full = _detail == Detail::Full;
    const QColor color = _symbolColor(f);
    const qreal right = f.width - _margin;
    const qreal arrowSize = 1.1 * _smallTextSize;
    qreal y = f.height - _margin - 0.5 * _smallTextSize;

    // Right-aligned rows from the bottom up: label, an arrow relative to the nose, value
    auto row = [&](const QString& label, double bearing, const QString& value, qreal size) {
        qreal x = right - _textWidth(size, value);
        _text(p, x, y, size, value, color);
        if (std::isfinite(bearing)) {
            x -= 0.5 * _unit + arrowSize / 2.0;
            _arrow(p, QPointF(x, y), arrowSize, bearing - f.heading, color);
            x -= arrowSize / 2.0;
        }
        _text(p, x - 0.6 * _unit, y, _smallTextSize, label, withAlpha(color, 0.85), Align::Right);
        y -= 1.9 * size;
    };

    if (std::isfinite(f.windSpeed)) {
        // The arrow shows where the wind blows to
        const bool arrow = std::isfinite(f.windDirection) && f.windSpeed >= 0.05;
        row(QStringLiteral("WIND"), arrow ? f.windDirection + 180.0 : qQNaN(),
            QStringLiteral("%1%2").arg(f.windSpeed, 0, 'f', 1).arg(f.speedUnits), _smallTextSize);
    }
    if (full && f.homeSet && std::isfinite(f.distanceToHome)) {
        row(QStringLiteral("HOME"), f.distanceToHome > 0.0 ? f.headingToHome : qQNaN(),
            _formatDistance(f.distanceToHome, f), _smallTextSize);
    }
    if (std::isfinite(f.wpDistance)) {
        QString label = QStringLiteral("WP");
        const QString mode = f.mode.toLower();
        if (mode == QLatin1String("auto") && f.wpNumber > 0) {
            label = QStringLiteral("WP %1").arg(f.wpNumber);
        } else if (mode == QLatin1String("loiter")) {
            label = QStringLiteral("LOITER");
        } else if (mode == QLatin1String("rtl")) {
            label = QStringLiteral("RTL");
        } else if (mode == QLatin1String("guided")) {
            label = QStringLiteral("GUIDED");
        }
        if (full && std::isfinite(f.wpTimeToGo)) {
            row(QStringLiteral("TTG"), qQNaN(), formatTime(f.wpTimeToGo), _smallTextSize);
        }
        const bool arrow = std::isfinite(f.wpBearing) && f.wpDistance > 0.0;
        row(label, arrow ? f.wpBearing : qQNaN(), _formatDistance(f.wpDistance, f), _textSize);
    }
}

void HudPainter::_drawArming(QPainter& p, const HudFrame& f)
{
    if (!f.hasVehicle || (_detail == Detail::Minimal)) {
        return;
    }
    const QColor color = _symbolColor(f);
    qreal y = f.height - _margin - 0.5 * _textSize;
    _text(p, _margin, y, _textSize, f.armed ? QStringLiteral("ARMED") : QStringLiteral("DISARMED"),
          f.armed ? color : withAlpha(color, 0.8));
    y -= 0.5 * _textSize + 1.0 * _unit + 0.5 * _smallTextSize;
    if (!f.armed && f.prearmAvailable) {
        _text(p, _margin, y, _smallTextSize, f.prearmOk ? QStringLiteral("READY TO ARM") : QStringLiteral("NOT READY"),
              f.prearmOk ? color : kCaution);
        y -= 1.7 * _smallTextSize;
    }
    if (f.safetyActive) {
        _text(p, _margin, y, _smallTextSize, QStringLiteral("SAFETY ON"), kCaution);
    }
}

void HudPainter::_drawFlightMode(QPainter& p, const HudFrame& f)
{
    if (!f.hasVehicle) {
        return;
    }
    const QColor color = _symbolColor(f);
    const QString mode = f.mode.isEmpty() ? QStringLiteral("----") : f.mode.toUpper();
    const qreal size = (_detail == Detail::Minimal) ? _textSize : _largeTextSize;
    const qreal halfWidth = _textWidth(size, mode) / 2.0 + 1.2 * _unit;
    const qreal halfHeight = 0.72 * size;
    const qreal y = f.height - _margin - halfHeight;
    const qreal arm = 0.9 * _unit;

    QPainterPath plate;
    plate.addRect(QRectF(_cx - halfWidth, y - halfHeight, 2.0 * halfWidth, 2.0 * halfHeight));
    _plate(p, plate);
    QPainterPath brackets;
    for (const qreal side : {-1.0, 1.0}) {
        const qreal x = _cx + side * halfWidth;
        brackets.moveTo(x - side * arm, y - halfHeight);
        brackets.lineTo(x, y - halfHeight);
        brackets.lineTo(x, y + halfHeight);
        brackets.lineTo(x - side * arm, y + halfHeight);
    }
    _stroke(p, brackets, color);
    _text(p, _cx, y, size, mode, color, Align::Center);
}

void HudPainter::_drawAlerts(QPainter& p, const HudFrame& f)
{
    struct Callout
    {
        QString text;
        QColor color;
        bool flashing;
    };

    QList<Callout> callouts;
    if (!f.hasVehicle) {
        callouts.append({QStringLiteral("NO VEHICLE"), kSymbolLost, false});
    } else if (!f.connected) {
        callouts.append({QStringLiteral("LINK LOST"), kWarning, true});
    }
    if (f.hasVehicle && f.failsafe) {
        callouts.append({QStringLiteral("FAILSAFE"), kWarning, true});
    }
    if (f.hasVehicle && f.recentlyArmed && f.armed) {
        callouts.append({QStringLiteral("ARMED"), kCaution, false});
    }
    qreal y = _cy - 0.42 * _fieldRadius;
    for (const Callout& callout : callouts) {
        const QRectF rect =
            _callout(p, y, _largeTextSize, callout.text, callout.color, !callout.flashing || blinkOn(f));
        y -= rect.height() + 0.8 * _unit;
    }

    if (f.hasVehicle && !f.message.isEmpty()) {
        // Over the flight mode, between the tapes, shrunk to fit
        const QColor color = severityColor(f.messageSeverity, _symbolColor(f));
        const qreal maxWidth = 2.0 * (_tapeOffset - 2.0 * _unit);
        qreal size = _textSize;
        const qreal width = _textWidth(size, f.message) + 1.6 * size;
        if (width > maxWidth) {
            size = std::max(0.8 * _smallTextSize, size * maxWidth / width);
        }
        const qreal modeSize = (_detail == Detail::Minimal) ? _textSize : _largeTextSize;
        const qreal messageY = f.height - _margin - 1.44 * modeSize - 0.5 * _unit - 0.8 * size;
        const qreal messageWidth = _textWidth(size, f.message) + 1.6 * size;
        QPainterPath plate;
        plate.addRect(QRectF(_cx - messageWidth / 2.0, messageY - 0.8 * size, messageWidth, 1.6 * size));
        _plate(p, plate);
        _text(p, _cx, messageY, size, f.message, color, Align::Center);
    }
}
