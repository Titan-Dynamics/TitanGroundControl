#include "LinkStats.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

constexpr int kReceivedSeconds = 3;  ///< Received bytes, packets and quality average this many seconds
constexpr int kSentSeconds = 5;      ///< Sent bytes average this many
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

}  // namespace

LinkStats::LinkStats()
{
    reset();
}

void LinkStats::addReceivedBytes(qint64 count)
{
    _bytesReceived += count;
    _current.receivedBytes += count;
}

void LinkStats::addSentBytes(qint64 count)
{
    _bytesSent += count;
    _current.sentBytes += count;
}

void LinkStats::addPacket(int sysid, int compid, int seq, double timeMs)
{
    ++_packetsReceived;
    ++_current.packets;

    if (!std::isnan(_lastPacketMs)) {
        const double interval = timeMs - _lastPacketMs;
        if (std::isnan(_maxPacketIntervalMs) || (interval > _maxPacketIntervalMs)) {
            _maxPacketIntervalMs = interval;
        }
    }
    _lastPacketMs = timeMs;

    // Lost packets are the gap in this sender's sequence numbers, as MAVLinkProtocol counts them
    const int key = (sysid << 8) | compid;
    const auto last = _lastSequence.constFind(key);
    if (last != _lastSequence.cend()) {
        if (seq == last.value()) {
            return;  // The same packet again: MAVLink 1 and 2 copies of a message share its sequence number
        }
        const int lost = (seq - last.value() - 1 + 256) % 256;
        _packetsLost += lost;
        _current.lost += lost;
    }
    _lastSequence.insert(key, seq);
}

void LinkStats::tick(double timeSeconds)
{
    _seconds.append(_current);
    while (_seconds.size() > std::max(kReceivedSeconds, kSentSeconds)) {
        _seconds.removeFirst();
    }

    const qsizetype receivedCount = std::min<qsizetype>(kReceivedSeconds, _seconds.size());
    qint64 receivedBytes = 0;
    qint64 packets = 0;
    qint64 lost = 0;
    for (qsizetype i = _seconds.size() - receivedCount; i < _seconds.size(); ++i) {
        receivedBytes += _seconds[i].receivedBytes;
        packets += _seconds[i].packets;
        lost += _seconds[i].lost;
    }
    _bytesReceivedPerSecond = static_cast<double>(receivedBytes) / receivedCount;
    _packetsPerSecond = static_cast<double>(packets) / receivedCount;
    _quality = (packets + lost) > 0 ? static_cast<double>(packets) / static_cast<double>(packets + lost) : kNaN;

    const qsizetype sentCount = std::min<qsizetype>(kSentSeconds, _seconds.size());
    qint64 sentBytes = 0;
    for (qsizetype i = _seconds.size() - sentCount; i < _seconds.size(); ++i) {
        sentBytes += _seconds[i].sentBytes;
    }
    _bytesSentPerSecond = static_cast<double>(sentBytes) / sentCount;

    _receivedHistory.append(QPointF(timeSeconds, static_cast<double>(_current.receivedBytes)));
    _sentHistory.append(QPointF(timeSeconds, static_cast<double>(_current.sentBytes)));
    while (_receivedHistory.size() > kChartSeconds) {
        _receivedHistory.removeFirst();
        _sentHistory.removeFirst();
    }

    _current = Second();
}

void LinkStats::reset()
{
    _bytesReceived = 0;
    _bytesSent = 0;
    _packetsReceived = 0;
    _packetsLost = 0;
    _bytesReceivedPerSecond = 0;
    _bytesSentPerSecond = 0;
    _packetsPerSecond = 0;
    _quality = kNaN;
    _maxPacketIntervalMs = kNaN;
    _lastPacketMs = kNaN;
    _current = Second();
    _seconds.clear();
    _lastSequence.clear();
    _receivedHistory.clear();
    _sentHistory.clear();
}

LinkStats::Summary LinkStats::summarize(const QList<QPointF>& history)
{
    Summary summary;
    if (history.isEmpty()) {
        return summary;
    }

    summary.min = history.first().y();
    summary.max = summary.min;
    double total = 0;
    for (const QPointF& point : history) {
        summary.min = std::min(summary.min, point.y());
        summary.max = std::max(summary.max, point.y());
        total += point.y();
    }
    summary.mean = total / static_cast<double>(history.size());
    return summary;
}
