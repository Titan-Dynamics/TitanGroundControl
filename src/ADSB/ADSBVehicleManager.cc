#include "ADSBVehicleManager.h"
#include "MAVLinkLib.h"
#include "AppMessages.h"
#include "SettingsManager.h"
#include "ADSBVehicleManagerSettings.h"
#include "ADSBTCPLink.h"
#include "ADSBVehicle.h"
#include "AdsbInternetSource.h"
#include "AdsbTrafficSender.h"
#include "QmlObjectListModel.h"
#include "QGCLoggingCategory.h"

#include <QtCore/QApplicationStatic>
#include <QtCore/QThread>
#include <QtCore/QTimer>

QGC_LOGGING_CATEGORY(ADSBVehicleManagerLog, "ADSB.ADSBVehicleManager")

Q_APPLICATION_STATIC(ADSBVehicleManager, _adsbVehicleManager, SettingsManager::instance()->adsbVehicleManagerSettings());

ADSBVehicleManager::ADSBVehicleManager(ADSBVehicleManagerSettings *settings, QObject *parent)
    : QObject(parent)
    , _adsbSettings(settings)
    , _adsbVehicleCleanupTimer(new QTimer(this))
    , _adsbVehicles(new QmlObjectListModel(this))
{
    // qCDebug(ADSBVehicleManagerLog) << Q_FUNC_INFO << this;

    (void) qRegisterMetaType<ADSB::VehicleInfo_t>("ADSB::VehicleInfo_t");

    _adsbVehicleCleanupTimer->setSingleShot(false);
    _adsbVehicleCleanupTimer->setInterval(1000);
    (void) connect(_adsbVehicleCleanupTimer, &QTimer::timeout, this, &ADSBVehicleManager::_cleanupStaleVehicles);
    // Always run: vehicles can also arrive via mavlink messages, not just the tcp link
    _adsbVehicleCleanupTimer->start();
    _clock.start();

    Fact* const adsbEnabled = _adsbSettings->adsbServerConnectEnabled();
    Fact* const hostAddress = _adsbSettings->adsbServerHostAddress();
    Fact* const port = _adsbSettings->adsbServerPort();
    // Titan: internet traffic replaces the SBS server while it is on
    Fact* const internetEnabled = _adsbSettings->adsbInternetEnabled();

    (void) connect(adsbEnabled, &Fact::rawValueChanged, this, [this, hostAddress, port, internetEnabled](QVariant value) {
        if (value.toBool() && !internetEnabled->rawValue().toBool()) {
            _start(hostAddress->rawValue().toString(), port->rawValue().toUInt());
        } else {
            _stop();
        }
    });

    if (adsbEnabled->rawValue().toBool() && !internetEnabled->rawValue().toBool()) {
        _start(hostAddress->rawValue().toString(), port->rawValue().toUInt());
    }

    // Titan
    (void) connect(internetEnabled, &Fact::rawValueChanged, this, &ADSBVehicleManager::_updateInternetTraffic);
    (void) connect(_adsbSettings->adsbInternetServer(), &Fact::rawValueChanged, this, [this](const QVariant &server) {
        if (_internetSource) {
            _internetSource->setServer(server.toString());
        }
    });
    (void) connect(_adsbSettings->adsbInternetRadius(), &Fact::rawValueChanged, this, [this](const QVariant &radius) {
        if (_internetSource) {
            _internetSource->setRadiusNm(radius.toInt());
        }
    });
    (void) connect(_adsbSettings->adsbInternetMaxAircraft(), &Fact::rawValueChanged, this, [this](const QVariant &count) {
        if (_internetSource) {
            _internetSource->setMaxAircraft(count.toInt());
        }
    });
    _updateInternetTraffic();
    _trafficSender = new AdsbTrafficSender(this, _adsbSettings->adsbSendToVehicle(), this);
}

ADSBVehicleManager::~ADSBVehicleManager()
{
    _stop();

    // qCDebug(ADSBVehicleManagerLog) << Q_FUNC_INFO << this;
}

ADSBVehicleManager *ADSBVehicleManager::instance()
{
    return _adsbVehicleManager();
}

