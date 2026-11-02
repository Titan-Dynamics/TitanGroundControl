pragma Singleton

import QtQuick

import QGroundControl

/// Fly View state of the Titan 3D map, kept while the view is closed and reopened.
QtObject {
    property bool shown:         false
    property real freeLookYaw:   0   ///< degrees, added to the last vehicle heading while disconnected
    property real freeLookPitch: 0   ///< degrees, positive up

    /// Shown beside the 2D map, on the right half of the Fly View, rather than in its place
    readonly property bool splitScreen: QGroundControl.settingsManager.map3DSettings.splitScreen.rawValue
    /// Shown over the whole Fly View in place of the 2D map
    readonly property bool coversMap:   shown && !splitScreen
}
