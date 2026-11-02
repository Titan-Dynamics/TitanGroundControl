#include "TitanUpdater.h"

#include <cstdio>

#include <QtCore/QCoreApplication>
#include <QtCore/QDirIterator>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QProcess>
#include <QtCore/QRegularExpression>
#include <QtCore/QSettings>
#include <QtCore/QStandardPaths>
#include <QtCore/QSysInfo>
#include <QtCore/QTemporaryDir>
#include <QtGui/QDesktopServices>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

#include "QGCCompression.h"
#include "QGCLoggingCategory.h"
#include "QGCVersionCheck.h"

QGC_LOGGING_CATEGORY(TitanUpdaterLog, "TitanUpdater.TitanUpdater")

namespace {

constexpr const char* kRepository = "Titan-Dynamics/TitanGroundControl";
constexpr const char* kSettingsGroup = "TitanUpdater";
constexpr const char* kChannelKey = "channel";
constexpr int kRequestTimeoutMs = 30000;

QNetworkRequest gitHubRequest(const QUrl& url, const QByteArray& accept)
{
    QNetworkRequest request(url);
    // GitHub's API refuses requests without a User-Agent
    request.setHeader(
        QNetworkRequest::UserAgentHeader,
        QStringLiteral("%1/%2").arg(QCoreApplication::applicationName(), QCoreApplication::applicationVersion()));
    request.setRawHeader("Accept", accept);
    // Release downloads redirect to GitHub's storage
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(kRequestTimeoutMs);
    return request;
}

}  // namespace

TitanUpdater::TitanUpdater(QObject* parent)
    : QObject(parent)
    , _network(new QNetworkAccessManager(this))
{
    QSettings settings;
    settings.beginGroup(QLatin1String(kSettingsGroup));
    _channel = (settings.value(QLatin1String(kChannelKey), Stable).toInt() == Development) ? Development : Stable;
}

TitanUpdater::~TitanUpdater()
{
    cancel();
}

void TitanUpdater::setChannel(Channel channel)
{
    if (channel == _channel) {
        return;
    }
    cancel();
    _channel = channel;
    QSettings settings;
    settings.beginGroup(QLatin1String(kSettingsGroup));
    settings.setValue(QLatin1String(kChannelKey), static_cast<int>(channel));
    _release = Release();
    emit channelChanged();
    _setState(Idle);
}

QString TitanUpdater::currentVersion() const
{
    return QCoreApplication::applicationVersion();
}

bool TitanUpdater::closesToInstall() const
{
#if defined(Q_OS_WIN)
    return true;
#elif defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    return qEnvironmentVariableIsSet("APPIMAGE");
#else
    return false;
#endif
}

QString TitanUpdater::sourceUrl() const
{
    return QStringLiteral("https://github.com/%1").arg(QLatin1String(kRepository));
}

QString TitanUpdater::releasesUrl() const
{
    return sourceUrl() + QStringLiteral("/releases");
}

QString TitanUpdater::developmentReleaseUrl() const
{
    return releasesUrl() + QStringLiteral("/tag/dev");
}

QString TitanUpdater::assetNameForPlatform()
{
#if defined(Q_OS_WIN)
    return (QSysInfo::currentCpuArchitecture() == QLatin1String("arm64"))
               ? QStringLiteral("TitanGroundControl-installer-ARM64.exe")
               : QStringLiteral("TitanGroundControl-installer-AMD64.exe");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("TitanGroundControl.dmg");
#elif defined(Q_OS_ANDROID)
    return QStringLiteral("TitanGroundControl.apk");
#elif defined(Q_OS_LINUX)
    return (QSysInfo::currentCpuArchitecture() == QLatin1String("x86_64"))
               ? QStringLiteral("TitanGroundControl-Linux-x86_64.zip")
               : QString();
#else
    return QString();
#endif
}

QString TitanUpdater::commitFromReleaseBody(const QString& body)
{
    static const QRegularExpression commit(QStringLiteral("\\*\\*Commit:\\*\\*\\s*([0-9a-fA-F]{7,40})"));
    const QRegularExpressionMatch match = commit.match(body);
    return match.hasMatch() ? match.captured(1).toLower() : QString();
}

QString TitanUpdater::commitFromVersion(const QString& version)
{
    static const QRegularExpression suffix(QStringLiteral("-g([0-9a-fA-F]{4,40})(?:-dirty)?$"));
    const QRegularExpressionMatch match = suffix.match(version.trimmed());
    return match.hasMatch() ? match.captured(1).toLower() : QString();
}

