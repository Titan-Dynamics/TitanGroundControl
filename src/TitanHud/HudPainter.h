#pragma once

#include <QtCore/QHash>
#include <QtCore/QPointF>
#include <QtCore/QRectF>
#include <QtCore/QString>
#include <QtCore/QtNumeric>
#include <QtGui/QBrush>
#include <QtGui/QColor>
#include <QtGui/QImage>

class QPainter;
class QPainterPath;

/// What one HUD frame shows, already in the user's units, gathered on the GUI thread so painting needs nothing else.
/// Angles are in degrees; NaN marks a value the vehicle has not reported.
struct HudFrame
{
    qreal width = 0.0;
    qreal height = 0.0;
    bool drawBackground = true;  ///< Synthetic sky and ground (instrument panel); off over video
    qint64 clockMs = 0;          ///< Drives blinking warnings

    bool hasVehicle = false;
    bool connected = false;
    bool armed = false;
    bool recentlyArmed = false;
    bool failsafe = false;
    bool safetyActive = false;
    bool prearmAvailable = false;
    bool prearmOk = false;
    bool fixedWing = false;

    double roll = 0.0;
    double pitch = 0.0;
    double heading = 0.0;
    bool flightPathValid = false;  ///< Moving fast enough for a meaningful flight path
    double flightPathAngle = 0.0;  ///< Climb angle of the velocity
    double drift = 0.0;            ///< Ground track minus heading
    double groundTrack = qQNaN();
    double turnRate = 0.0;         ///< deg/s
    double targetHeading = qQNaN();

    double airspeed = qQNaN();  ///< NaN without an airspeed sensor
    double groundspeed = 0.0;
    double speedTrend = 0.0;    ///< Change of the displayed speed over the trend time
    double targetSpeed = qQNaN();
    double speedRange = 12.0;   ///< Half the speed tape
    double verticalSpeed = 0.0;
    double aoa = qQNaN();

    double altitude = 0.0;        ///< Above home
    double altitudeTrend = 0.0;   ///< Change of altitude over the trend time
    double targetAltitude = qQNaN();
    double altitudeRange = 15.0;  ///< Half the altitude tape
    bool homeSet = false;

    double xtrack = qQNaN();  ///< Horizontal distance units, positive with the track to the right
    double distanceToHome = qQNaN();
    double headingToHome = qQNaN();
    int wpNumber = 0;
    double wpDistance = qQNaN();
    double wpBearing = qQNaN();
    double wpTimeToGo = qQNaN();     ///< s
    QString mode;
    double windDirection = qQNaN();  ///< Where the wind comes from
    double windSpeed = qQNaN();

    struct Battery
    {
        double voltage = qQNaN();
        double current = qQNaN();
        double remaining = qQNaN();  ///< %
        int chargeState = 0;         ///< MAV_BATTERY_CHARGE_STATE
    };

    Battery battery;
    Battery battery2;

    int gpsFix = -1;  ///< GPS_FIX_TYPE, -1 without a GPS
    int gpsSatellites = -1;
    int gps2Fix = -1;
    double vibration = qQNaN();    ///< Worst axis, m/s/s
    double ekfStatus = qQNaN();    ///< 0..1, worst normalised variance
    double linkQuality = qQNaN();  ///< %

    QString message;
    int messageSeverity = 6;

    QString speedUnits;
    QString altitudeUnits;
    QString distanceUnits;
};

/// Paints the Titan HUD: fighter-style symbology in one phosphor colour, with a dark halo so it reads over any video
/// and a glow over its own synthetic sky, amber and red kept for cautions and warnings. Holds a glyph cache, so a
/// painter belongs to one painting thread at a time.
class HudPainter
{
public:
    /// Adds the HUD typeface to the application fonts. Call once on the GUI thread before painting.
    static void registerFont();

    void paint(QPainter& p, const HudFrame& f);

private:
    enum class Align
    {
        Left,
        Center,
        Right
    };

    /// How much a HUD of this size shows: text keeps a readable size, so a smaller HUD shows less rather than crowd
    enum class Detail
    {
        Minimal,  ///< Picture in picture: attitude, the readouts and alerts
        Compact,  ///< Instrument panel: nominal system rows and secondary readouts left out
        Full
    };

    /// Fades symbology out toward the edge of its area
    struct Fade
    {
        enum class Kind
        {
            None,
            Radial,
            Vertical,
            Horizontal
        };

        Kind kind = Kind::None;
        QPointF center;
        qreal inner = 0.0;  ///< Opaque within this distance of the centre
        qreal outer = 0.0;  ///< Clear from this distance

