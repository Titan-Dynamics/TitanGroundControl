import QtQuick

import QGroundControl
import QGroundControl.Controls

/// Fly View toolstrip action opening the 3D map settings, shown while the 3D map is up.
ToolStripAction {
    id: root

    objectName: "flyToolStrip_map3DSettingsButton"

    iconSource: "/res/gear-white.svg"
    text:       qsTr("3D Setup")
    visible:    Map3DState.shown

    onTriggered: settingsDialogFactory.open()

    property QGCPopupDialogFactory settingsDialogFactory: QGCPopupDialogFactory {
        dialogComponent: Component {
            Map3DSettingsDialog { }
        }
    }
}
