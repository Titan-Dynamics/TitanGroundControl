import QtQuick

import QGroundControl

/// The Titan HUD bound to the active vehicle. With drawBackground off it is an overlay: only the
/// symbology is drawn, so the video shows through.
Item {
    id: root

    property var   vehicle:        QGroundControl.multiVehicleManager.activeVehicle
    property bool  drawBackground: true
    property real  symbolScale:    1    ///< Text and symbol size
    property color symbolColor:    QGroundControl.settingsManager.videoSettings.hudColor.rawValue

    // Detached while hidden so an unused HUD neither polls telemetry nor requests frames
    HudData {
        id:      hudDataSource
        vehicle: root.visible ? root.vehicle : null
    }

    HudItem {
        anchors.fill:   parent
        hudData:        hudDataSource
        drawBackground: root.drawBackground
        symbolScale:    root.symbolScale
        symbolColor:    root.symbolColor
    }
}
