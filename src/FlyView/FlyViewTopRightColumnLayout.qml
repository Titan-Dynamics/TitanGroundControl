import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FlyView
import QGroundControl.FlightMap

ColumnLayout {
    spacing: ScreenTools.defaultFontPixelHeight / 2

    TerrainProgress {
        Layout.fillWidth: true
    }

    // AI Chat Widget for natural language vehicle control
    Loader {
        id:                 aiChatLoader
        Layout.alignment:   Qt.AlignRight
        sourceComponent:    _showAIChatWidget && globals.activeVehicle ? aiChatComponent : undefined

        property var _flyViewSettings: QGroundControl.settingsManager.flyViewSettings
        property bool _showAIChatWidget: _flyViewSettings.showAIChatWidget.rawValue
        property real rightEdgeCenterInset: visible ? parent.width - x : 0

        Component {
            id: aiChatComponent

            AIChatWidget {
            }
        }
    }

    // We use a Loader to load the photoVideoControlComponent only when we have an active vehicle and a camera manager.
    // This make it easier to implement PhotoVideoControl without having to check for the mavlink camera
    // to be null all over the place
    // Titan: and only when the camera controls are switched on in the Fly View settings
    Loader {
        id:                 photoVideoControlLoader
        Layout.alignment:   Qt.AlignRight
        sourceComponent:    _showCameraControls && globals.activeVehicle && globals.activeVehicle.cameraManager ? photoVideoControlComponent : undefined

        property bool _showCameraControls: QGroundControl.settingsManager.flyViewSettings.showCameraControls.rawValue

        Component {
            id: photoVideoControlComponent

            PhotoVideoControl {
            }
        }
    }
}
