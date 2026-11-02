#include "AdsbInternetSource.h"

#include <algorithm>

#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

#include "AdsbCategory.h"
#include "MultiVehicleManager.h"
#include "QGCLoggingCategory.h"
#include "QGroundControlQmlGlobal.h"
#include "Vehicle.h"

QGC_LOGGING_CATEGORY(AdsbInternetSourceLog, "TitanAdsb.AdsbInternetSource")

namespace {

constexpr double kKnotsToMetersPerSecond = 0.514444;
constexpr double kFeetToMeters = 0.3048;
constexpr double kFeetPerMinuteToMetersPerSecond = 0.00508;

/// TitanPlanner's contact, which adsb.lol asks API clients to put in their User-Agent
constexpr const char* kContact = "contact@titanuas.com";

/// Non-ICAO addresses (TIS-B, ADS-R) come as "~" and six hex digits; bit 24 keeps them apart from real ones
constexpr uint32_t kNonIcaoAddressFlag = 0x01000000;

bool isAdsbFi(const QString& server)
{
    return server.contains(QLatin1String("adsb.fi"), Qt::CaseInsensitive);
}

QString normalizedServer(const QString& server)
{
    QString normalized = server.trimmed();
    while (normalized.endsWith(QLatin1Char('/'))) {
        normalized.chop(1);
    }
    return normalized.isEmpty() ? QString::fromLatin1(AdsbInternetSource::kDefaultServer) : normalized;
}

}  // namespace

AdsbInternetSource::AdsbInternetSource(const QString& server, QObject* parent)
    : QObject(parent)
    , _network(new QNetworkAccessManager(this))
    , _userAgent(
          QStringLiteral("%1/%2 (contact: %3)")
              .arg(QCoreApplication::applicationName(), QCoreApplication::applicationVersion(), QLatin1String(kContact))
              .toUtf8())
{
    _pollTimer.setSingleShot(true);
    (void) connect(&_pollTimer, &QTimer::timeout, this, &AdsbInternetSource::_poll);
    _publishTimer.setInterval(kPublishIntervalMs);
    (void) connect(&_publishTimer, &QTimer::timeout, this, &AdsbInternetSource::_publish);
    _publishTimer.start();

    setServer(server);
}

AdsbInternetSource::~AdsbInternetSource()
{
    _cancelRequest();
}

void AdsbInternetSource::setServer(const QString& server)
{
    _cancelRequest();
    _server = normalizedServer(server);
    _intervalMs = pollIntervalMs(_server);
    _delayMs = _intervalMs;
    _radiusNm = kInitialRadiusNm;
    _dropAll();
    qCDebug(AdsbInternetSourceLog) << "server" << _server << "every" << _intervalMs << "ms";
    _pollTimer.start(0);
}

QUrl AdsbInternetSource::requestUrl(const QString& server, double latitude, double longitude, int radiusNm)
{
    const QString base = normalizedServer(server);
    const QString lat = QString::number(latitude, 'f', 5);
    const QString lon = QString::number(longitude, 'f', 5);
    if (isAdsbFi(base)) {
        const QString api =
            base.endsWith(QLatin1String("/api"), Qt::CaseInsensitive) ? base : base + QLatin1String("/api");
        return QUrl(QStringLiteral("%1/v3/lat/%2/lon/%3/dist/%4").arg(api, lat, lon).arg(radiusNm));
    }
    return QUrl(QStringLiteral("%1/v2/point/%2/%3/%4").arg(base, lat, lon).arg(radiusNm));
}

int AdsbInternetSource::pollIntervalMs(const QString& server)
{
    // Just over a second for adsb.fi, whose data refreshes every two seconds; adsb.lol's sixty requests a minute are
    // shared by everyone behind the same address, so it gets every three
    return isAdsbFi(server) ? 1100 : 3000;
}

