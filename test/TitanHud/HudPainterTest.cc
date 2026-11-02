#include "HudPainterTest.h"

#include <limits>

#include <QtCore/QRegularExpression>
#include <QtCore/QtMath>
#include <QtGui/QColor>
#include <QtGui/QImage>
#include <QtGui/QPainter>

#include "HudPainter.h"

namespace {

HudFrame cruiseFrame(QSize size, bool background)
{
    HudFrame f;
    f.width = size.width();
    f.height = size.height();
    f.drawBackground = background;
    f.hasVehicle = true;
    f.connected = true;
    f.armed = true;
    f.fixedWing = true;
    f.roll = 18.0;
    f.pitch = 4.0;
    f.heading = 47.0;
    f.flightPathValid = true;
    f.flightPathAngle = 2.7;
    f.drift = -4.0;
    f.groundTrack = 43.0;
    f.turnRate = 3.2;
    f.targetHeading = 62.0;
    f.airspeed = 22.6;
    f.groundspeed = 25.3;
    f.speedTrend = 2.4;
    f.targetSpeed = 22.0;
    f.verticalSpeed = 1.2;
    f.aoa = 4.2;
    f.altitude = 120.4;
    f.altitudeTrend = 7.2;
    f.targetAltitude = 150.0;
    f.homeSet = true;
    f.xtrack = 8.3;
    f.distanceToHome = 2400.0;
    f.headingToHome = 210.0;
    f.wpNumber = 3;
    f.wpDistance = 1240.0;
    f.wpBearing = 65.0;
    f.wpTimeToGo = 49.0;
    f.mode = QStringLiteral("Auto");
    f.windDirection = 270.0;
    f.windSpeed = 5.2;
    f.battery = {15.8, 12.3, 67.0, 1};
    f.battery2 = {12.2, 4.1, 30.0, 2};
    f.gpsFix = 3;
    f.gpsSatellites = 14;
    f.gps2Fix = 2;
    f.vibration = 12.0;
    f.ekfStatus = 0.12;
    f.linkQuality = 98.0;
    f.message = QStringLiteral("PreArm: Battery 1 below minimum arming voltage");
    f.messageSeverity = 4;
    f.northMeters = 812.0;
    f.eastMeters = -431.0;
    f.heightMeters = 120.4;
    f.speedUnits = QStringLiteral("m/s");
    f.altitudeUnits = QStringLiteral("m");
    f.distanceUnits = QStringLiteral("m");
    return f;
}

QImage paint(HudPainter& painter, const HudFrame& f)
{
    QImage image(QSize(qMax(1, qCeil(f.width)), qMax(1, qCeil(f.height))), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter p(&image);
    painter.paint(p, f);
    p.end();
    return image;
}

/// Share of the image's pixels with anything painted on them
double coverage(const QImage& image)
{
    qsizetype covered = 0;
    for (int y = 0; y < image.height(); y++) {
        const auto* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); x++) {
            if (qAlpha(line[x]) > 0) {
                covered++;
            }
        }
    }
    return static_cast<double>(covered) / (static_cast<double>(image.width()) * image.height());
}

}  // namespace

void HudPainterTest::init()
{
    UnitTest::init();
    // Whichever test first touches the font database starts it, and on the offscreen platform it looks for a font
    // directory that a Windows Qt install does not have
    ignoreLogMessage("default", QtWarningMsg,
                     QRegularExpression(QStringLiteral("^QFontDatabase: Cannot find font directory")));
}

void HudPainterTest::_overlayLeavesVideoVisible()
{
    HudPainter::registerFont();
    HudPainter painter;
    for (const QSize size : {QSize(320, 180), QSize(1280, 720), QSize(1920, 1080)}) {
        const double covered = coverage(paint(painter, cruiseFrame(size, false)));
        QVERIFY2((covered > 0.01) && (covered < 0.3),
                 qPrintable(QStringLiteral("%1 covered at %2x%3").arg(covered).arg(size.width()).arg(size.height())));
    }
}

void HudPainterTest::_instrumentFillsItsPanel()
{
    HudPainter::registerFont();
    HudPainter painter;
    QCOMPARE(coverage(paint(painter, cruiseFrame(QSize(320, 240), true))), 1.0);
}

