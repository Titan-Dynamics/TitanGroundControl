#pragma once

#include <QtCore/QCryptographicHash>
#include <QtCore/QFile>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtQmlIntegration/QtQmlIntegration>

class QNetworkAccessManager;
class QNetworkReply;

/// Updates Titan Ground Control from its GitHub releases. The stable channel follows the latest version release; the
/// development channel follows the dev prerelease, rebuilt from the development branch on every push. It downloads
/// this platform's asset and hands it over: the Windows installer runs while the app closes, an AppImage replaces the
/// running one and restarts it, the macOS disk image opens for the app to be dragged to Applications, and Android
/// opens the package's download in the browser.
class TitanUpdater : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(Channel channel READ channel WRITE setChannel NOTIFY channelChanged)
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY stateChanged)
    Q_PROPERTY(QString releasePageUrl READ releasePageUrl NOTIFY stateChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY stateChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(bool closesToInstall READ closesToInstall CONSTANT)
    Q_PROPERTY(QString sourceUrl READ sourceUrl CONSTANT)
    Q_PROPERTY(QString releasesUrl READ releasesUrl CONSTANT)
    Q_PROPERTY(QString developmentReleaseUrl READ developmentReleaseUrl CONSTANT)

public:
    enum Channel
    {
        Stable,
        Development,
    };
    Q_ENUM(Channel)

    enum State
    {
        Idle,
        Checking,
        UpToDate,
        Available,
        Downloading,
        ReadyToInstall,
        Failed,
    };
    Q_ENUM(State)

    /// What a release offers this platform
    struct Release
    {
        QString version;   ///< The tag, or "dev <commit>" for the development build
        QString pageUrl;
        QString assetUrl;  ///< Empty when the release has nothing for this platform
        QString assetName;
        qint64 assetSize = 0;
        QByteArray sha256;   ///< From the asset's digest, when GitHub gives one
        bool newer = false;  ///< Newer than the running build
    };

    explicit TitanUpdater(QObject* parent = nullptr);
    ~TitanUpdater() override;

    Channel channel() const { return _channel; }

    void setChannel(Channel channel);

    State state() const { return _state; }

    QString currentVersion() const;

    QString latestVersion() const { return _release.version; }

    QString releasePageUrl() const { return _release.pageUrl; }

    QString errorString() const { return _errorString; }

    double progress() const { return _progress; }

    bool closesToInstall() const;
    QString sourceUrl() const;
    QString releasesUrl() const;
    QString developmentReleaseUrl() const;

    /// Asks GitHub for the channel's release
    Q_INVOKABLE void check();
    /// Downloads the release's asset for this platform, or on Android opens it in the browser
    Q_INVOKABLE void download();
    /// Hands the download over to install. True when the app should now close for it.
    Q_INVOKABLE bool install();
    Q_INVOKABLE void cancel();

    /// The release asset for this platform, empty where there is none
    static QString assetNameForPlatform();
    /// The commit a development release was built from, from the "**Commit:**" line the dev workflow writes
    static QString commitFromReleaseBody(const QString& body);
    /// The commit the running build was made from: git describe's "-g<sha>" suffix, empty on a tagged build
    static QString commitFromVersion(const QString& version);
    /// Reads a GitHub release for a channel, judged against the running version
    static Release parseRelease(const QByteArray& json, Channel channel, const QString& currentVersion,
                                const QString& assetName);

signals:
    void channelChanged();
    void stateChanged();
    void progressChanged();

private:
    void _setState(State state, const QString& errorString = QString());
    void _checkFinished();
    void _downloadReadyRead();
    void _downloadFinished();
    bool _installAppImage();
    QString _downloadPath() const;

    QNetworkAccessManager* _network = nullptr;
    QPointer<QNetworkReply> _reply;
    QFile _file;
    QCryptographicHash _hash{QCryptographicHash::Sha256};
    Channel _channel = Stable;
    State _state = Idle;
    Release _release;
    QString _errorString;
    double _progress = 0;
};