QList<AdsbInternetSource::Report> AdsbInternetSource::parseResponse(const QByteArray& json, qint64 receivedMs)
{
    QList<Report> reports;
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(json, &error);
    if ((error.error != QJsonParseError::NoError) || !document.isObject()) {
        qCDebug(AdsbInternetSourceLog) << "unreadable response:" << error.errorString();
        return reports;
    }
    const QJsonObject root = document.object();

    // The snapshot is itself a moment old; "now" is in milliseconds, or seconds from some servers
    double snapshotAgeSeconds = 0;
    const double snapshot = root.value(QLatin1String("now")).toDouble();
    if (snapshot > 0) {
        const double snapshotMs = (snapshot < 1e11) ? snapshot * 1000.0 : snapshot;
        snapshotAgeSeconds = std::clamp((static_cast<double>(receivedMs) - snapshotMs) / 1000.0, 0.0, 5.0);
    }

    QJsonArray aircraft = root.value(QLatin1String("ac")).toArray();
    if (aircraft.isEmpty()) {
        aircraft = root.value(QLatin1String("aircraft")).toArray();
    }
    reports.reserve(aircraft.size());
    for (const QJsonValue& value : std::as_const(aircraft)) {
        const QJsonObject ac = value.toObject();
        const QJsonValue lat = ac.value(QLatin1String("lat"));
        const QJsonValue lon = ac.value(QLatin1String("lon"));
        if (!lat.isDouble() || !lon.isDouble()) {
            continue;
        }

        QString hex = ac.value(QLatin1String("hex")).toString().trimmed();
        uint32_t flag = 0;
        if (hex.startsWith(QLatin1Char('~'))) {
            hex.remove(0, 1);
            flag = kNonIcaoAddressFlag;
        }
        bool ok = false;
        const uint32_t address = hex.toUInt(&ok, 16);
        if (!ok || (address > 0xFFFFFF)) {
            continue;
        }

        Report report;
        ADSB::VehicleInfo_t& info = report.info;
        info.icaoAddress = address | flag;
        info.availableFlags = ADSB::LocationAvailable | ADSB::CategoryAvailable | ADSB::GroundAvailable;
        info.location = QGeoCoordinate(lat.toDouble(), lon.toDouble());

        // Geometric altitude where there is one, to compare with the vehicle's GPS altitude; else barometric.
        // alt_baro is the string "ground" for aircraft on the ground.
        const QJsonValue baro = ac.value(QLatin1String("alt_baro"));
        const QJsonValue geometric = ac.value(QLatin1String("alt_geom"));
        info.onGround = baro.isString() && (baro.toString() == QLatin1String("ground"));
        if (geometric.isDouble()) {
            info.location.setAltitude(geometric.toDouble() * kFeetToMeters);
            info.altitudeeType = ADSB_ALTITUDE_TYPE_GEOMETRIC;
            info.availableFlags |= ADSB::AltitudeAvailable;
        } else if (baro.isDouble()) {
            info.location.setAltitude(baro.toDouble() * kFeetToMeters);
            info.altitudeeType = ADSB_ALTITUDE_TYPE_PRESSURE_QNH;
            info.availableFlags |= ADSB::AltitudeAvailable;
        }

        const QJsonValue groundSpeed = ac.value(QLatin1String("gs"));
        if (groundSpeed.isDouble()) {
            info.velocity = groundSpeed.toDouble() * kKnotsToMetersPerSecond;
            info.availableFlags |= ADSB::VelocityAvailable;
        }

        const QJsonValue track = ac.value(QLatin1String("track"));
        report.trackKnown = track.isDouble();
        if (report.trackKnown) {
            info.heading = track.toDouble();
            info.availableFlags |= ADSB::HeadingAvailable;
        }

        QJsonValue verticalRate = ac.value(QLatin1String("baro_rate"));
        if (!verticalRate.isDouble()) {
            verticalRate = ac.value(QLatin1String("geom_rate"));
        }
        if (verticalRate.isDouble()) {
            info.verticalVel = verticalRate.toDouble() * kFeetPerMinuteToMetersPerSecond;
            info.availableFlags |= ADSB::VerticalVelAvailable;
        }

        const QString callsign = ac.value(QLatin1String("flight")).toString().trimmed().toUpper();
        if (!callsign.isEmpty()) {
            info.callsign = callsign;
            info.availableFlags |= ADSB::CallsignAvailable;
        }

        // The squawk's four digits, read as a decimal number as MAVLink carries them: "1200" is 1200
        bool squawkOk = false;
        const uint squawk = ac.value(QLatin1String("squawk")).toString().toUInt(&squawkOk, 10);
        if (squawkOk && (squawk <= 7777)) {
            info.squawk = static_cast<uint16_t>(squawk);
            info.availableFlags |= ADSB::SquawkAvailable;
        }

        info.emitterType = AdsbCategory::fromCode(ac.value(QLatin1String("category")).toString());
        info.aircraftType = ac.value(QLatin1String("t")).toString().trimmed();

        report.ageSeconds = std::max(0.0, ac.value(QLatin1String("seen_pos")).toDouble()) + snapshotAgeSeconds;
        reports.append(report);
    }
    return reports;
}

