#include "UvcStream.h"

#include <algorithm>
#include <cstring>
#include <initializer_list>
#include <libusb.h>
#include <libuvc/libuvc.h>
#include <mutex>
#include <tuple>

#include <QtCore/QByteArray>
#include <QtCore/QCoreApplication>
#include <QtCore/QList>
#include <QtCore/QSize>
#include <QtGui/QImage>
#include <QtMultimedia/QVideoFrame>
#include <QtMultimedia/QVideoFrameFormat>

#include "UsbCameraDevices.h"

namespace {

struct Mode
{
    uvc_frame_format format;
    int width;
    int height;
    int fps;
};

struct UncompressedFormat
{
    const char* fourcc;
    uvc_frame_format format;
};

constexpr UncompressedFormat kUncompressedFormats[] = {
    {"YUY2", UVC_FRAME_FORMAT_YUYV}, {"UYVY", UVC_FRAME_FORMAT_UYVY}, {"NV12", UVC_FRAME_FORMAT_NV12},
    {"NV21", UVC_FRAME_FORMAT_NV21}, {"I420", UVC_FRAME_FORMAT_I420}, {"Y800", UVC_FRAME_FORMAT_GRAY8},
};

/// The frame format of a format descriptor, or UVC_FRAME_FORMAT_UNKNOWN for those not shown (H.264, Bayer, RGB)
uvc_frame_format frameFormat(const uvc_format_desc_t* format)
{
    if (format->bDescriptorSubtype == UVC_VS_FORMAT_MJPEG) {
        return UVC_FRAME_FORMAT_MJPEG;
    }
    if (format->bDescriptorSubtype != UVC_VS_FORMAT_UNCOMPRESSED) {
        return UVC_FRAME_FORMAT_UNKNOWN;
    }

    // An uncompressed format's GUID starts with its FourCC
    const QByteArray fourcc(reinterpret_cast<const char*>(format->guidFormat), 4);
    for (const UncompressedFormat& uncompressed : kUncompressedFormats) {
        if (fourcc == uncompressed.fourcc) {
            return uncompressed.format;
        }
    }
    return UVC_FRAME_FORMAT_UNKNOWN;
}

/// The frame rate to ask for: the fastest up to the cap, or else the slowest the camera has
int frameRate(const uvc_frame_desc_t* frame, int cap)
{
    QList<int> rates;
    if (frame->intervals) {
        for (const uint32_t* interval = frame->intervals; *interval; ++interval) {
            rates.append(static_cast<int>(10000000 / *interval));
        }
    } else {
        // A continuous range, whose ends are sure to fall on its step
        for (const uint32_t interval : {frame->dwMinFrameInterval, frame->dwMaxFrameInterval}) {
            if (interval > 0) {
                rates.append(static_cast<int>(10000000 / interval));
            }
        }
    }

    int best = 0;
    for (const int rate : std::as_const(rates)) {
        if (rate <= cap && rate > best) {
            best = rate;
        }
    }
    if (best == 0 && !rates.isEmpty()) {
        best = *std::min_element(rates.cbegin(), rates.cend());
    }
    return best;
}

/// The camera's modes, best first: up to 1080p, as larger frames cost too much to decode; 25 fps or more; then the
/// largest, uncompressed rather than MJPEG (nothing to decode), and the fastest. MJPEG is held to 30 fps, beyond which
/// decoding falls behind, and uncompressed video to 60.
QList<Mode> cameraModes(uvc_device_handle_t* device)
{
    QList<Mode> modes;
    for (const uvc_format_desc_t* format = uvc_get_format_descs(device); format; format = format->next) {
        const uvc_frame_format pixelFormat = frameFormat(format);
        if (pixelFormat == UVC_FRAME_FORMAT_UNKNOWN) {
            continue;
        }

        const int cap = (pixelFormat == UVC_FRAME_FORMAT_MJPEG) ? 30 : 60;
        for (const uvc_frame_desc_t* frame = format->frame_descs; frame; frame = frame->next) {
            const int fps = frameRate(frame, cap);
            if (fps > 0 && frame->wWidth > 0 && frame->wHeight > 0) {
                modes.append(Mode{pixelFormat, frame->wWidth, frame->wHeight, fps});
            }
        }
    }

    const auto rank = [](const Mode& mode) {
        const bool withinHd = mode.width <= 1920 && mode.height <= 1080;
        const int area = mode.width * mode.height;
        return std::make_tuple(withinHd, mode.fps >= 25, withinHd ? area : -area, mode.format != UVC_FRAME_FORMAT_MJPEG,
                               mode.fps);
    };
    std::stable_sort(modes.begin(), modes.end(), [&rank](const Mode& a, const Mode& b) { return rank(a) > rank(b); });
    return modes;
}

struct Plane
{
    int rows;
    int rowBytes;
};

/// Copies an uncompressed frame whose planes follow one another without padding, as UVC sends them
QVideoFrame copyFrame(const uchar* data, qsizetype size, const QSize& frameSize,
                      QVideoFrameFormat::PixelFormat pixelFormat, std::initializer_list<Plane> planes)
{
    qsizetype needed = 0;
    for (const Plane& plane : planes) {
        needed += static_cast<qsizetype>(plane.rows) * plane.rowBytes;
    }
    if (size < needed) {
        return QVideoFrame();  // Cut short by a USB error
    }

    QVideoFrame frame(QVideoFrameFormat(frameSize, pixelFormat));
    if (!frame.map(QVideoFrame::WriteOnly)) {
        return QVideoFrame();
    }

    int index = 0;
    for (const Plane& plane : planes) {
        uchar* destination = frame.bits(index);
        const qsizetype stride = frame.bytesPerLine(index);
        for (int row = 0; row < plane.rows; ++row) {
            std::memcpy(destination + row * stride, data, static_cast<size_t>(plane.rowBytes));
            data += plane.rowBytes;
        }
        ++index;
    }

    frame.unmap();
    return frame;
}

QVideoFrame toVideoFrame(const uvc_frame_t* frame)
{
    const auto* data = static_cast<const uchar*>(frame->data);
    const auto size = static_cast<qsizetype>(frame->data_bytes);
    const int width = static_cast<int>(frame->width);
    const int height = static_cast<int>(frame->height);
    if (!data || size <= 0 || width <= 0 || height <= 0) {
        return QVideoFrame();
    }

    const QSize frameSize(width, height);
    switch (frame->frame_format) {
        case UVC_FRAME_FORMAT_MJPEG: {
            // Qt's libjpeg-turbo supplies the standard Huffman tables that MJPEG frames usually leave out
            QImage image;
            if (!image.loadFromData(data, static_cast<int>(size), "JPG")) {
                return QVideoFrame();
            }
            return QVideoFrame(image);
        }
        case UVC_FRAME_FORMAT_YUYV:
            return copyFrame(data, size, frameSize, QVideoFrameFormat::Format_YUYV, {{height, width * 2}});
        case UVC_FRAME_FORMAT_UYVY:
            return copyFrame(data, size, frameSize, QVideoFrameFormat::Format_UYVY, {{height, width * 2}});
        case UVC_FRAME_FORMAT_GRAY8:
            return copyFrame(data, size, frameSize, QVideoFrameFormat::Format_Y8, {{height, width}});
        case UVC_FRAME_FORMAT_NV12:
            return copyFrame(data, size, frameSize, QVideoFrameFormat::Format_NV12,
                             {{height, width}, {height / 2, width}});
        case UVC_FRAME_FORMAT_NV21:
            return copyFrame(data, size, frameSize, QVideoFrameFormat::Format_NV21,
                             {{height, width}, {height / 2, width}});
        case UVC_FRAME_FORMAT_I420:
            return copyFrame(data, size, frameSize, QVideoFrameFormat::Format_YUV420P,
                             {{height, width}, {height / 2, width / 2}, {height / 2, width / 2}});
        default:
            return QVideoFrame();
    }
}

}  // namespace

