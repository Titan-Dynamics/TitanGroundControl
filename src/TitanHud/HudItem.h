#pragma once

#include <QtCore/QPointer>
#include <QtGui/QColor>
#include <QtGui/QFont>
#include <QtQuick/QQuickPaintedItem>

#include "HudData.h"

class QPainter;

/// Mission Planner's HUD, drawn with QPainter: artificial horizon, pitch ladder and roll scale, heading,
/// speed and altitude tapes with target bugs, cross-track and turn rate, battery, GPS/EKF/vibration
/// badges, status texts and the flight mode. With drawBackground off (video and 3D map overlays) the
/// sky, ground and horizon line are skipped and everything else is drawn over the host.
class HudItem : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(HudData* hudData READ hudData WRITE setHudData NOTIFY hudDataChanged)
    Q_PROPERTY(bool drawBackground READ drawBackground WRITE setDrawBackground NOTIFY drawBackgroundChanged)
    Q_PROPERTY(bool showHeading MEMBER _showHeading NOTIFY optionsChanged)
    Q_PROPERTY(bool showSpeed MEMBER _showSpeed NOTIFY optionsChanged)
    Q_PROPERTY(bool showAltitude MEMBER _showAltitude NOTIFY optionsChanged)
    Q_PROPERTY(bool showRollPitch MEMBER _showRollPitch NOTIFY optionsChanged)
    Q_PROPERTY(bool showXtrack MEMBER _showXtrack NOTIFY optionsChanged)
    Q_PROPERTY(bool showBattery MEMBER _showBattery NOTIFY optionsChanged)
    Q_PROPERTY(bool showGps MEMBER _showGps NOTIFY optionsChanged)
    Q_PROPERTY(bool showLink MEMBER _showLink NOTIFY optionsChanged)
    Q_PROPERTY(bool showWaypoint MEMBER _showWaypoint NOTIFY optionsChanged)
    Q_PROPERTY(bool showWind MEMBER _showWind NOTIFY optionsChanged)

public:
    explicit HudItem(QQuickItem* parent = nullptr);

    HudData* hudData() const { return _data; }

    void setHudData(HudData* data);

    bool drawBackground() const { return _drawBackground; }

    void setDrawBackground(bool draw);

    void paint(QPainter* painter) override;

signals:
    void hudDataChanged();
    void drawBackgroundChanged();
    void optionsChanged();

private:
    /// Vehicle values converted to the user's units once per frame
    struct Frame
    {
        bool connected = false;
        bool armed = false;
        bool fixedWing = false;
        double roll = 0.0;
        double pitch = 0.0;
        double heading = 0.0;
        double groundCourse = 0.0;
        double airspeed = 0.0;
        double groundspeed = 0.0;
        double altitude = 0.0;
        double verticalSpeed = 0.0;
        double airspeedAccel = 0.0;
        double targetHeading = qQNaN();
        double targetAltitude = qQNaN();
        double targetSpeed = qQNaN();
        double xtrack = 0.0;
        double turnRate = 0.0;
        double distanceToHome = 0.0;
        double headingToHome = 0.0;
        bool homeSet = false;
        double wpDistanceMeters = qQNaN();
        double wpBearing = qQNaN();
        int wpNumber = 0;
        QString mode;
        double windDirection = qQNaN();
        double windSpeed = qQNaN();
        double batteryVoltage = qQNaN();
        double batteryCurrent = qQNaN();
        double batteryRemaining = qQNaN();
        int batteryChargeState = 0;
        double battery2Voltage = qQNaN();
        double battery2Current = qQNaN();
        double battery2Remaining = qQNaN();
        int gpsFix = -1;
        int gps2Fix = -1;
        double vibeMax = qQNaN();
        double ekfStatus = qQNaN();
        double linkQuality = qQNaN();
        bool failsafe = false;
        bool safetyActive = false;
        bool recentlyArmed = false;
        bool prearmAvailable = false;
        bool prearmOk = false;
        QString message;
        int messageSeverity = 6;
        double aoa = qQNaN();
        double ssa = qQNaN();
        QString speedUnits;
        QString altitudeUnits;
        QString distanceUnits;
    };

    Frame _collectFrame() const;
    void _connectVehicle(Vehicle* vehicle);

    void _drawHorizon(QPainter& p, const Frame& f) const;
    void _drawPitchLadder(QPainter& p, const Frame& f) const;
    void _drawFlightPathVector(QPainter& p, const Frame& f) const;
    void _drawHeadingTape(QPainter& p, const Frame& f) const;
    void _drawXtrackAndTurnRate(QPainter& p, const Frame& f) const;
    void _drawSpeedTape(QPainter& p, const Frame& f) const;
    void _drawAltitudeTape(QPainter& p, const Frame& f) const;
    void _drawLinkQuality(QPainter& p, const Frame& f) const;
    void _drawAoaGauge(QPainter& p, const Frame& f) const;
    void _drawBattery(QPainter& p, const Frame& f) const;
    void _drawBadges(QPainter& p, const Frame& f) const;
    void _drawCenterMessages(QPainter& p, const Frame& f) const;
    void _drawFlightMode(QPainter& p, const Frame& f) const;

    void _drawText(QPainter& p, qreal x, qreal y, qreal size, const QString& text, const QColor& color,
                   bool centered = false) const;
    qreal _textWidth(qreal size, const QString& text) const;
    void _drawArrowReadout(QPainter& p, qreal x, qreal y, qreal size, const QString& label, double direction,
                           bool showArrow, const QString& value, const Frame& f) const;
    void _drawTapePointer(QPainter& p, qreal tapeWidth, qreal fs, bool rightSide, bool extended) const;
    void _drawBadge(QPainter& p, const QRectF& rect, const QColor& fill, const QString& text) const;
    void _drawBatteryIcon(QPainter& p, const QRectF& rect, int bars, const QColor& barColor) const;

    QPointer<HudData> _data;
    QPointer<Vehicle> _connectedVehicle;
    bool _drawBackground = true;
    bool _showHeading = true;
    bool _showSpeed = true;
    bool _showAltitude = true;
    bool _showRollPitch = true;
    bool _showXtrack = true;
    bool _showBattery = true;
    bool _showGps = true;
    bool _showLink = true;
    bool _showWaypoint = true;
    bool _showWind = true;

    QColor _hudColor{0xF8, 0xF8, 0xF8};
    QColor _skyTop{0x4A, 0x90, 0xC9};
    QColor _skyBottom{0x9D, 0xC4, 0xDB};
    QColor _groundTop{0x8B, 0xA5, 0x1E};
    QColor _groundBottom{0x36, 0x42, 0x05};
    QColor _targetHeadingColor{0xFF, 0x91, 0x1B};
    QString _fontFamily{QStringLiteral("Arial")};
};
