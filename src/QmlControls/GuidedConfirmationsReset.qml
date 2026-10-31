import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FactControls

/// Settings row bringing back the guided action confirmations turned off with "Don't show again".
RowLayout {
    id: root

    readonly property SettingsFact _skipped: QGroundControl.settingsManager.flyViewSettings.skippedGuidedConfirmations as SettingsFact

    spacing: ScreenTools.defaultFontPixelWidth

    QGCLabel {
        Layout.fillWidth:   true
        text:               qsTr("Confirmations turned off with Don't show again")
        wrapMode:           Text.WordWrap
    }

    QGCButton {
        text:       qsTr("Show Again")
        enabled:    root._skipped.rawValue !== ""
        onClicked:  root._skipped.rawValue = ""
    }
}