void ADSBVehicleManager::mavlinkMessageReceived(const mavlink_message_t &message)
{
    switch (message.msgid) {
    case MAVLINK_MSG_ID_ADSB_VEHICLE:
        _handleADSBVehicle(message);
        break;
    case MAVLINK_MSG_ID_COLLISION:
        _handleCollision(message);  // Titan
        break;
    default:
        break;
    }
}

bool ADSBVehicleManager::isLocalTraffic(uint32_t icaoAddress) const
{
    const auto it = _localTrafficMs.constFind(icaoAddress);
    return (it != _localTrafficMs.constEnd()) && ((_clock.elapsed() - it.value()) < kLocalTrafficTimeoutMs);
}

void ADSBVehicleManager::_handleADSBVehicle(const mavlink_message_t &message)
{
    mavlink_adsb_vehicle_t adsbVehicleMsg{};
    mavlink_msg_adsb_vehicle_decode(&message, &adsbVehicleMsg);

    if (adsbVehicleMsg.tslc > kMaxTimeSinceLastSeen) {
        return;
    }

    // Titan: traffic QGroundControl sent the vehicle comes back from it; the source it came from keeps it
    if (isLocalTraffic(adsbVehicleMsg.ICAO_address)) {
        return;
    }

    ADSB::VehicleInfo_t vehicleInfo{};

    vehicleInfo.availableFlags = ADSB::AvailableInfoTypes::fromInt(0);

    vehicleInfo.icaoAddress = adsbVehicleMsg.ICAO_address;
    vehicleInfo.lastContact = adsbVehicleMsg.tslc;

    if (adsbVehicleMsg.flags & ADSB_FLAGS_VALID_COORDS) {
        vehicleInfo.availableFlags |= ADSB::LocationAvailable;
        vehicleInfo.location.setLatitude(adsbVehicleMsg.lat / 1e7);
        vehicleInfo.location.setLongitude(adsbVehicleMsg.lon / 1e7);
    }

    if (adsbVehicleMsg.flags & ADSB_FLAGS_VALID_ALTITUDE) {
        vehicleInfo.availableFlags |= ADSB::AltitudeAvailable;
        vehicleInfo.location.setAltitude(adsbVehicleMsg.altitude / 1e3);
    }

    if (adsbVehicleMsg.flags & ADSB_FLAGS_VALID_HEADING) {
        vehicleInfo.availableFlags |= ADSB::HeadingAvailable;
        vehicleInfo.heading = adsbVehicleMsg.heading / 1e2;
    }

    if (adsbVehicleMsg.flags & ADSB_FLAGS_VALID_VELOCITY) {
        vehicleInfo.availableFlags |= ADSB::VelocityAvailable;
        vehicleInfo.velocity = adsbVehicleMsg.hor_velocity / 1e2;
    }

    if (adsbVehicleMsg.flags & ADSB_FLAGS_VALID_CALLSIGN) {
        vehicleInfo.availableFlags |= ADSB::CallsignAvailable;
        vehicleInfo.callsign = QString::fromLatin1(adsbVehicleMsg.callsign, sizeof(adsbVehicleMsg.callsign));
    }

    if (adsbVehicleMsg.flags & ADSB_FLAGS_VALID_SQUAWK) {
        vehicleInfo.availableFlags |= ADSB::SquawkAvailable;
        vehicleInfo.squawk = adsbVehicleMsg.squawk;
    }

    if (adsbVehicleMsg.flags & ADSB_FLAGS_SIMULATED) {
        vehicleInfo.simulated = true;
    }

    if (adsbVehicleMsg.flags & ADSB_FLAGS_VERTICAL_VELOCITY_VALID) {
        vehicleInfo.availableFlags |= ADSB::VerticalVelAvailable;
        vehicleInfo.verticalVel = adsbVehicleMsg.ver_velocity;
    }

    if (adsbVehicleMsg.flags & ADSB_FLAGS_BARO_VALID) {
        vehicleInfo.baro = true;
    }

    if (adsbVehicleMsg.flags & ADSB_FLAGS_SOURCE_UAT) {

    }

    adsbVehicleMsg.altitude_type = adsbVehicleMsg.altitude_type;
    adsbVehicleMsg.emitter_type = adsbVehicleMsg.emitter_type;

    // Titan: the category picks the aircraft's map icon
    vehicleInfo.availableFlags |= ADSB::CategoryAvailable;
    vehicleInfo.emitterType = static_cast<ADSB_EMITTER_TYPE>(adsbVehicleMsg.emitter_type);

    _updateVehicle(vehicleInfo);
}

