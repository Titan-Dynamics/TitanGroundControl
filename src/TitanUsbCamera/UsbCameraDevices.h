#pragma once

#include <QtCore/QList>
#include <QtCore/QLoggingCategory>
#include <QtCore/QObject>
#include <QtCore/QStringList>

Q_DECLARE_LOGGING_CATEGORY(TitanUsbCameraLog)

/// Titan: the USB cameras and capture cards that Android's USB host API reaches (TitanUsbCameraManager.java), as video
/// sources named "USB Camera: <product>". Android's camera service lists a USB camera only on devices whose camera HAL
/// supports external cameras; these are read by the app's own UVC driver instead (TitanUsbCamera). Empty on other
/// platforms, where Qt's camera support lists USB cameras.
class UsbCameraDevices : public QObject
{
    Q_OBJECT

public:
    /// open() results other than a file descriptor, as TitanUsbCameraManager.java returns them
    enum OpenResult
    {
        OpenPending = -1,  ///< Waiting for the user to answer the permission dialog
        OpenFailed = -2,   ///< Unplugged, or Android would not open it
        OpenDenied = -3,   ///< Permission refused; asked again once the camera is plugged back in
    };

    static UsbCameraDevices* instance();

    QStringList names() const;
    bool contains(const QString& name) const;
    /// The Android device name (e.g. /dev/bus/usb/001/002) of a source, or empty
    QString deviceName(const QString& name) const;

    /// Opens a camera, asking for permission first if need be: its file descriptor, or an OpenResult
    int open(const QString& deviceName);
    /// Closes a camera once the UVC driver has finished with its file descriptor
    void close(const QString& deviceName);

    /// Lists the cameras again; Android calls it when one is plugged in or out or a permission request is answered
    void refresh();

signals:
    void changed();

private:
    explicit UsbCameraDevices(QObject* parent = nullptr);

    struct Camera
    {
        QString name;
        QString deviceName;

        bool operator==(const Camera& other) const = default;
    };

    QList<Camera> _cameras;
};
