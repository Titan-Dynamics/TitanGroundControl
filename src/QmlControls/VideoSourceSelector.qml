import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FactControls

/// Titan: the Video settings page's source dropdown with a Refresh button, which lists the cameras again for a USB
/// camera plugged in while the app runs. The list also refreshes itself when the system reports a camera arriving or
/// leaving; the button is for systems that do not.
ColumnLayout {
    id:         root
    spacing:    ScreenTools.defaultFontPixelHeight / 4

    readonly property var _videoSettings:   QGroundControl.settingsManager.videoSettings
    readonly property var _sourceFact:      _videoSettings.videoSource

    RowLayout {
        Layout.fillWidth:   true
        spacing:            ScreenTools.defaultFontPixelWidth

        LabelledFactComboBox {
            objectName:         "settingsComboBox_videoSource"
            Layout.fillWidth:   true
            label:              root._sourceFact.label
            fact:               root._sourceFact
            indexModel:         false
        }

        QGCButton {
            objectName: "settingsButton_refreshVideoSources"
            text:       qsTr("Refresh")
            onClicked:  root._videoSettings.refreshVideoSources()
        }
    }

    QGCLabel {
        Layout.fillWidth:   true
        text:               root._sourceFact.shortDescription
        visible:            text !== ""
        font.pointSize:     ScreenTools.smallFontPointSize
        wrapMode:           Text.WordWrap
    }
}