void ADSBVehicleManager::adsbVehicleUpdate(const ADSB::VehicleInfo_t &vehicleInfo)
{
    // Titan: the SBS server and the internet come through here, the vehicle through _handleADSBVehicle
    _localTrafficMs[vehicleInfo.icaoAddress] = _clock.elapsed();
    _updateVehicle(vehicleInfo);
}

void ADSBVehicleManager::_updateVehicle(const ADSB::VehicleInfo_t &vehicleInfo)
{
    const uint32_t icaoAddress = vehicleInfo.icaoAddress;
    if (_adsbICAOMap.contains(icaoAddress)) {
        _adsbICAOMap[icaoAddress]->update(vehicleInfo);
        return;
    }

    if (vehicleInfo.availableFlags & ADSB::LocationAvailable) {
        ADSBVehicle* const adsbVehicle = new ADSBVehicle(vehicleInfo, this);
        _adsbICAOMap[icaoAddress] = adsbVehicle;
        _adsbVehicles->append(adsbVehicle);
        qCDebug(ADSBVehicleManagerLog) << "Added" << QString::number(adsbVehicle->icaoAddress());
    }
}

void ADSBVehicleManager::_start(const QString &hostAddress, quint16 port)
{
    if (_adsbTcpLink) {
        qCWarning(ADSBVehicleManagerLog) << "TCP Link already started";
        return;
    }

    if (hostAddress.isEmpty()) {
        qCWarning(ADSBVehicleManagerLog) << "No ADSB server host address set";
        return;
    }

    // Run the link on a worker thread so socket reads and SBS-1 parsing stay off the ui thread.
    // The thread is unparented so QObject parent-child destruction can never delete it while it
    // is still running (which is fatal); _stop() manages its lifetime explicitly.
    _adsbTcpLinkThread = new QThread();
    _adsbTcpLinkThread->setObjectName(QStringLiteral("ADSBTCPLink"));

    _adsbTcpLink = new ADSBTCPLink(hostAddress, port); // unparented, deleted when the thread finishes
    _adsbTcpLink->moveToThread(_adsbTcpLinkThread);

    (void) connect(_adsbTcpLinkThread, &QThread::started, _adsbTcpLink, &ADSBTCPLink::init);
    (void) connect(_adsbTcpLinkThread, &QThread::finished, _adsbTcpLink, &QObject::deleteLater);
    (void) connect(_adsbTcpLink, &ADSBTCPLink::adsbVehicleUpdate, this, &ADSBVehicleManager::adsbVehicleUpdate, Qt::QueuedConnection);
    (void) connect(_adsbTcpLink, &ADSBTCPLink::errorOccurred, this, &ADSBVehicleManager::_linkError, Qt::QueuedConnection);

    _adsbTcpLinkThread->start();
}

void ADSBVehicleManager::_stop()
{
    if (!_adsbTcpLinkThread) {
        return;
    }

    _adsbTcpLinkThread->quit();
    if (!_adsbTcpLinkThread->wait(kThreadStopTimeoutMs)) {
        // Bail out without deleting or resetting state: destroying a still-running QThread is
        // fatal, and clearing the pointers would allow a second link to be started alongside it.
        qCWarning(ADSBVehicleManagerLog) << "ADSBTCPLink thread failed to stop";
        return;
    }
    // wait() succeeded so the thread has finished: delete it directly rather than with
    // deleteLater(), which would leak during shutdown once the event loop stops running.
    delete _adsbTcpLinkThread;
    _adsbTcpLinkThread = nullptr;
    _adsbTcpLink = nullptr; // deleted via the thread finished connection

    _clearVehicles();
}

void ADSBVehicleManager::_clearVehicles()
{
    _adsbVehicles->clearAndDeleteContents();
    _adsbICAOMap.clear();
    _localTrafficMs.clear();
    _threatReportMs.clear();
}

