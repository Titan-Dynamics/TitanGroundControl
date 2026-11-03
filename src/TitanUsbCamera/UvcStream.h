#pragma once

#include <functional>

#include <QtCore/QString>

class QVideoFrame;
struct uvc_context;
struct uvc_device_handle;
struct uvc_frame;

/// Titan: one USB camera streaming through libuvc, on a file descriptor from Android's USB host API. It picks the
/// camera's best mode up to 1080p, preferring 25 fps or more, and hands each frame over as a QVideoFrame on libuvc's
/// callback thread: uncompressed frames are copied, MJPEG frames decoded. Android only.
class UvcStream
{
public:
    using FrameHandler = std::function<void(const QVideoFrame&)>;

    explicit UvcStream(FrameHandler handler);
    ~UvcStream();

    UvcStream(const UvcStream&) = delete;
    UvcStream& operator=(const UvcStream&) = delete;

    /// Opens the camera and starts streaming. Blocking: its control transfers can take seconds on a slow camera.
    /// Returns an error message, or nothing once frames are on their way.
    QString start(int fileDescriptor);
    /// Stops streaming and releases the camera, but not the file descriptor
    void stop();

private:
    static void _frameCallback(struct uvc_frame* frame, void* user);

    FrameHandler _handler;
    struct uvc_context* _context = nullptr;
    struct uvc_device_handle* _device = nullptr;
};
