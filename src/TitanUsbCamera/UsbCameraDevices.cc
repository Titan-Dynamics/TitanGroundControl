#include "UsbCameraDevices.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QMetaObject>

#ifdef Q_OS_ANDROID
#include <QtCore/QJniEnvironment>
#include <QtCore/QJniObject>
#endif

#include <algorithm>

#include "QGCLoggingCategory.h"

QGC_LOGGING_CATEGORY(TitanUsbCameraLog, "TitanUsbCamera.TitanUsbCamera")

namespace {

#ifdef Q_OS_ANDROID
constexpr const char* kNamePrefix = "USB Camera: ";
constexpr const char* kJavaClass = "org/mavlink/qgroundcontrol/TitanUsbCameraManager";

void jniCamerasChanged(JNIEnv*, jclass)
{
    // Android's main thread
    (void) QMetaObject::invokeMethod(UsbCameraDevices::instance(), &UsbCameraDevices::refresh, Qt::QueuedConnection);
}
#endif

}  // namespace

UsbCameraDevices* UsbCameraDevices::instance()
{
    static UsbCameraDevices* const devices = new UsbCameraDevices(QCoreApplication::instance());
    return devices;
}

UsbCameraDevices::UsbCameraDevices(QObject* parent)
    : QObject(parent)
{
#ifdef Q_OS_ANDROID
    const JNINativeMethod methods[] = {
        {"nativeCamerasChanged", "()V", reinterpret_cast<void*>(jniCamerasChanged)},
    };

    QJniEnvironment env;
    if (!env.registerNativeMethods(kJavaClass, methods, std::size(methods))) {
        qCWarning(TitanUsbCameraLog) << "Failed to register the native methods of" << kJavaClass;
    }

    QJniObject::callStaticMethod<void>(kJavaClass, "initialize", "(Landroid/content/Context;)V",
                                       QNativeInterface::QAndroidApplication::context().object());
    (void) env.checkAndClearExceptions();

    refresh();
#endif
}

QStringList UsbCameraDevices::names() const
{
    QStringList result;
    for (const Camera& camera : _cameras) {
        result.append(camera.name);
    }
    return result;
}

bool UsbCameraDevices::contains(const QString& name) const
{
    return !deviceName(name).isEmpty();
}

QString UsbCameraDevices::deviceName(const QString& name) const
{
    for (const Camera& camera : _cameras) {
        if (camera.name == name) {
            return camera.deviceName;
        }
    }
    return QString();
}

int UsbCameraDevices::open(const QString& deviceName)
{
#ifdef Q_OS_ANDROID
    QJniEnvironment env;
    const jint result = QJniObject::callStaticMethod<jint>(kJavaClass, "open", "(Ljava/lang/String;)I",
                                                           QJniObject::fromString(deviceName).object<jstring>());
    if (env.checkAndClearExceptions()) {
        return OpenFailed;
    }
    return result;
#else
    Q_UNUSED(deviceName);
    return OpenFailed;
#endif
}

void UsbCameraDevices::close(const QString& deviceName)
{
#ifdef Q_OS_ANDROID
    QJniEnvironment env;
    QJniObject::callStaticMethod<void>(kJavaClass, "close", "(Ljava/lang/String;)V",
                                       QJniObject::fromString(deviceName).object<jstring>());
    (void) env.checkAndClearExceptions();
#else
    Q_UNUSED(deviceName);
#endif
}

void UsbCameraDevices::refresh()
{
#ifdef Q_OS_ANDROID
    QJniEnvironment env;
    const QJniObject array = QJniObject::callStaticObjectMethod(kJavaClass, "cameras", "()[Ljava/lang/String;");
    if (env.checkAndClearExceptions() || !array.isValid()) {
        return;
    }

    // Each entry is the device name and the camera's label, separated by a newline
    QList<Camera> cameras;
    const auto entries = array.object<jobjectArray>();
    const jsize count = env->GetArrayLength(entries);
    for (jsize i = 0; i < count; ++i) {
        const QString entry = QJniObject::fromLocalRef(env->GetObjectArrayElement(entries, i)).toString();
        const qsizetype separator = entry.indexOf(QLatin1Char('\n'));
        if (separator <= 0) {
            continue;
        }

        const QString label = QString::fromLatin1(kNamePrefix) + entry.mid(separator + 1);
        QString name = label;
        for (int copy = 2;
             std::any_of(cameras.cbegin(), cameras.cend(), [&name](const Camera& other) { return other.name == name; });
             ++copy) {
            name = QStringLiteral("%1 (%2)").arg(label).arg(copy);
        }
        cameras.append(Camera{name, entry.left(separator)});
    }

    if (cameras != _cameras) {
        _cameras = cameras;
        qCDebug(TitanUsbCameraLog) << "USB cameras:" << names();
    }

    // Also when only a permission request was answered, so a camera waiting for it opens
    emit changed();
#endif
}
