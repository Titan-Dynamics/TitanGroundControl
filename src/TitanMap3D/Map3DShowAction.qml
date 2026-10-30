import QGroundControl
import QGroundControl.Controls

/// Fly View toolstrip action switching between the map and the Titan 3D map.
ToolStripAction {
    id: root

    objectName: "flyToolStrip_map3DButton"

    iconSource: Map3DState.shown ? "/qmlimages/PaperPlane.svg" : "/qml/QGroundControl/Viewer3D/City3DMapIcon.svg"
    text:       Map3DState.shown ? qsTr("2D") : qsTr("3D")

    onTriggered: Map3DState.shown = !Map3DState.shown
}