void ADSBVehicleManager::_cleanupStaleVehicles()
{
    for (qsizetype i = _adsbVehicles->count() - 1; i >= 0; i--) {
        ADSBVehicle* const adsbVehicle = _adsbVehicles->value<ADSBVehicle*>(i);
        if (adsbVehicle->expired()) {
            qCDebug(ADSBVehicleManagerLog) << "Expired" << QString::number(adsbVehicle->icaoAddress());
            (void) _adsbVehicles->removeAt(i);
            (void) _adsbICAOMap.remove(adsbVehicle->icaoAddress());
            (void) _localTrafficMs.remove(adsbVehicle->icaoAddress());
            (void) _threatReportMs.remove(adsbVehicle->icaoAddress());
            adsbVehicle->deleteLater();
        }
    }

    // Titan: a threat the vehicle has stopped reporting is over
    const qint64 now = _clock.elapsed();
    for (auto it = _threatReportMs.begin(); it != _threatReportMs.end();) {
        if ((now - it.value()) > kThreatTimeoutMs) {
            if (ADSBVehicle* const adsbVehicle = _adsbICAOMap.value(it.key(), nullptr)) {
                adsbVehicle->setThreatLevel(MAV_COLLISION_THREAT_LEVEL_NONE);
            }
            it = _threatReportMs.erase(it);
        } else {
            ++it;
        }
    }
}

void ADSBVehicleManager::_updateInternetTraffic()
{
    const bool enabled = _adsbSettings->adsbInternetEnabled()->rawValue().toBool();
    if (enabled == (_internetSource != nullptr)) {
        return;
    }

    if (enabled) {
        _stop();
        _internetSource = new AdsbInternetSource(_adsbSettings->adsbInternetServer()->rawValue().toString(),
                                                 _adsbSettings->adsbInternetRadius()->rawValue().toInt(),
                                                 _adsbSettings->adsbInternetMaxAircraft()->rawValue().toInt(), this);
        (void) connect(_internetSource, &AdsbInternetSource::vehicleUpdated, this, &ADSBVehicleManager::adsbVehicleUpdate);
        (void) connect(_internetSource, &AdsbInternetSource::vehicleLost, this, &ADSBVehicleManager::_removeVehicle);
        return;
    }

    delete _internetSource;
    _internetSource = nullptr;
    _clearVehicles();
    if (_adsbSettings->adsbServerConnectEnabled()->rawValue().toBool()) {
        _start(_adsbSettings->adsbServerHostAddress()->rawValue().toString(), _adsbSettings->adsbServerPort()->rawValue().toUInt());
    }
}

void ADSBVehicleManager::_removeVehicle(uint32_t icaoAddress)
{
    ADSBVehicle* const adsbVehicle = _adsbICAOMap.take(icaoAddress);
    (void) _localTrafficMs.remove(icaoAddress);
    (void) _threatReportMs.remove(icaoAddress);
    if (adsbVehicle) {
        (void) _adsbVehicles->removeOne(adsbVehicle);
        adsbVehicle->deleteLater();
    }
}

void ADSBVehicleManager::_handleCollision(const mavlink_message_t &message)
{
    mavlink_collision_t collision{};
    mavlink_msg_collision_decode(&message, &collision);
    if (collision.src != MAV_COLLISION_SRC_ADSB) {
        return;
    }
    ADSBVehicle* const adsbVehicle = _adsbICAOMap.value(collision.id, nullptr);
    if (!adsbVehicle) {
        return;
    }
    adsbVehicle->setThreatLevel(collision.threat_level);
    _threatReportMs[collision.id] = _clock.elapsed();
}

void ADSBVehicleManager::_linkError(const QString &errorMsg, bool stopped)
{
    qCDebug(ADSBVehicleManagerLog) << errorMsg;

    QString msg = tr("ADSB Server Error: %1").arg(errorMsg);

    if (stopped) {
        (void) msg.append(QLatin1Char('\n'));
        (void) msg.append(tr("ADSB has been disabled"));
        _adsbSettings->adsbServerConnectEnabled()->setRawValue(false);
    }

    QGC::showAppMessage(msg);
}
