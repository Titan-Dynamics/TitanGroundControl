#pragma once

#include <QtCore/QMutex>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QString>
#include <QtMultimedia/QVideoFrame>
#include <QtMultimedia/QVideoSink>
#include <QtQmlIntegration/QtQmlIntegration>

#ifdef Q_OS_ANDROID
#include <memory>

#include <QtCore/QFuture>

class UvcStream;
#endif

/// Titan: shows a USB camera that Android's camera service does not list (UsbCameraDevices) in a VideoOutput, through
/// the app's own UVC driver (UvcStream). FlightDisplayViewUVC.qml uses it in place of Qt's Camera for those sources;
/// on other platforms there are none, and it does nothing.
class TitanUsbCamera : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString sourceName READ sourceName WRITE setSourceName NOTIFY sourceNameChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(QVideoSink* videoSink READ videoSink WRITE setVideoSink NOTIFY videoSinkChanged)
    Q_PROPERTY(bool isUsbCamera READ isUsbCamera NOTIFY isUsbCameraChanged)
    Q_PROPERTY(bool streaming READ streaming NOTIFY streamingChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged)

public:
    explicit TitanUsbCamera(QObject* parent = nullptr);
    ~TitanUsbCamera() override;

    QString sourceName() const { return _sourceName; }

    void setSourceName(const QString& sourceName);

    bool active() const { return _active; }

    void setActive(bool active);

    QVideoSink* videoSink() const { return _videoSink; }

    void setVideoSink(QVideoSink* videoSink);

    /// Whether the source is one of the USB cameras this reads, rather than one for Qt's Camera
    bool isUsbCamera() const { return _isUsbCamera; }

    bool streaming() const { return _streaming; }

    QString errorString() const { return _errorString; }

signals:
    void sourceNameChanged();
    void activeChanged();
    void videoSinkChanged();
    void isUsbCameraChanged();
    void streamingChanged();
    void errorStringChanged();

private:
    void _update();
    void _start();
    void _stop();
    void _setStreaming(bool streaming);
    void _setErrorString(const QString& errorString);
    void _frameArrived(const QVideoFrame& frame);
    void _deliverFrame();

    QString _sourceName;
    bool _active = false;
    QPointer<QVideoSink> _videoSink;
    bool _isUsbCamera = false;
    bool _streaming = false;
    QString _errorString;

    // Frames come in on libuvc's thread; only the latest waits for the GUI thread, so a busy one skips frames
    QMutex _frameMutex;
    QVideoFrame _pendingFrame;
    bool _framePosted = false;

#ifdef Q_OS_ANDROID
    std::shared_ptr<UvcStream> _stream;
    QFuture<QString> _starting;
    QString _deviceName;
#endif
};
