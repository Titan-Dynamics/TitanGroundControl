import QtQuick

import QGroundControl

/// Mission Planner style HUD bound to the active vehicle. With drawBackground off it is an overlay:
/// sky, ground and the horizon line are skipped so the video shows through.
Item {
    id: root

    property var  vehicle:        QGroundControl.multiVehicleManager.activeVehicle
    property bool drawBackground: true

    // Detached while hidden so an unused HUD neither polls telemetry nor requests frames
    HudData {
        id:      hudDataSource
        vehicle: root.visible ? root.vehicle : null
    }

    HudItem {
        anchors.fill:   parent
        hudData:        hudDataSource
        drawBackground: root.drawBackground
    }
}
