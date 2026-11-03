#pragma once

#include <QtCore/QElapsedTimer>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QString>
#include <QtCore/QTimer>
#include <QtQmlIntegration/QtQmlIntegration>

#include "LinkStats.h"

class LinkInterface;
class QAbstractSeries;

/// Titan: the Analyze view's Link Stats page (LinkStatsPage.qml), TitanPlanner's Link Stats: the traffic on the
/// active vehicle's primary link, or on the first link while no vehicle is connected, updated every second. Its chart
/// shows the last 30 s of received and sent bytes per second, scrolling as Mission Planner's does.
class LinkStatsController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_MOC_INCLUDE("QtGraphs/qabstractseries.h")

    Q_PROPERTY(bool hasLink READ hasLink NOTIFY linkChanged)
    Q_PROPERTY(QString linkName READ linkName NOTIFY linkChanged)
    Q_PROPERTY(qint64 bytesReceived READ bytesReceived NOTIFY statsChanged)
    Q_PROPERTY(qint64 bytesSent READ bytesSent NOTIFY statsChanged)
    Q_PROPERTY(qint64 packetsReceived READ packetsReceived NOTIFY statsChanged)
    Q_PROPERTY(qint64 packetsLost READ packetsLost NOTIFY statsChanged)
    Q_PROPERTY(double bytesReceivedPerSecond READ bytesReceivedPerSecond NOTIFY statsChanged)
    Q_PROPERTY(double bytesSentPerSecond READ bytesSentPerSecond NOTIFY statsChanged)
    Q_PROPERTY(double packetsPerSecond READ packetsPerSecond NOTIFY statsChanged)
    Q_PROPERTY(double quality READ quality NOTIFY statsChanged)
    Q_PROPERTY(double maxPacketIntervalMs READ maxPacketIntervalMs NOTIFY statsChanged)
    Q_PROPERTY(double receivedMin READ receivedMin NOTIFY statsChanged)
    Q_PROPERTY(double receivedMax READ receivedMax NOTIFY statsChanged)
    Q_PROPERTY(double receivedMean READ receivedMean NOTIFY statsChanged)
    Q_PROPERTY(double sentMin READ sentMin NOTIFY statsChanged)
    Q_PROPERTY(double sentMax READ sentMax NOTIFY statsChanged)
    Q_PROPERTY(double sentMean READ sentMean NOTIFY statsChanged)
    Q_PROPERTY(double chartTimeMin READ chartTimeMin NOTIFY statsChanged)
    Q_PROPERTY(double chartTimeMax READ chartTimeMax NOTIFY statsChanged)
    Q_PROPERTY(double chartRateMax READ chartRateMax NOTIFY statsChanged)

public:
    explicit LinkStatsController(QObject* parent = nullptr);

    /// The chart's received and sent bytes per second, LineSeries from QML
    Q_INVOKABLE void setSeries(QAbstractSeries* received, QAbstractSeries* sent);
    Q_INVOKABLE void reset();

    bool hasLink() const { return !_link.isNull(); }

    QString linkName() const { return _linkName; }

    qint64 bytesReceived() const { return _stats.bytesReceived(); }

    qint64 bytesSent() const { return _stats.bytesSent(); }

    qint64 packetsReceived() const { return _stats.packetsReceived(); }

    qint64 packetsLost() const { return _stats.packetsLost(); }

    double bytesReceivedPerSecond() const { return _stats.bytesReceivedPerSecond(); }

    double bytesSentPerSecond() const { return _stats.bytesSentPerSecond(); }

    double packetsPerSecond() const { return _stats.packetsPerSecond(); }

    double quality() const { return _stats.quality(); }

    double maxPacketIntervalMs() const { return _stats.maxPacketIntervalMs(); }

    double receivedMin() const { return _received.min; }

    double receivedMax() const { return _received.max; }

    double receivedMean() const { return _received.mean; }

    double sentMin() const { return _sent.min; }

    double sentMax() const { return _sent.max; }

    double sentMean() const { return _sent.mean; }

    double chartTimeMin() const { return _chartTimeMax - kChartWindowSeconds; }

    double chartTimeMax() const { return _chartTimeMax; }

    double chartRateMax() const { return _chartRateMax; }

signals:
    void linkChanged();
    void statsChanged();

private:
    static constexpr double kChartWindowSeconds = 30;

    void _tick();
    /// Follows the active vehicle's primary link; true when it changed, which resets the statistics
    bool _updateLink();
    void _updateChart();

    LinkStats _stats;
    LinkStats::Summary _received;
    LinkStats::Summary _sent;
    QPointer<LinkInterface> _link;
    QString _linkName;
    QTimer _timer;
    QElapsedTimer _clock;
    QPointer<QAbstractSeries> _receivedSeries;
    QPointer<QAbstractSeries> _sentSeries;
    double _chartTimeMax = kChartWindowSeconds;
    double _chartRateMax = 0;
};
