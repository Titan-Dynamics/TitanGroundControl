#include "LinkStatsTest.h"

#include <cmath>
#include <memory>

#include <QtGraphs/QLineSeries>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlEngine>
#include <QtQuick/QQuickItem>

#include "LinkStats.h"
#include "LinkStatsController.h"
#include "QGCPalette.h"

void LinkStatsTest::_ratesAverageWholeSeconds()
{
    LinkStats stats;
    const int received[] = {1000, 2000, 3000, 4000};
    for (int i = 0; i < 4; ++i) {
        stats.addReceivedBytes(received[i]);
        stats.addSentBytes(100 * (i + 1));
        for (int packet = 0; packet < 10 * (i + 1); ++packet) {
            stats.addPacket(1, 1, (i * 40 + packet) % 256, i * 1000.0 + packet);
        }
        stats.tick(i + 1);
    }

    // Received bytes and packets over the last 3 seconds; sent bytes over the last 5, all 4 so far
    QCOMPARE(stats.bytesReceivedPerSecond(), 3000.0);
    QCOMPARE(stats.packetsPerSecond(), 30.0);
    QCOMPARE(stats.bytesSentPerSecond(), 250.0);
    QCOMPARE(stats.bytesReceived(), static_cast<qint64>(10000));
    QCOMPARE(stats.bytesSent(), static_cast<qint64>(1000));
    QCOMPARE(stats.packetsReceived(), static_cast<qint64>(100));
}

void LinkStatsTest::_lostPacketsFollowSequenceGaps()
{
    LinkStats stats;
    double time = 0;
    for (const int seq : {0, 1, 2, 5}) {  // 3 and 4 lost
        stats.addPacket(1, 1, seq, time++);
    }
    QCOMPARE(stats.packetsLost(), static_cast<qint64>(2));

    for (const int seq : {254, 255, 1}) {  // 6 to 253 lost, then 0 across the wrap
        stats.addPacket(1, 1, seq, time++);
    }
    QCOMPARE(stats.packetsLost(), static_cast<qint64>(2 + 248 + 1));

    // The same packet again is not a loss, and each sender keeps its own sequence
    stats.addPacket(1, 1, 1, time++);
    stats.addPacket(1, 2, 200, time++);
    stats.addPacket(1, 2, 201, time++);
    QCOMPARE(stats.packetsLost(), static_cast<qint64>(251));
    QCOMPARE(stats.packetsReceived(), static_cast<qint64>(10));
}

void LinkStatsTest::_qualityCoversTheLastThreeSeconds()
{
    LinkStats stats;
    QVERIFY(std::isnan(stats.quality()));

    // A second with a lost packet in five, then three clean seconds push it out of the window
    for (const int seq : {0, 1, 3, 4}) {
        stats.addPacket(1, 1, seq, seq);
    }
    stats.tick(1);
    QCOMPARE(stats.quality(), 0.8);

    int seq = 5;
    for (int second = 2; second <= 4; ++second) {
        for (int packet = 0; packet < 4; ++packet) {
            stats.addPacket(1, 1, seq++, second * 1000.0 + packet);
        }
        stats.tick(second);
    }
    QCOMPARE(stats.quality(), 1.0);

    // Seconds without packets have no quality
    for (int second = 5; second <= 7; ++second) {
        stats.tick(second);
    }
    QVERIFY(std::isnan(stats.quality()));
}

void LinkStatsTest::_maxPacketInterval()
{
    LinkStats stats;
    stats.addPacket(1, 1, 0, 1000);
    QVERIFY(std::isnan(stats.maxPacketIntervalMs()));

    stats.addPacket(1, 1, 1, 1100);
    stats.addPacket(1, 1, 2, 1350);
    stats.addPacket(1, 1, 3, 1400);
    QCOMPARE(stats.maxPacketIntervalMs(), 250.0);
}

void LinkStatsTest::_chartKeepsTheLastMinute()
{
    LinkStats stats;
    for (int second = 1; second <= 70; ++second) {
        stats.addReceivedBytes(second * 10);
        stats.addSentBytes(5);
        stats.tick(second);
    }

    QCOMPARE(stats.receivedHistory().size(), static_cast<qsizetype>(LinkStats::kChartSeconds));
    QCOMPARE(stats.receivedHistory().first(), QPointF(11, 110));
    QCOMPARE(stats.receivedHistory().last(), QPointF(70, 700));

    const LinkStats::Summary received = LinkStats::summarize(stats.receivedHistory());
    QCOMPARE(received.min, 110.0);
    QCOMPARE(received.max, 700.0);
    QCOMPARE(received.mean, 405.0);

    const LinkStats::Summary sent = LinkStats::summarize(stats.sentHistory());
    QCOMPARE(sent.min, 5.0);
    QCOMPARE(sent.max, 5.0);
    QCOMPARE(sent.mean, 5.0);
}

void LinkStatsTest::_resetClearsEverything()
{
    LinkStats stats;
    stats.addReceivedBytes(100);
    stats.addSentBytes(50);
    stats.addPacket(1, 1, 0, 0);
    stats.addPacket(1, 1, 3, 500);
    stats.tick(1);

    stats.reset();
    QCOMPARE(stats.bytesReceived(), static_cast<qint64>(0));
    QCOMPARE(stats.bytesSent(), static_cast<qint64>(0));
    QCOMPARE(stats.packetsReceived(), static_cast<qint64>(0));
    QCOMPARE(stats.packetsLost(), static_cast<qint64>(0));
    QCOMPARE(stats.bytesReceivedPerSecond(), 0.0);
    QVERIFY(std::isnan(stats.quality()));
    QVERIFY(std::isnan(stats.maxPacketIntervalMs()));
    QVERIFY(stats.receivedHistory().isEmpty());

    // A sender's sequence starts afresh: no loss counted against the last one before the reset
    stats.addPacket(1, 1, 200, 1000);
    QCOMPARE(stats.packetsLost(), static_cast<qint64>(0));
}

void LinkStatsTest::_pageLoads()
{
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qml"));
    // AnalyzePage takes the palette from the Analyze view around it
    QGCPalette palette;
    engine.rootContext()->setContextProperty(QStringLiteral("qgcPal"), &palette);
    QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qml/QGroundControl/TitanLinkStats/LinkStatsPage.qml")));
    QTRY_VERIFY_WITH_TIMEOUT(!component.isLoading(), TestTimeout::mediumMs());
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));

    // Inside a sized item, as the Analyze view shows it
    QQuickItem view;
    view.setSize(QSizeF(1200, 800));
    std::unique_ptr<QObject> page(
        component.createWithInitialProperties({{QStringLiteral("parent"), QVariant::fromValue(&view)}}));
    QVERIFY2(page, qPrintable(component.errorString()));

    // The page's content: its controller, with both chart series handed to it
    QVERIFY(page->findChild<LinkStatsController*>());
    QCOMPARE(page->findChildren<QLineSeries*>().size(), static_cast<qsizetype>(2));
}

UT_REGISTER_TEST(LinkStatsTest, TestLabel::Unit)
