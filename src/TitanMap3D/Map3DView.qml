import QtQuick
import QtPositioning

import QGroundControl
import QGroundControl.Controls
import QGroundControl.TitanHud

/// The Titan 3D map view, Mission Planner's Map3D: the scene, its mouse controls, the HUD overlay, ADS-B tooltips
/// and the stats panel.
Item {
    id: root

    property var  map2DCenter        ///< The 2D map's centre, followed while no vehicle is connected
    property real topInset:     0    ///< Kept clear (toolbar) by the HUD and the stats panel
    property real rightInset:   0    ///< Kept clear (top-right widgets) by the stats panel

    readonly property var _settings: QGroundControl.settingsManager.map3DSettings

    Map3DItem {
        id:             map3D
        anchors.fill:   parent
        vehicle:        QGroundControl.multiVehicleManager.activeVehicle
        map2DCenter:    root.map2DCenter ? root.map2DCenter : QtPositioning.coordinate()
        freeLookYaw:    Map3DState.freeLookYaw
        freeLookPitch:  Map3DState.freeLookPitch
    }

    // Mission Planner's mouse: drag orbits around the vehicle and raises or lowers the camera (free-look while
    // disconnected), the wheel moves the camera closer or further, and a double click puts it back behind
    MouseArea {
        id:                 mouseArea
        anchors.fill:       parent
        acceptedButtons:    Qt.LeftButton
        hoverEnabled:       true

        property bool _dragging: false
        property real _lastX:    0
        property real _lastY:    0

        onPressed: (mouse) => {
            map3D.markActive()
            _dragging = true
            _lastX = mouse.x
            _lastY = mouse.y
        }
        onReleased: _dragging = false
        onCanceled: _dragging = false

        onPositionChanged: (mouse) => {
            if (!_dragging) {
                map3D.hover(mouse.x, mouse.y)
                return
            }
            map3D.markActive()
            const dx = mouse.x - _lastX
            const dy = mouse.y - _lastY
            if (map3D.vehicleConnected) {
                map3D.cameraAngle += dx * 0.5
                map3D.cameraHeight = Math.max(-5, Math.min(5, map3D.cameraHeight + dy * 0.005))
            } else {
                // Reversed for a natural "grab the view" feel
                Map3DState.freeLookYaw -= dx * 0.3
                Map3DState.freeLookPitch = Math.max(-89, Math.min(89, Map3DState.freeLookPitch + dy * 0.3))
            }
            _lastX = mouse.x
            _lastY = mouse.y
        }

        onExited: map3D.hoverExited()

        onDoubleClicked: {
            map3D.markActive()
            map3D.cameraAngle = 0
        }

        onWheel: (wheel) => {
            if (!map3D.vehicleConnected) {
                return
            }
            // 0.1 per notch; scrolling up moves closer
            map3D.cameraDistance = Math.max(0.1, Math.min(10, map3D.cameraDistance - wheel.angleDelta.y / 1200))
            map3D.markActive()
        }
    }

    // Touch screens have no wheel: pinch for the camera distance
    PinchHandler {
        target:     null
        enabled:    map3D.vehicleConnected

        property real _startDistance: 0

        onActiveChanged: {
            if (active) {
                _startDistance = map3D.cameraDistance
            }
        }
        onActiveScaleChanged: {
            if (active && activeScale > 0) {
                map3D.cameraDistance = Math.max(0.1, Math.min(10, _startDistance / activeScale))
                map3D.markActive()
            }
        }
    }

    TitanHud {
        anchors.fill:       parent
        anchors.topMargin:  root.topInset
        drawBackground:     false
        visible:            root._settings.hudOverlay.rawValue
    }

    Rectangle {
        id:         adsbTooltip
        x:          Math.min(mouseArea.mouseX + 15, root.width - width - 4)
        y:          Math.min(mouseArea.mouseY + 15, root.height - height - 4)
        width:      tooltipLabel.contentWidth + ScreenTools.defaultFontPixelWidth * 1.5
        height:     tooltipLabel.contentHeight + ScreenTools.defaultFontPixelHeight * 0.5
        radius:     ScreenTools.defaultFontPixelHeight / 4
        color:      Qt.rgba(1, 1, 1, 0.92)
        border.color: Qt.rgba(0, 0, 0, 0.4)
        visible:    map3D.adsbTooltip !== ""

        QGCLabel {
            id:                 tooltipLabel
            anchors.centerIn:   parent
            text:               map3D.adsbTooltip
            color:              "black"
        }
    }

    Map3DStatsPanel {
        anchors.top:        parent.top
        anchors.right:      parent.right
        anchors.topMargin:  root.topInset + ScreenTools.defaultFontPixelHeight * 0.5
        anchors.rightMargin: root.rightInset + ScreenTools.defaultFontPixelWidth
        stats:              map3D.stats
        visible:            root._settings.statsForNerds.rawValue
    }
}
