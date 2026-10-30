pragma Singleton

import QtQuick

/// Fly View state of the Titan 3D map, kept while the view is closed and reopened.
QtObject {
    property bool shown:         false
    property real freeLookYaw:   0   ///< degrees, added to the last vehicle heading while disconnected
    property real freeLookPitch: 0   ///< degrees, positive up
}
