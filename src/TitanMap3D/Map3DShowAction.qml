import QGroundControl
import QGroundControl.Controls

/// Fly View toolstrip action opening and closing the Titan 3D map, which takes the left half of the Fly View.
ToolStripAction {
    id: root

    objectName: "flyToolStrip_map3DButton"

    iconSource: "/qml/QGroundControl/Viewer3D/City3DMapIcon.svg"
    text:       Map3DState.shown ? qsTr("Hide 3D") : qsTr("3D")

    onTriggered: Map3DState.shown = !Map3DState.shown
}
