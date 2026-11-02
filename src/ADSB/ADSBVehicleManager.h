#pragma once

#include <QtCore/QElapsedTimer>
#include <QtCore/QHash>
#include <QtCore/QObject>

#include "ADSB.h"
#include "MAVLinkMessageType.h"

class ADSBTCPLink;
class ADSBVehicle;
class QmlObjectListModel;
class QThread;
class QTimer;
class ADSBVehicleManagerSettings;
class AdsbInternetSource;
class AdsbTrafficSender;

class ADSBVehicleManager : public QObject
{
    Q_OBJECT
    Q_MOC_INCLUDE("QmlObjectListModel.h")

    Q_PROPERTY(const QmlObjectListModel *adsbVehicles READ adsbVehicles CONSTANT)

public:
    explicit ADSBVehicleManager(ADSBVehicleManagerSettings *settings, QObject *parent = nullptr);
    ~ADSBVehicleManager();

    static ADSBVehicleManager *instance();

    const QmlObjectListModel *adsbVehicles() const { return _adsbVehicles; }

    void mavlinkMessageReceived(const mavlink_message_t &message);

    /// Titan: traffic QGroundControl gets itself, from the SBS server or the internet, rather than from the vehicle
    bool isLocalTraffic(uint32_t icaoAddress) const;

public slots:
    void adsbVehicleUpdate(const ADSB::VehicleInfo_t &vehicleInfo);

private slots:
    void _cleanupStaleVehicles();
    void _linkError(const QString &errorMsg, bool stopped = false);
    void _updateInternetTraffic();
    void _removeVehicle(uint32_t icaoAddress);

private:
    void _start(const QString &hostAddress, quint16 port);
    void _stop();
    void _handleADSBVehicle(const mavlink_message_t &message);
    void _updateVehicle(const ADSB::VehicleInfo_t &vehicleInfo);
    void _handleCollision(const mavlink_message_t &message);
    void _clearVehicles();

    ADSBVehicleManagerSettings *_adsbSettings = nullptr;
    QTimer *_adsbVehicleCleanupTimer = nullptr;
    QmlObjectListModel *_adsbVehicles = nullptr;

    QMap<uint32_t, ADSBVehicle*> _adsbICAOMap;
    ADSBTCPLink *_adsbTcpLink = nullptr;
    QThread *_adsbTcpLinkThread = nullptr;

    // Titan: internet traffic, sending traffic to the vehicle, and the vehicle's collision threat reports
    AdsbInternetSource *_internetSource = nullptr;
    AdsbTrafficSender *_trafficSender = nullptr;
    QElapsedTimer _clock;
    QHash<uint32_t, qint64> _localTrafficMs;   ///< When each aircraft last came from the SBS server or the internet
    QHash<uint32_t, qint64> _threatReportMs;   ///< When the vehicle last reported a threat level for each aircraft
    static constexpr qint64 kLocalTrafficTimeoutMs = 10000;
    static constexpr qint64 kThreatTimeoutMs = 5000;   ///< ArduPilot repeats COLLISION about once a second

    static constexpr uint8_t kMaxTimeSinceLastSeen = 15;
    static constexpr unsigned long kThreadStopTimeoutMs = 5000; ///< max time in ms to wait for the tcp link thread to stop
};