void HudPainterTest::_symbolScaleSizesTheSymbology()
{
    HudPainter::registerFont();
    HudPainter painter;
    double previous = 0.0;
    for (const double scale : {0.8, 1.0, 1.25}) {
        HudFrame f = cruiseFrame(QSize(1280, 720), false);
        f.symbolScale = scale;
        const double covered = coverage(paint(painter, f));
        QVERIFY2(covered > previous, qPrintable(QStringLiteral("%1 covered at scale %2").arg(covered).arg(scale)));
        previous = covered;
    }
}

void HudPainterTest::_symbolColorTintsTheSymbology()
{
    HudPainter::registerFont();
    HudPainter painter;
    auto bluePixels = [](const QImage& image) {
        qsizetype count = 0;
        for (int y = 0; y < image.height(); y++) {
            const auto* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
            for (int x = 0; x < image.width(); x++) {
                count += (qBlue(line[x]) > 200) && (qRed(line[x]) < 100) && (qGreen(line[x]) < 140) ? 1 : 0;
            }
        }
        return count;
    };

    HudFrame f = cruiseFrame(QSize(1280, 720), false);
    QVERIFY(bluePixels(paint(painter, f)) < 50);
    f.symbolColor = QColor(0x20, 0x60, 0xFF);
    QVERIFY(bluePixels(paint(painter, f)) > 1000);
}

void HudPainterTest::_paintsWithoutVehicle()
{
    HudPainter::registerFont();
    HudPainter painter;
    HudFrame f;
    f.width = 640.0;
    f.height = 360.0;
    f.drawBackground = false;
    QVERIFY(coverage(paint(painter, f)) > 0.0);
}

void HudPainterTest::_survivesMissingAndExtremeValues()
{
    HudPainter::registerFont();
    HudPainter painter;
    const double nan = qQNaN();
    const double inf = std::numeric_limits<double>::infinity();

    for (const bool background : {false, true}) {
        HudFrame f = cruiseFrame(QSize(640, 480), background);
        f.roll = nan;
        f.pitch = inf;
        f.heading = -inf;
        f.drift = nan;
        f.flightPathAngle = inf;
        f.turnRate = nan;
        f.targetHeading = inf;
        f.airspeed = inf;
        f.groundspeed = nan;
        f.speedTrend = -inf;
        f.targetSpeed = 1e300;
        f.verticalSpeed = nan;
        f.aoa = inf;
        f.altitude = 1e300;
        f.altitudeTrend = nan;
        f.targetAltitude = -1e300;
        f.speedRange = 0.0;
        f.altitudeRange = inf;
        f.xtrack = -inf;
        f.distanceToHome = 1e300;
        f.headingToHome = nan;
        f.wpDistance = inf;
        f.wpBearing = nan;
        f.wpTimeToGo = inf;
        f.windDirection = inf;
        f.windSpeed = 1e300;
        f.battery = {inf, -inf, nan, 99};
        f.vibration = 1e300;
        f.ekfStatus = inf;
        f.linkQuality = -1e300;
        f.northMeters = 1e300;
        f.eastMeters = nan;
        f.heightMeters = -inf;
        f.symbolColor = QColor();
        f.message = QString(500, QLatin1Char('W'));
        QVERIFY(coverage(paint(painter, f)) > 0.0);
    }

    // No size, nothing to paint
    QCOMPARE(coverage(paint(painter, cruiseFrame(QSize(0, 0), false))), 0.0);
    HudFrame unsized = cruiseFrame(QSize(64, 48), false);
    unsized.width = inf;
    QImage image(64, 48, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter p(&image);
        painter.paint(p, unsized);
    }
    QCOMPARE(coverage(image), 0.0);
}

void HudPainterTest::_paintsEveryAttitudeAndSize()
{
    HudPainter::registerFont();
    HudPainter painter;
    for (const double roll : {-180.0, -95.0, -45.0, 0.0, 60.0, 135.0, 900.0}) {
        for (const double pitch : {-90.0, -60.0, -12.0, 0.0, 33.0, 90.0}) {
            HudFrame f = cruiseFrame(QSize(480, 360), true);
            f.roll = roll;
            f.pitch = pitch;
            f.heading = roll + pitch;
            f.drift = roll;
            f.altitude = -pitch;
            QVERIFY(coverage(paint(painter, f)) > 0.0);
        }
    }
    for (const QSize size : {QSize(1, 1), QSize(40, 30), QSize(2000, 50), QSize(50, 2000), QSize(3840, 2160)}) {
        for (const bool background : {false, true}) {
            QVERIFY(coverage(paint(painter, cruiseFrame(size, background))) > 0.0);
        }
    }
}

UT_REGISTER_TEST(HudPainterTest, TestLabel::Unit)
