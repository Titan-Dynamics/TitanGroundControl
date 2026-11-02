#pragma once

#include <QtCore/QHash>
#include <QtCore/QList>
#include <QtCore/QLoggingCategory>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtPositioning/QGeoCoordinate>

#include "ADSB.h"

class QNetworkAccessManager;
class QNetworkReply;

Q_DECLARE_LOGGING_CATEGORY(AdsbInternetSourceLog)

/// ADS-B traffic from an aggregator on the internet, ported from TitanPlanner's adsb.cs. It asks adsb.fi's open data
/// API (or an ADSBExchange-style /v2/point API such as adsb.lol) for the aircraft around the active vehicle, or around
/// the Fly View map while there is no vehicle position, and carries each one forward along its track between
/// responses, so traffic moves smoothly and shows where it is now rather than where it was a few seconds ago.
class AdsbInternetSource : public QObject
{
    Q_OBJECT

public:
    /// One aircraft as a response reports it
    struct Report
    {
        ADSB::VehicleInfo_t info;
        bool trackKnown = false;  ///< False when the feed gave a speed but no track, so it stays where it was reported
        double ageSeconds = 0;    ///< How old its position already was when the response arrived
    };

    static constexpr const char* kDefaultServer = "https://opendata.adsb.fi";

    explicit AdsbInternetSource(const QString& server, QObject* parent = nullptr);
    ~AdsbInternetSource() override;

    void setServer(const QString& server);

    /// True while the aircraft is in the feed
    bool tracks(uint32_t icaoAddress) const { return _tracks.contains(icaoAddress); }

    /// The request for the aircraft within radiusNm of a point. adsb.fi has its own path; the rest use /v2/point.
    static QUrl requestUrl(const QString& server, double latitude, double longitude, int radiusNm);

    /// How often a server is polled: adsb.fi allows one request a second, adsb.lol sixty a minute per address
    static int pollIntervalMs(const QString& server);

    /// The aircraft in a response; receivedMs (milliseconds since the epoch) dates the snapshot it was taken from
    static QList<Report> parseResponse(const QByteArray& json, qint64 receivedMs);

    /// Where an aircraft is after flying secondsAhead along its track, at most kMaxPredictSeconds
    static QGeoCoordinate predict(const QGeoCoordinate& measured, double trackDegrees, double groundSpeed,
                                  double verticalSpeed, double secondsAhead);

    static constexpr int kInitialRadiusNm = 100;
    static constexpr int kMaxRadiusNm = 250;
    static constexpr double kMaxPredictSeconds = 10;
    static constexpr qint64 kDropAfterMs = 30000;  ///< An aircraft missing from the feed this long is dropped

signals:
    void vehicleUpdated(const ADSB::VehicleInfo_t& vehicleInfo);
    void vehicleLost(uint32_t icaoAddress);

private:
    struct Track
    {
        ADSB::VehicleInfo_t info;
        bool trackKnown = false;
        qint64 measuredMs = 0;  ///< When the position was measured
        qint64 receivedMs = 0;  ///< When the feed last listed it
    };

    void _poll();
    void _replyFinished();
    void _cancelRequest();
    void _publish();
    void _dropAll();
    ADSB::VehicleInfo_t _predicted(const Track& track, qint64 nowMs) const;
    static QGeoCoordinate _center();

    QNetworkAccessManager* _network = nullptr;
    QPointer<QNetworkReply> _reply;
    QTimer _pollTimer;
    QTimer _publishTimer;
    QString _server;
    QByteArray _userAgent;
    QHash<uint32_t, Track> _tracks;
    int _radiusNm = kInitialRadiusNm;
    int _intervalMs = 0;
    int _delayMs = 0;
    qint64 _pollStartedMs = 0;

    static constexpr int kFewAircraft = 10;  ///< Fewer than this and the search radius grows
    static constexpr int kRadiusStepNm = 10;
    static constexpr int kMaxDelayMs = 10000;
    static constexpr int kMaxRetryAfterMs = 60000;
    static constexpr int kRequestTimeoutMs = 20000;
    static constexpr int kNoPositionRetryMs = 2500;
    static constexpr int kPublishIntervalMs = 500;
};