        qreal alphaAt(QPointF point) const;
        QBrush brush(const QColor& color) const;
    };

    struct Tape
    {
        bool rightSide = false;
        double value = 0.0;
        double range = 1.0;  ///< Shown each side of the value
        double trend = 0.0;
        double target = qQNaN();
        bool ground = false;        ///< Hatch below zero
        bool positiveOnly = false;  ///< Nothing below zero
        QString caption;
        int digits = 3;
    };

    struct GlyphKey
    {
        char16_t character = 0;
        int pixelSize = 0;
        QRgb color = 0;
        bool glow = false;

        bool operator==(const GlyphKey&) const = default;
    };

    friend size_t qHash(const GlyphKey& key, size_t seed);

    /// A character with its halo, drawn once and blitted from then on: outlining text every frame is slow
    struct Glyph
    {
        QImage image;    ///< Device pixels; null for blanks
        QPointF origin;  ///< Of the image, from the pen position on the baseline
        qreal advance = 0;
    };

    void _layout(const HudFrame& f);
    QColor _symbolColor(const HudFrame& f) const;

    void _drawBackground(QPainter& p, const HudFrame& f) const;
    void _drawPitchLadder(QPainter& p, const HudFrame& f);
    void _drawBankScale(QPainter& p, const HudFrame& f) const;
    void _drawHeadingTape(QPainter& p, const HudFrame& f);
    void _drawCrossTrack(QPainter& p, const HudFrame& f, qreal top);
    void _drawSpeedTape(QPainter& p, const HudFrame& f);
    void _drawAltitudeTape(QPainter& p, const HudFrame& f);
    void _drawTape(QPainter& p, const HudFrame& f, const Tape& tape);
    void _drawFlightPathMarker(QPainter& p, const HudFrame& f) const;
    void _drawWaterline(QPainter& p, const HudFrame& f) const;
    void _drawSystems(QPainter& p, const HudFrame& f);
    void _drawBatteries(QPainter& p, const HudFrame& f);
    void _drawNavigation(QPainter& p, const HudFrame& f);
    void _drawArming(QPainter& p, const HudFrame& f);
    void _drawFlightMode(QPainter& p, const HudFrame& f);
    void _drawAlerts(QPainter& p, const HudFrame& f);

    /// Strokes a path over its halo, or its glow on the synthetic background
    void _stroke(QPainter& p, const QPainterPath& path, const QColor& color, qreal width = 0.0) const;
    void _stroke(QPainter& p, const QPainterPath& path, const QColor& color, qreal width, const Fade& fade) const;
    void _fill(QPainter& p, const QPainterPath& path, const QColor& color) const;
    void _plate(QPainter& p, const QPainterPath& path) const;
    void _arrow(QPainter& p, QPointF center, qreal size, double angle, const QColor& color) const;
    QRectF _callout(QPainter& p, qreal centerY, qreal size, const QString& text, const QColor& color, bool framed);
    void _rollingNumber(QPainter& p, const QRectF& box, double value, qreal size, const QColor& color);

    /// Text vertically centred on y, from cached glyphs; returns its width
    qreal _text(QPainter& p, qreal x, qreal y, qreal size, const QString& text, const QColor& color,
                Align align = Align::Left);
    qreal _textWidth(qreal size, const QString& text);
    const Glyph& _glyph(QChar character, int pixelSize, const QColor& color, qreal devicePixelRatio);

    QString _formatDistance(double distance, const HudFrame& f) const;

    // Layout, recomputed each frame from the item's size
    Detail _detail = Detail::Full;
    qreal _cx = 0.0;
    qreal _cy = 0.0;
    qreal _unit = 1.0;  ///< 1% of the HUD's height, or of its 4:3 width when narrower
    qreal _lineWidth = 1.5;
    qreal _haloWidth = 1.0;
    qreal _textSize = 9.0;
    qreal _smallTextSize = 8.0;
    qreal _largeTextSize = 11.0;
    qreal _pxPerDegree = 4.0;   ///< Pitch ladder and flight path scale
    qreal _tapeOffset = 100.0;  ///< From the centre to each tape's spine
    qreal _tapeHalfHeight = 60.0;
    qreal _fieldRadius = 80.0;  ///< Where the pitch ladder fades out, and the bank scale's radius
    qreal _margin = 3.0;
    bool _glow = false;
    qreal _devicePixelRatio = 1.0;

    QHash<GlyphKey, Glyph> _glyphs;
    qreal _glyphsDevicePixelRatio = 0.0;
};
