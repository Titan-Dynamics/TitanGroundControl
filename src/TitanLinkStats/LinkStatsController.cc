#include "LinkStatsController.h"

#include <algorithm>
#include <cmath>

#include <QtGraphs/QLineSeries>

#include "LinkConfiguration.h"
#include "LinkInterface.h"
#include "LinkManager.h"
#include "MAVLinkProtocol.h"
#include "MultiVehicleManager.h"
#include "Vehicle.h"
#include "VehicleLinkManager.h"

namespace {

/// The chart's Y axis top: the next 1, 2 or 5 step above the value
double niceCeiling(double value)
{
    if (!(value > 0)) {
        return 10;
    }
    const double magnitude = std::pow(10.0, std::floor(std::log10(value)));
    for (const double step : {1.0, 2.0, 5.0}) {
        if (value <= step * magnitude) {
            return step * magnitude;
        }
    }
    return 10 * magnitude;
}

}  // namespace

LinkStatsController::LinkStatsController(QObject* parent)
    : QObject(parent)
{
    (void) connect(MAVLinkProtocol::instance(), &MAVLinkProtocol::messageReceived, this,
                   [this](LinkInterface* link, const mavlink_message_t& message) {
                       if (link && (link == _link)) {
                           _stats.addPacket(message.sysid, message.compid, message.seq,
                                            static_cast<double>(_clock.nsecsElapsed()) / 1e6);
                       }
                   });

    _timer.setInterval(1000);
    (void) connect(&_timer, &QTimer::timeout, this, &LinkStatsController::_tick);

    // A new link resets the statistics, which starts the clock and the timer
    if (!_updateLink()) {
        reset();
    }
}

void LinkStatsController::setSeries(QAbstractSeries* received, QAbstractSeries* sent)
{
    _receivedSeries = received;
    _sentSeries = sent;
    _updateChart();
}

void LinkStatsController::reset()
{
    _stats.reset();
    _clock.restart();
    _timer.start();  // Restarted too, so each second of the chart is a whole one
    _chartTimeMax = kChartWindowSeconds;
    _updateChart();
    emit statsChanged();
}

void LinkStatsController::_tick()
{
    if (_updateLink()) {
        return;
    }

    const double seconds = static_cast<double>(_clock.elapsed()) / 1000.0;
    _stats.tick(seconds);

    // The window scrolls as Mission Planner's does: 30 s wide, the newest second 5 s from its right edge
    _chartTimeMax = std::max(kChartWindowSeconds, seconds + 5.0);
    _updateChart();
    emit statsChanged();
}

bool LinkStatsController::_updateLink()
{
    LinkInterface* link = nullptr;
    if (Vehicle* const vehicle = MultiVehicleManager::instance()->activeVehicle()) {
        if (const SharedLinkInterfacePtr primary = vehicle->vehicleLinkManager()->primaryLink().lock()) {
            link = primary.get();
        }
    }
    if (!link) {
        const QList<SharedLinkInterfacePtr> links = LinkManager::instance()->links();
        if (!links.isEmpty()) {
            link = links.first().get();
        }
    }
    if (link == _link) {
        return false;
    }

    if (_link) {
        (void) disconnect(_link, nullptr, this, nullptr);
    }
    _link = link;
    _linkName.clear();
    if (_link) {
        (void) connect(_link, &LinkInterface::bytesReceived, this,
                       [this](LinkInterface*, const QByteArray& data) { _stats.addReceivedBytes(data.size()); });
        (void) connect(_link, &LinkInterface::bytesSent, this,
                       [this](LinkInterface*, const QByteArray& data) { _stats.addSentBytes(data.size()); });
        if (const SharedLinkConfigurationPtr config = _link->linkConfiguration()) {
            _linkName = config->name();
        }
    }
    emit linkChanged();
    reset();
    return true;
}

void LinkStatsController::_updateChart()
{
    _received = LinkStats::summarize(_stats.receivedHistory());
    _sent = LinkStats::summarize(_stats.sentHistory());
    _chartRateMax = niceCeiling(std::max(_received.max, _sent.max) * 1.1);

    if (auto* const series = qobject_cast<QLineSeries*>(_receivedSeries.data())) {
        series->replace(_stats.receivedHistory());
    }
    if (auto* const series = qobject_cast<QLineSeries*>(_sentSeries.data())) {
        series->replace(_stats.sentHistory());
    }
}
