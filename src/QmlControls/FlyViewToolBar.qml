import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FactControls

Item {
    id:     control
    width:  parent.width
    height: ScreenTools.toolbarHeight

    property var    _activeVehicle:     QGroundControl.multiVehicleManager.activeVehicle
    property bool   _communicationLost: _activeVehicle ? _activeVehicle.vehicleLinkManager.communicationLost : false
    property color  _mainStatusBGColor: qgcPal.brandingPurple
    property bool   _armed:             _activeVehicle ? _activeVehicle.armed : false
    property bool   _healthAndArmingChecksSupported: _activeVehicle ? _activeVehicle.healthAndArmingCheckReport.supported : false
    property bool   _parametersReady: QGroundControl.multiVehicleManager.parameterReadyVehicleAvailable
    property var    _fltmodeNames:   (_activeVehicle && _activeVehicle.apmFirmware && _parametersReady) ? _buildFltmodeList() : []

    function dropMainStatusIndicatorTool() {
        mainStatusIndicator.dropMainStatusIndicator();
    }

    function _buildFltmodeList() {
        var controller = factControllerComponent.createObject(control)
        if (!controller) return []

        var isRover = controller.parameterExists(-1, "MODE1")
        var prefix = isRover ? "MODE" : "FLTMODE"

        if (!controller.parameterExists(-1, prefix + "1")) {
            controller.destroy()
            return []
        }

        var modes = []
        var seen = {}
        for (var i = 1; i <= 6; i++) {
            var paramName = prefix + i
            if (controller.parameterExists(-1, paramName)) {
                var fact = controller.getParameterFact(-1, paramName, false)
                if (fact) {
                    var name = fact.enumStringValue
                    if (name && name !== "" && !seen[name]) {
                        seen[name] = true
                        modes.push(name)
                    }
                }
            }
        }

        controller.destroy()
        return modes
    }

    QGCPalette { id: qgcPal }

    Component { id: factControllerComponent; FactPanelController {} }

    // Full-width toolbar background
    Rectangle {
        anchors.fill:   parent
        color:          qgcPal.windowTransparent
    }

    // Right panel docked to the right edge, outside the flickable
    Item {
        id:             rightPanel
        anchors.right:  parent.right
        anchors.top:    parent.top
        width:          flyViewIndicators.width
        height:         parent.height

        FlyViewToolBarIndicators {
            id:     flyViewIndicators
            height: parent.height
        }
    }

    // Scrollable area for everything else
    QGCFlickable {
        anchors.left:       parent.left
        anchors.right:      rightPanel.left
        anchors.top:        parent.top
        anchors.bottom:     parent.bottom
        contentWidth:       leftPanel.width
        flickableDirection:  Flickable.HorizontalFlick

        Item {
            id:     leftPanel
            width:  leftPanelLayout.implicitWidth
            height: parent.height

            // Gradient background behind Q button and main status indicator
            Rectangle {
                id:         gradientBackground
                height:     parent.height
                width:      mainStatusLayout.width
                opacity:    qgcPal.windowTransparent.a

                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: _mainStatusBGColor }
                    GradientStop { position: 1; color: "transparent" }
                }
            }



            RowLayout {
                id:         leftPanelLayout
                height:     parent.height
                spacing:    ScreenTools.defaultFontPixelWidth * 2

                RowLayout {
                    id:         mainStatusLayout
                    height:     parent.height
                    spacing:    0

                    QGCToolBarButton {
                        id:                 qgcButton
                        Layout.fillHeight:  true
                        icon.source:        "/res/QGCLogoFull.png"
                        logo:               true
                        onClicked:          mainWindow.showToolSelectDialog()
                    }

                    MainStatusIndicator {
                        id:                 mainStatusIndicator
                        Layout.fillHeight:  true
                    }
                }

                QGCDelayButton {
                    id:                     armButton
                    Layout.alignment:       Qt.AlignVCenter
                    Layout.preferredWidth:  Math.max(armTextMetrics.width, disarmTextMetrics.width) + _horizontalPadding * 2
                    text:                   _armed ? qsTr("Disarm") : qsTr("Arm")
                    enabled:                _armed || !_healthAndArmingChecksSupported || _activeVehicle.healthAndArmingCheckReport.canArm
                    visible:                _activeVehicle
                    backgroundColor:        enabled ? "#b33" : qgcPal.button
                    textColor:              enabled ? "white" : qgcPal.buttonText
                    onActivated: {
                        if (_armed) {
                            _activeVehicle.armed = false
                        } else {
                            _activeVehicle.armed = true
                        }
                        armResetTimer.start()
                    }

                    Timer {
                        id:         armResetTimer
                        interval:   150
                        onTriggered: armButton.progress = 0
                    }

                    TextMetrics {
                        id:     armTextMetrics
                        font:   armButton.font
                        text:   qsTr("Arm")
                    }
                    TextMetrics {
                        id:     disarmTextMetrics
                        font:   armButton.font
                        text:   qsTr("Disarm")
                    }
                }

                QGCColoredImage {
                    id:                     toolbarMessagesIcon
                    Layout.alignment:       Qt.AlignVCenter
                    height:                 ScreenTools.defaultFontPixelHeight * 1.5
                    width:                  height
                    source:                 "/res/VehicleMessages.png"
                    sourceSize.width:       width
                    fillMode:               Image.PreserveAspectFit
                    color:                  getMessageIconColor()
                    visible:                _activeVehicle

                    function getMessageIconColor() {
                        if (_activeVehicle) {
                            if (_activeVehicle.messageTypeError) return qgcPal.colorRed
                            if (_activeVehicle.messageTypeWarning) return qgcPal.colorOrange
                        }
                        return qgcPal.text
                    }

                    QGCMouseArea {
                        anchors.fill:   parent
                        onClicked:      mainWindow.showIndicatorDrawer(vehicleMessagesIndicatorPage, toolbarMessagesIcon)
                    }
                }

                QGCButton {
                    id:         disconnectButton
                    text:       qsTr("Disconnect")
                    onClicked:  _activeVehicle.closeVehicle()
                    visible:    _activeVehicle && _communicationLost
                }

                FlightModeIndicator {
                    Layout.fillHeight:  true
                    visible:            _activeVehicle
                }

                Repeater {
                    model: _fltmodeNames

                    QGCDelayButton {
                        Layout.alignment:   Qt.AlignVCenter
                        text:               modelData
                        visible:            _activeVehicle
                        onActivated: {
                            _activeVehicle.flightMode = modelData
                            fltmodeResetTimer.start()
                        }

                        Timer {
                            id:         fltmodeResetTimer
                            interval:   150
                            onTriggered: parent.progress = 0
                        }
                    }
                }
            }
        }
    }

    ParameterDownloadProgress {
        anchors.fill: parent
    }

    Component {
        id: vehicleMessagesIndicatorPage

        ToolIndicatorPage {
            contentComponent: vehicleMessagesContentComponent
        }
    }

    Component {
        id: vehicleMessagesContentComponent

        ColumnLayout {
            spacing: ScreenTools.defaultFontPixelWidth / 2

            SettingsGroupLayout {
                heading: qsTr("Vehicle Messages")

                VehicleMessageList { }
            }
        }
    }
}