TitanUpdater::Release TitanUpdater::parseRelease(const QByteArray& json, Channel channel, const QString& currentVersion,
                                                 const QString& assetName)
{
    Release release;
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    const QString tag = root.value(QLatin1String("tag_name")).toString();
    if (tag.isEmpty()) {
        return release;
    }
    release.pageUrl = root.value(QLatin1String("html_url")).toString();

    if (channel == Development) {
        // The dev release is always the development branch's newest build, so any other commit counts as older
        const QString commit = commitFromReleaseBody(root.value(QLatin1String("body")).toString());
        const QString running = commitFromVersion(currentVersion);
        release.version = commit.isEmpty() ? tag : QStringLiteral("dev %1").arg(commit.left(7));
        release.newer = !commit.isEmpty() && (running.isEmpty() || !commit.startsWith(running));
    } else {
        release.version = tag;
        release.newer = QGCVersionCheck::isNewerVersion(currentVersion, tag);
    }

    const QJsonArray assets = root.value(QLatin1String("assets")).toArray();
    for (const QJsonValue& value : assets) {
        const QJsonObject asset = value.toObject();
        if (asset.value(QLatin1String("name")).toString() != assetName) {
            continue;
        }
        release.assetName = assetName;
        release.assetUrl = asset.value(QLatin1String("browser_download_url")).toString();
        release.assetSize = asset.value(QLatin1String("size")).toInteger();
        const QString digest = asset.value(QLatin1String("digest")).toString();
        if (digest.startsWith(QLatin1String("sha256:"))) {
            release.sha256 = QByteArray::fromHex(digest.mid(7).toLatin1());
        }
        break;
    }
    return release;
}

void TitanUpdater::check()
{
    if ((_state == Checking) || (_state == Downloading)) {
        return;
    }
    const QString release = (_channel == Development) ? QStringLiteral("tags/dev") : QStringLiteral("latest");
    const QUrl url(
        QStringLiteral("https://api.github.com/repos/%1/releases/%2").arg(QLatin1String(kRepository), release));
    _reply = _network->get(gitHubRequest(url, "application/vnd.github+json"));
    (void) connect(_reply, &QNetworkReply::finished, this, &TitanUpdater::_checkFinished);
    _setState(Checking);
}

void TitanUpdater::_checkFinished()
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

    if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 404) {
        _setState(Failed, tr("GitHub has no release on this channel"));
        return;
    }
    if (reply->error() != QNetworkReply::NoError) {
        _setState(Failed, tr("Could not reach GitHub: %1").arg(reply->errorString()));
        return;
    }
    _release = parseRelease(reply->readAll(), _channel, currentVersion(), assetNameForPlatform());
    if (_release.version.isEmpty()) {
        _setState(Failed, tr("GitHub has no release on this channel"));
        return;
    }
    qCDebug(TitanUpdaterLog) << "latest" << _release.version << "newer" << _release.newer << _release.assetUrl;
    _setState(_release.newer ? Available : UpToDate);
}

void TitanUpdater::download()
{
    if ((_state == Checking) || (_state == Downloading)) {
        return;
    }
    if (_release.assetUrl.isEmpty()) {
        _setState(Failed, tr("This release has no download for this platform"));
        return;
    }
#if defined(Q_OS_ANDROID)
    // Android installs a package itself: the browser downloads it and offers to install it
    (void) QDesktopServices::openUrl(QUrl(_release.assetUrl));
#else
    _file.setFileName(_downloadPath());
    if (!_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        _setState(Failed, tr("Could not write %1").arg(_file.fileName()));
        return;
    }
    _hash.reset();
    _progress = 0;
    emit progressChanged();

    _reply = _network->get(gitHubRequest(QUrl(_release.assetUrl), "application/octet-stream"));
    (void) connect(_reply, &QNetworkReply::readyRead, this, &TitanUpdater::_downloadReadyRead);
    (void) connect(_reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        const qint64 size = (total > 0) ? total : _release.assetSize;
        if (size > 0) {
            _progress = static_cast<double>(received) / static_cast<double>(size);
            emit progressChanged();
        }
    });
    (void) connect(_reply, &QNetworkReply::finished, this, &TitanUpdater::_downloadFinished);
    _setState(Downloading);
