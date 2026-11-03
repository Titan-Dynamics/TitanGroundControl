#pragma once

#include <QtCore/QHash>
#include <QtCore/QList>
#include <QtCore/QPointF>

/// Titan: a link's statistics as TitanPlanner's Link Stats works them out (Controls/ConnectionStats.cs), from the
/// bytes and MAVLink packets the link carries, in one-second steps. Received bytes, packets and quality cover the last
/// 3 s and sent bytes the last 5 s; lost packets are the gaps in each sender's sequence numbers. The chart keeps each
/// second's received and sent bytes for the last minute.
class LinkStats
{
public:
    static constexpr int kChartSeconds = 60;

    struct Summary
    {
        double min = 0;
        double max = 0;
        double mean = 0;
    };

    LinkStats();

    void addReceivedBytes(qint64 count);
    void addSentBytes(qint64 count);
    /// A decoded packet from sysid/compid with sequence number seq, received at timeMs on a monotonic clock
    void addPacket(int sysid, int compid, int seq, double timeMs);
    /// Closes the current second, timeSeconds after the start: updates the rates and adds it to the chart
    void tick(double timeSeconds);
    void reset();

    qint64 bytesReceived() const { return _bytesReceived; }

    qint64 bytesSent() const { return _bytesSent; }

    qint64 packetsReceived() const { return _packetsReceived; }

    qint64 packetsLost() const { return _packetsLost; }

    double bytesReceivedPerSecond() const { return _bytesReceivedPerSecond; }

    double bytesSentPerSecond() const { return _bytesSentPerSecond; }

    double packetsPerSecond() const { return _packetsPerSecond; }

    /// Packets received over received and lost, 0 to 1; NaN while no packets arrive
    double quality() const { return _quality; }

    /// The longest gap between two packets, in ms; NaN before the second packet
    double maxPacketIntervalMs() const { return _maxPacketIntervalMs; }

    /// Each second as (seconds since the start, bytes in that second)
    const QList<QPointF>& receivedHistory() const { return _receivedHistory; }

    const QList<QPointF>& sentHistory() const { return _sentHistory; }

    static Summary summarize(const QList<QPointF>& history);

private:
    struct Second
    {
        qint64 receivedBytes = 0;
        qint64 sentBytes = 0;
        qint64 packets = 0;
        qint64 lost = 0;
    };

    qint64 _bytesReceived = 0;
    qint64 _bytesSent = 0;
    qint64 _packetsReceived = 0;
    qint64 _packetsLost = 0;
    double _bytesReceivedPerSecond = 0;
    double _bytesSentPerSecond = 0;
    double _packetsPerSecond = 0;
    double _quality = 0;
    double _maxPacketIntervalMs = 0;
    double _lastPacketMs = 0;

    Second _current;
    QList<Second> _seconds;         ///< The last few closed seconds, newest last
    QHash<int, int> _lastSequence;  ///< By (sysid << 8) | compid
    QList<QPointF> _receivedHistory;
    QList<QPointF> _sentHistory;
};