UvcStream::UvcStream(FrameHandler handler)
    : _handler(std::move(handler))
{}

UvcStream::~UvcStream()
{
    stop();
}

QString UvcStream::start(int fileDescriptor)
{
    // An Android app may reach only the devices it was handed, so libusb must not look for others
    static std::once_flag noDeviceDiscovery;
    std::call_once(noDeviceDiscovery, [] { (void) libusb_set_option(nullptr, LIBUSB_OPTION_NO_DEVICE_DISCOVERY); });

    uvc_error_t result = uvc_init(&_context, nullptr);
    if (result != UVC_SUCCESS) {
        _context = nullptr;
        return QCoreApplication::translate("TitanUsbCamera", "Could not start the USB camera driver: %1")
            .arg(QString::fromUtf8(uvc_strerror(result)));
    }

    result = uvc_wrap(fileDescriptor, _context, &_device);
    if (result != UVC_SUCCESS) {
        _device = nullptr;
        stop();
        return QCoreApplication::translate("TitanUsbCamera", "Could not open the USB camera: %1")
            .arg(QString::fromUtf8(uvc_strerror(result)));
    }

    const QList<Mode> modes = cameraModes(_device);
    for (const Mode& mode : modes) {
        uvc_stream_ctrl_t control{};
        result = uvc_get_stream_ctrl_format_size(_device, &control, mode.format, mode.width, mode.height, mode.fps);
        if (result == UVC_SUCCESS) {
            result = uvc_start_streaming(_device, &control, &UvcStream::_frameCallback, this, 0);
        }
        if (result == UVC_SUCCESS) {
            qCDebug(TitanUsbCameraLog) << "Streaming" << mode.width << "x" << mode.height << "at" << mode.fps
                                       << "fps, libuvc format" << mode.format;
            return QString();
        }
        qCDebug(TitanUsbCameraLog) << "Mode" << mode.width << "x" << mode.height << "at" << mode.fps
                                   << "fps, libuvc format" << mode.format << "failed:" << uvc_strerror(result);
    }

    stop();
    if (modes.isEmpty()) {
        return QCoreApplication::translate("TitanUsbCamera",
                                           "The USB camera offers no video format that can be shown.");
    }
    return QCoreApplication::translate("TitanUsbCamera", "The USB camera would not start streaming: %1")
        .arg(QString::fromUtf8(uvc_strerror(result)));
}

void UvcStream::stop()
{
    if (_device) {
        // Waits for the transfer and callback threads, so no frame arrives afterwards
        uvc_stop_streaming(_device);
        uvc_close(_device);
        _device = nullptr;
    }
    if (_context) {
        uvc_exit(_context);
        _context = nullptr;
    }
}

void UvcStream::_frameCallback(uvc_frame* frame, void* user)
{
    const QVideoFrame videoFrame = toVideoFrame(frame);
    if (videoFrame.isValid()) {
        static_cast<UvcStream*>(user)->_handler(videoFrame);
    }
}