QGeoCoordinate AdsbInternetSource::predict(const QGeoCoordinate& measured, double trackDegrees, double groundSpeed,
                                           double verticalSpeed, double secondsAhead)
{
    if (!measured.isValid() || (groundSpeed <= 0) || (secondsAhead <= 0)) {
        return measured;
    }
    const double seconds = std::min(secondsAhead, kMaxPredictSeconds);
    QGeoCoordinate predicted = measured.atDistanceAndAzimuth(groundSpeed * seconds, trackDegrees);
    if (!qIsNaN(measured.altitude())) {
        predicted.setAltitude(measured.altitude() + (verticalSpeed * seconds));
    }
    return predicted;
}

void AdsbInternetSource::_poll()
{
    if (_reply) {
        return;
    }
    const QGeoCoordinate center = _center();
    if (!center.isValid()) {
        _pollTimer.start(kNoPositionRetryMs);
        return;
    }

    _pollStartedMs = QDateTime::currentMSecsSinceEpoch();
    QNetworkRequest request(requestUrl(_server, center.latitude(), center.longitude(), _radiusNm));
    request.setHeader(QNetworkRequest::UserAgentHeader, _userAgent);
    request.setRawHeader("Accept", "application/json");
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    request.setTransferTimeout(kRequestTimeoutMs);
    _reply = _network->get(request);
    (void) connect(_reply, &QNetworkReply::finished, this, &AdsbInternetSource::_replyFinished);
}

void AdsbInternetSource::_replyFinished()
{
    QNetworkReply* const reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) {
        return;
    }
    reply->deleteLater();
    if (reply != _reply) {
        return;
    }
    _reply = nullptr;

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if ((reply->error() == QNetworkReply::NoError) && (status == 200)) {
        _delayMs = _intervalMs;
        const QList<Report> reports = parseResponse(reply->readAll(), nowMs);
        if (reports.size() < kFewAircraft) {
            _radiusNm = std::min(_radiusNm + kRadiusStepNm, kMaxRadiusNm);
        }
        for (const Report& report : reports) {
            Track& track = _tracks[report.info.icaoAddress];
            track.info = report.info;
            track.trackKnown = report.trackKnown;
            track.receivedMs = nowMs;
            track.measuredMs = nowMs - qRound64(report.ageSeconds * 1000.0);
            emit vehicleUpdated(_predicted(track, nowMs));
        }
    } else if (status == 429) {
        // Retrying sooner than the server says only gets more refusals
        _delayMs = std::min(_delayMs * 2, kMaxDelayMs);
        bool ok = false;
        const int retryAfterSeconds = reply->rawHeader("Retry-After").trimmed().toInt(&ok);
        if (ok) {
            _delayMs = std::max(_delayMs, std::min(retryAfterSeconds * 1000, kMaxRetryAfterMs));
        }
        qCDebug(AdsbInternetSourceLog) << "rate limited, next request in" << _delayMs << "ms";
    } else {
        _delayMs = std::min(_delayMs * 2, kMaxDelayMs);
        qCDebug(AdsbInternetSourceLog) << "request failed:" << status << reply->errorString() << "next in" << _delayMs
                                       << "ms";
    }

    _pollTimer.start(static_cast<int>(std::max<qint64>(0, _delayMs - (nowMs - _pollStartedMs))));
}

void AdsbInternetSource::_cancelRequest()
{
    if (!_reply) {
        return;
    }
    QNetworkReply* const reply = _reply;
    _reply = nullptr;
    (void) reply->disconnect(this);
    reply->abort();
    reply->deleteLater();
}

void AdsbInternetSource::_publish()
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    for (auto it = _tracks.begin(); it != _tracks.end();) {
        if ((nowMs - it->receivedMs) > kDropAfterMs) {
            const uint32_t address = it.key();
            it = _tracks.erase(it);
            emit vehicleLost(address);
        } else {
            emit vehicleUpdated(_predicted(*it, nowMs));
            ++it;
        }
    }
}

void AdsbInternetSource::_dropAll()
{
    const QList<uint32_t> addresses = _tracks.keys();
    _tracks.clear();
    for (const uint32_t address : addresses) {
        emit vehicleLost(address);
    }
}

ADSB::VehicleInfo_t AdsbInternetSource::_predicted(const Track& track, qint64 nowMs) const
{
    ADSB::VehicleInfo_t info = track.info;
    // Aircraft on the ground, without a track or standing still stay where they were reported
    if (!info.onGround && track.trackKnown) {
        info.location = predict(info.location, info.heading, info.velocity, info.verticalVel,
                                static_cast<double>(nowMs - track.measuredMs) / 1000.0);
    }
    return info;
}

QGeoCoordinate AdsbInternetSource::_center()
{
    Vehicle* const vehicle = MultiVehicleManager::instance()->activeVehicle();
    if (vehicle && vehicle->coordinate().isValid()) {
        return vehicle->coordinate();
    }
    return QGroundControlQmlGlobal::flightMapPosition();
}
