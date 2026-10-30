import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FactControls

/// The 3D map settings, as Mission Planner's 3D Map Settings window. Changes apply as they are made.
QGCPopupDialog {
    id:         root
    title:      qsTr("3D Map Settings")
    buttons:    Dialog.Close

    readonly property var  _settings:   QGroundControl.settingsManager.map3DSettings
    readonly property real _fieldWidth: ScreenTools.defaultFontPixelWidth * 14

    function _reset() {
        const facts = [_settings.cameraDistance, _settings.cameraHeight, _settings.cameraAngle, _settings.fieldOfView,
                       _settings.vehicleScale, _settings.waypointMarkerSize, _settings.adsbCircleSize, _settings.cloudDensity,
                       _settings.vehicleColor, _settings.stlPath, _settings.showHeadingLine, _settings.showNavBearingLine,
                       _settings.showGpsHeadingLine, _settings.showTurnRadius, _settings.showTrail, _settings.fpvMode,
                       _settings.smoothMovement, _settings.hudOverlay, _settings.statsForNerds]
        for (const fact of facts) {
            fact.rawValue = fact.defaultValue
        }
    }

    ColumnLayout {
        spacing: ScreenTools.defaultFontPixelHeight / 2

        GridLayout {
            columns:        2
            columnSpacing:  ScreenTools.defaultFontPixelWidth * 2
            rowSpacing:     ScreenTools.defaultFontPixelHeight / 4

            QGCLabel { text: qsTr("Camera distance") }
            FactTextField {
                fact:                   root._settings.cameraDistance
                enabled:                !root._settings.fpvMode.rawValue
                Layout.preferredWidth:  root._fieldWidth
            }

            QGCLabel { text: qsTr("Camera height") }
            FactTextField {
                fact:                   root._settings.cameraHeight
                enabled:                !root._settings.fpvMode.rawValue
                Layout.preferredWidth:  root._fieldWidth
            }

            QGCLabel { text: qsTr("Camera field of view") }
            FactTextField {
                fact:                   root._settings.fieldOfView
                Layout.preferredWidth:  root._fieldWidth
            }

            QGCLabel { text: qsTr("Vehicle scale") }
            FactTextField {
                fact:                   root._settings.vehicleScale
                Layout.preferredWidth:  root._fieldWidth
            }

            QGCLabel { text: qsTr("Waypoint marker size") }
            FactTextField {
                fact:                   root._settings.waypointMarkerSize
                Layout.preferredWidth:  root._fieldWidth
            }

            QGCLabel { text: qsTr("ADS-B circle size") }
            FactTextField {
                fact:                   root._settings.adsbCircleSize
                Layout.preferredWidth:  root._fieldWidth
            }

            QGCLabel { text: qsTr("Cloud density") }
            FactTextField {
                fact:                   root._settings.cloudDensity
                Layout.preferredWidth:  root._fieldWidth
            }

            QGCLabel { text: qsTr("Vehicle color") }
            FactComboBox {
                fact:                   root._settings.vehicleColor
                indexModel:             false
                sizeToContents:         true
            }

            QGCLabel { text: qsTr("Vehicle STL") }
            RowLayout {
                QGCButton {
                    text:       root._settings.stlPath.rawValue === "" ? qsTr("Default") : root._settings.stlPath.rawValue.split(/[\\/]/).pop()
                    onClicked:  stlDialog.openForLoad()
                }
                QGCButton {
                    text:       qsTr("Clear")
                    visible:    root._settings.stlPath.rawValue !== ""
                    onClicked:  root._settings.stlPath.rawValue = ""
                }
            }
        }

        FactCheckBox { text: qsTr("Heading line (red)");            fact: root._settings.showHeadingLine }
        FactCheckBox { text: qsTr("Nav bearing line (orange)");     fact: root._settings.showNavBearingLine }
        FactCheckBox { text: qsTr("GPS heading line (black)");      fact: root._settings.showGpsHeadingLine }
        FactCheckBox { text: qsTr("Turn radius arc (pink)");        fact: root._settings.showTurnRadius }
        FactCheckBox { text: qsTr("Flight path trail");             fact: root._settings.showTrail }
        FactCheckBox { text: qsTr("FPV mode (camera at aircraft)"); fact: root._settings.fpvMode }
        FactCheckBox { text: qsTr("Smooth movement");               fact: root._settings.smoothMovement }
        FactCheckBox { text: qsTr("HUD overlay");                   fact: root._settings.hudOverlay }
        FactCheckBox { text: qsTr("Stats for nerds");               fact: root._settings.statsForNerds }

        QGCButton {
            text:               qsTr("Reset")
            Layout.alignment:   Qt.AlignRight
            onClicked:          root._reset()
        }
    }

    QGCFileDialog {
        id:             stlDialog
        title:          qsTr("Select Vehicle STL File")
        nameFilters:    [qsTr("STL files (*.stl)"), qsTr("All Files (*)")]

        onAcceptedForLoad: (file) => {
            root._settings.stlPath.rawValue = file
            close()
        }
    }
}
