import QtQuick

import QGroundControl
import QGroundControl.Controls

/// Instrument-panel variant of the Mission Planner HUD (Fly View settings > Instrument panel):
/// a 4:3 panel with its own sky and ground, sized like the other instrument widgets.
Rectangle {
    id:     control
    width:  Math.min(mainWindow.width * 0.25, ScreenTools.defaultFontPixelHeight * 20)
    height: width * 3 / 4
    radius: ScreenTools.defaultFontPixelHeight / 2
    color:  qgcPal.window
    clip:   true

    property real extraInset:       0
    property real extraValuesWidth: 0

    QGCPalette { id: qgcPal }

    DeadMouseArea { anchors.fill: parent }

    TitanHud {
        anchors.fill:   parent
        drawBackground: true
    }
}