#endif
}

void TitanUpdater::_downloadReadyRead()
{
    if (!_reply) {
        return;
    }
    const QByteArray data = _reply->readAll();
    _hash.addData(data);
    if (_file.write(data) != data.size()) {
        const QString error = tr("Could not write %1").arg(_file.fileName());
        cancel();
        _setState(Failed, error);
    }
}

void TitanUpdater::_downloadFinished()
{
    QNetworkReply* const reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) {
        return;
    }
    reply->deleteLater();
    if (reply != _reply) {
        return;
    }
    _downloadReadyRead();
    _reply = nullptr;
    _file.close();

    QString error;
    if (reply->error() != QNetworkReply::NoError) {
        error = tr("Download failed: %1").arg(reply->errorString());
    } else if ((_release.assetSize > 0) && (_file.size() != _release.assetSize)) {
        error = tr("The download is incomplete");
    } else if (!_release.sha256.isEmpty() && (_hash.result() != _release.sha256)) {
        error = tr("The download does not match the release's checksum");
    }
    if (!error.isEmpty()) {
        (void) _file.remove();
        _setState(Failed, error);
        return;
    }
    _progress = 1;
    emit progressChanged();
    _setState(ReadyToInstall);
}

bool TitanUpdater::install()
{
    if (_state != ReadyToInstall) {
        return false;
    }
    const QString path = _downloadPath();
#if defined(Q_OS_WIN)
    // The installer asks for elevation and removes this version before installing the new one, so the app closes
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
        _setState(Failed, tr("Could not start %1").arg(path));
        return false;
    }
    return true;
#elif defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    if (qEnvironmentVariableIsSet("APPIMAGE")) {
        return _installAppImage();
    }
    (void) QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
    return false;
#else
    // macOS mounts the disk image, from which the app is dragged to Applications
    (void) QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    return false;
#endif
}

bool TitanUpdater::_installAppImage()
{
    const QString target = qEnvironmentVariable("APPIMAGE");
    QTemporaryDir unpacked;
    if (!unpacked.isValid() || !QGCCompression::extractArchive(_downloadPath(), unpacked.path())) {
        _setState(Failed, tr("Could not unpack the update"));
        return false;
    }
    QDirIterator it(unpacked.path(), {QStringLiteral("*.AppImage")}, QDir::Files, QDirIterator::Subdirectories);
    if (!it.hasNext()) {
        _setState(Failed, tr("The update has no AppImage"));
        return false;
    }
    const QString appImage = it.next();

    // Staged beside the running AppImage and renamed over it, which the running copy survives since it keeps its
    // open file
    const QString staged = target + QStringLiteral(".update");
    (void) QFile::remove(staged);
    if (!QFile::copy(appImage, staged) ||
        !QFile::setPermissions(staged, QFile::permissions(target) | QFileDevice::ExeOwner | QFileDevice::ExeGroup |
                                           QFileDevice::ExeOther) ||
        (std::rename(QFile::encodeName(staged).constData(), QFile::encodeName(target).constData()) != 0)) {
        (void) QFile::remove(staged);
        _setState(Failed, tr("Could not replace %1").arg(target));
        return false;
    }

    // Started once this instance has exited, since a second instance would refuse to run
    const QString waitThenStart = QStringLiteral("while kill -0 %1 2>/dev/null; do sleep 0.2; done; exec \"$0\"")
                                      .arg(QCoreApplication::applicationPid());
    if (!QProcess::startDetached(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), waitThenStart, target})) {
        _setState(Failed, tr("Updated; start %1 again to use it").arg(QCoreApplication::applicationName()));
        return false;
    }
    return true;
}

void TitanUpdater::cancel()
{
    if (_reply) {
        QNetworkReply* const reply = _reply;
        _reply = nullptr;
        (void) reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
    if (_state == Downloading) {
        _file.close();
        (void) _file.remove();
        _setState(_release.newer ? Available : UpToDate);
    } else if (_state == Checking) {
        _setState(Idle);
    }
}

QString TitanUpdater::_downloadPath() const
{
    QString directory = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (directory.isEmpty() || !QFileInfo(directory).isWritable()) {
        directory = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    }
    return directory + QLatin1Char('/') + _release.assetName;
}

void TitanUpdater::_setState(State state, const QString& errorString)
{
    _state = state;
    _errorString = errorString;
    emit stateChanged();
}
