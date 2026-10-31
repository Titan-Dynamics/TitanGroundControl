import QtQuick

import QGroundControl

/// Fly View toolstrip action opening the 3D Map settings page, shown while the 3D map is up.
ToolStripAction {
    objectName: "flyToolStrip_map3DSettingsButton"

    iconSource: "/res/gear-white.svg"
    text:       qsTr("3D Setup")
    visible:    Map3DState.shown

    onTriggered: mainWindow.showSettingsTool("3D Map")
}
