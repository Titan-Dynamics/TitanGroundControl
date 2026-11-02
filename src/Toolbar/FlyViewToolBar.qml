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
    property color  _mainStatusBGColor: qgcPal.brandPrimary
    property bool   _armed:             _activeVehicle ? _activeVehicle.armed : false
    property bool   _healthAndArmingChecksSupported: _activeVehicle ? _activeVehicle.healthAndArmingCheckReport.supported : false
    property bool   _parametersReady: QGroundControl.multiVehicleManager.parameterReadyVehicleAvailable
    property var    _fltmodeNames:   (_activeVehicle && _activeVehicle.apmFirmware && _parametersReady) ? _buildFltmodeList() : []

    // Titan: every indicator follows the flight modes, leaving the right end to the logo: GPS, battery and RC first,
    // then the rest as the app and the firmware list them. GPS is one of the app's indicators (it also shows a GNSS
    // receiver without a vehicle), the others are the vehicle's.
    readonly property var  _leadingIndicators: [ "/GPSIndicator.qml", "/BatteryIndicator.qml", "/RCRSSIIndicator.qml" ]
    readonly property var  _indicators:        _orderIndicators(QGroundControl.corePlugin.toolBarIndicators,
                                                                _activeVehicle ? _activeVehicle.toolIndicators : [])
    readonly property real _indicatorMargins:  ScreenTools.defaultFontPixelHeight * 0.66

    // The Titan Dynamics logo at the right end, in the theme's color, while the toolbar has room for it
    readonly property real _logoHeight:      ScreenTools.toolbarHeight * 0.7
    readonly property real _logoWidth:       _logoHeight * 4.5   // The images are 1800 x 400
    readonly property real _logoMargin:      ScreenTools.defaultFontPixelWidth * 1.5
    readonly property bool _showLogo:        width >= leftPanelLayout.implicitWidth + _logoWidth + _logoMargin * 2

    function _orderIndicators(appIndicators, vehicleIndicators) {
        const all = Array.from(appIndicators).concat(Array.from(vehicleIndicators))
        const leadIndex = url => _leadingIndicators.findIndex(name => url.toString().endsWith(name))
        const leading = all.filter(url => leadIndex(url) >= 0).sort((a, b) => leadIndex(a) - leadIndex(b))
        return leading.concat(all.filter(url => leadIndex(url) < 0))
    }

    function dropMainStatusIndicatorTool(showMessages = false) {
        mainStatusIndicator.dropMainStatusIndicator(showMessages);
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
                    // Use the firmware plugin's canonical name (e.g. "FBW A")
                    // instead of the parameter metadata name (e.g. "FBWA")
                    var modeNum = fact.rawValue
                    var name = _activeVehicle.flightModeFromCustomMode(modeNum)
                    if (name && name !== "" && name !== "Unknown" && !seen[name]) {
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

    // Right panel docked to the right edge, outside the flickable. Titan: only the logo; indicators are on the left.
    Item {
        id:             rightPanel
        anchors.right:  parent.right
        anchors.top:    parent.top
        width:          control._showLogo ? control._logoWidth + control._logoMargin * 2 : 0
        height:         parent.height

        // Titan: clicks on the logo stop here rather than reaching the map
        DeadMouseArea {
            anchors.fill: parent
        }

        Image {
            anchors.right:          parent.right
            anchors.rightMargin:    control._logoMargin
            anchors.verticalCenter: parent.verticalCenter
            width:                  control._logoWidth
            height:                 control._logoHeight
            source:                 qgcPal.globalTheme === QGCPalette.Light ? "/res/td_full_logo_black.png" : "/res/td_full_logo_white.png"
            sourceSize.height:      height
            fillMode:               Image.PreserveAspectFit
            mipmap:                 true
            visible:                control._showLogo
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
                        objectName:         "toolbar_qgcLogo"
                        Layout.fillHeight:  true
                        icon.source:        "/res/QGCLogoFull.png"
                        logo:               true
                        onClicked:          mainWindow.showToolSelectDialog()
                    }

                    MainStatusIndicator {
                        id:                 mainStatusIndicator
                        objectName:         "toolbar_mainStatusIndicator"
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
                            if (_activeVehicle.unreadCriticalMessageCount > 0) return qgcPal.colorRed
                            if (_activeVehicle.unreadWarningMessageCount > 0) return qgcPal.colorOrange
                        }
                        return qgcPal.text
                    }

                    QGCMouseArea {
                        anchors.fill:   parent
                        onClicked:      dropMainStatusIndicatorTool(true /* showMessages */)
                    }

                    SeverityPill {
                        objectName:                 "toolbarMessagesSeverityBadge"
                        anchors.horizontalCenter:   parent.horizontalCenter
                        anchors.verticalCenter:     parent.top
                        errorCount:                 _activeVehicle ? _activeVehicle.unreadCriticalMessageCount : 0
                        warningCount:               _activeVehicle ? _activeVehicle.unreadWarningMessageCount : 0
                        unreadCount:                _activeVehicle ? _activeVehicle.unreadMessageCount : 0
                    }
                }

                QGCButton {
                    id:         disconnectButton
                    text:       qsTr("Disconnect")
                    onClicked:  _activeVehicle.closeVehicle()
                    visible:    _activeVehicle && _communicationLost
                }

                FlightModeIndicator {
                    objectName:         "toolbar_flightModeIndicator"
                    Layout.fillHeight:  true
                    visible:            _activeVehicle
                }

                Repeater {
                    model: _fltmodeNames

                    QGCDelayButton {
                        Layout.alignment:   Qt.AlignVCenter
                        text:               TDHelpers.displayModeName(modelData)
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

                // Titan: the indicators, right after the flight modes
                Row {
                    Layout.fillHeight:  true
                    spacing:            ScreenTools.defaultFontPixelWidth * 1.75

                    Repeater {
                        model: control._indicators

                        Loader {
                            anchors.top:            parent.top
                            anchors.bottom:         parent.bottom
                            anchors.topMargin:      control._indicatorMargins
                            anchors.bottomMargin:   control._indicatorMargins
                            source:                 modelData
                            visible:                item ? item.showIndicator : false
                        }
                    }
                }
            }
        }
    }

    ParameterDownloadProgress {
        anchors.fill: parent
    }
}
