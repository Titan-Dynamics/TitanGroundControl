#include "TitanUsbCamera.h"

#include <QtCore/QMetaObject>

#ifdef Q_OS_ANDROID
#include <QtConcurrent/QtConcurrentRun>
#include <QtCore/QCoreApplication>
#include <QtCore/QPermissions>

#include "UvcStream.h"
#endif

#include <utility>

#include "UsbCameraDevices.h"

TitanUsbCamera::TitanUsbCamera(QObject* parent)
    : QObject(parent)
{
    (void) connect(UsbCameraDevices::instance(), &UsbCameraDevices::changed, this, &TitanUsbCamera::_update);
}

TitanUsbCamera::~TitanUsbCamera()
{
    _stop();
}

void TitanUsbCamera::setSourceName(const QString& sourceName)
{
    if (sourceName == _sourceName) {
        return;
    }

    _stop();
    _sourceName = sourceName;
    emit sourceNameChanged();
    _update();
}

void TitanUsbCamera::setActive(bool active)
{
    if (active == _active) {
        return;
    }

    _active = active;
    emit activeChanged();
    _update();
}

void TitanUsbCamera::setVideoSink(QVideoSink* videoSink)
{
    if (videoSink == _videoSink) {
        return;
    }

    _stop();
    _videoSink = videoSink;
    emit videoSinkChanged();
    _update();
}

void TitanUsbCamera::_update()
{
    const bool isUsbCamera = UsbCameraDevices::instance()->contains(_sourceName);
    if (isUsbCamera != _isUsbCamera) {
        _isUsbCamera = isUsbCamera;
        emit isUsbCameraChanged();
    }

    if (!_active || !_videoSink || !isUsbCamera) {
        _stop();
        _setErrorString(QString());
        return;
    }

#ifdef Q_OS_ANDROID
    if (!_stream) {
        _start();
    }
#endif
}

void TitanUsbCamera::_start()
{
#ifdef Q_OS_ANDROID
    // Android lets an app use a USB camera only once it has the camera permission
    const QCameraPermission cameraPermission;
    switch (qApp->checkPermission(cameraPermission)) {
        case Qt::PermissionStatus::Undetermined:
            qApp->requestPermission(cameraPermission, this, [this](const QPermission&) { _update(); });
            return;
        case Qt::PermissionStatus::Denied:
            _setErrorString(tr("USB cameras need the camera permission. Allow it in Android's settings for this app."));
            return;
        case Qt::PermissionStatus::Granted:
            break;
    }

    UsbCameraDevices* const devices = UsbCameraDevices::instance();
    const QString deviceName = devices->deviceName(_sourceName);
    const int fileDescriptor = devices->open(deviceName);
    if (fileDescriptor == UsbCameraDevices::OpenPending) {
        // Tried again once the permission dialog is answered, which refreshes the devices
        _setErrorString(QString());
        return;
    }
    if (fileDescriptor == UsbCameraDevices::OpenDenied) {
        _setErrorString(tr("Permission to use %1 was refused. Plug it back in to be asked again.").arg(_sourceName));
        return;
    }
    if (fileDescriptor < 0) {
        _setErrorString(tr("Could not open %1.").arg(_sourceName));
        return;
    }

    _setErrorString(QString());
    _deviceName = deviceName;

    const auto stream = std::make_shared<UvcStream>([this](const QVideoFrame& frame) { _frameArrived(frame); });
    _stream = stream;

    // Opening and negotiating take control transfers, which a slow camera can stretch to seconds
    _starting = QtConcurrent::run([stream, fileDescriptor] { return stream->start(fileDescriptor); });
    (void) _starting.then(this, [this, stream](const QString& error) {
        if (stream != _stream) {
            return;  // Stopped meanwhile
        }
        if (!error.isEmpty()) {
            qCWarning(TitanUsbCameraLog) << _sourceName << error;
            _stop();
            _setErrorString(error);
            return;
        }
        _setStreaming(true);
    });
#endif
}

void TitanUsbCamera::_stop()
{
#ifdef Q_OS_ANDROID
    if (!_stream) {
        return;
    }

    const std::shared_ptr<UvcStream> stream = std::exchange(_stream, nullptr);
    _starting.waitForFinished();
    stream->stop();
    UsbCameraDevices::instance()->close(_deviceName);
    _deviceName.clear();

    {
        const QMutexLocker locker(&_frameMutex);
        _pendingFrame = QVideoFrame();
    }
    if (_videoSink) {
        _videoSink->setVideoFrame(QVideoFrame());
    }
#endif

    _setStreaming(false);
}

void TitanUsbCamera::_setStreaming(bool streaming)
{
    if (streaming != _streaming) {
        _streaming = streaming;
        emit streamingChanged();
    }
}

void TitanUsbCamera::_setErrorString(const QString& errorString)
{
    if (errorString != _errorString) {
        _errorString = errorString;
        emit errorStringChanged();
    }
}

void TitanUsbCamera::_frameArrived(const QVideoFrame& frame)
{
    {
        const QMutexLocker locker(&_frameMutex);
        _pendingFrame = frame;
        if (_framePosted) {
            return;
        }
        _framePosted = true;
    }

    (void) QMetaObject::invokeMethod(this, &TitanUsbCamera::_deliverFrame, Qt::QueuedConnection);
}

void TitanUsbCamera::_deliverFrame()
{
    QVideoFrame frame;
    {
        const QMutexLocker locker(&_frameMutex);
        frame = std::exchange(_pendingFrame, QVideoFrame());
        _framePosted = false;
    }

    if (_videoSink && frame.isValid()) {
        _videoSink->setVideoFrame(frame);
    }
}
