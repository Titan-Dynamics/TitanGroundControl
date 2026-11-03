import QtQuick
import QtMultimedia

import QGroundControl
import QGroundControl.Controls

Rectangle {
    id:                 _root
    width:              parent.width
    height:             parent.height
    implicitWidth:      videoOutput.implicitWidth
    implicitHeight:     videoOutput.implicitHeight   
    color:              Qt.rgba(0,0,0,0.75)
    clip:               true
    anchors.centerIn:   parent
    visible:            _videoManager.isUvc

    property var _videoManager: QGroundControl.videoManager

    function adjustAspectRatio() {
        //-- Set aspect ratio
        var resolution = camera.cameraFormat.resolution
        if (resolution.height > 0 && resolution.width > 0) {
            var aspectRatio = resolution.width / resolution.height
            _root.height = parent.height * aspectRatio
        }
    }

    MediaDevices {
        id: mediaDevices

        function findCameraDevice(cameraId) {
            var videoInputs = mediaDevices.videoInputs
            for (var i = 0; i < videoInputs.length; i++) {
                if (videoInputs[i].description === cameraId) {
                    return videoInputs[i]
                }
            }
            return mediaDevices.defaultVideoInput
        }

        // Titan: not for a USB camera that only TitanUsbCamera reaches, which would open the default camera instead
        function hasCameraDevice(cameraId) {
            var videoInputs = mediaDevices.videoInputs
            for (var i = 0; i < videoInputs.length; i++) {
                if (videoInputs[i].description === cameraId) {
                    return true
                }
            }
            return false
        }
    }

    CaptureSession {
        camera: Camera {
            id:             camera
            cameraDevice:   mediaDevices.findCameraDevice(_videoManager.uvcVideoSourceID)
            active:         _root._videoManager.isUvc && mediaDevices.hasCameraDevice(_root._videoManager.uvcVideoSourceID)

            onCameraDeviceChanged: {
                if (active) {
                    adjustAspectRatio()
                }
            }

            onActiveChanged: {
                if (active) {
                    adjustAspectRatio()
                }
            }
        }
        videoOutput: videoOutput
    }

    VideoOutput {
        id:             videoOutput
        anchors.fill:   parent
        fillMode:       VideoOutput.PreserveAspectCrop
    }

    // Titan: a USB camera that Android's camera service does not list, through the app's own UVC driver
    TitanUsbCamera {
        id:         usbCamera
        sourceName: _root._videoManager.uvcVideoSourceID
        active:     _root._videoManager.isUvc
        videoSink:  videoOutput.videoSink
    }

    QGCLabel {
        anchors.centerIn:       parent
        width:                  parent.width * 0.8
        horizontalAlignment:    Text.AlignHCenter
        wrapMode:               Text.WordWrap
        text:                   usbCamera.errorString
        visible:                text !== ""
    }
}
