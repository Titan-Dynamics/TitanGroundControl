import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

Popup {
    id:             control
    modal:          !_requiresInteraction
    focus:          true
    closePolicy:    _requiresInteraction ? Popup.CloseOnEscape : (Popup.CloseOnEscape | Popup.CloseOnPressOutside)
    anchors.centerIn: parent
    padding:        ScreenTools.defaultFontPixelWidth * 2

    property var    guidedController
    property var    guidedValueSlider
    property string title
    property string message
    property int    action
    property var    actionData
    property bool   hideTrigger:        false
    property bool   requiresInteraction: false
    property var    mapIndicator
    property alias  optionText:         optionCheckBox.text
    property alias  optionChecked:      optionCheckBox.checked

    property bool _emergencyAction:      action === guidedController.actionEmergencyStop
    property bool _requiresInteraction:  requiresInteraction || guidedValueSlider.visible

    Component.onCompleted: guidedController.confirmDialog = this

    onHideTriggerChanged: {
        if (hideTrigger) {
            confirmCancelled()
        }
    }

    function show(immediate) {
        if (immediate) {
            _reallyShow()
        } else {
            visibleTimer.restart()
        }
    }

    function reset() {
        close()
        guidedValueSlider.visible = false
        hideTrigger = false
        visibleTimer.stop()
    }

    // Cancel the current pending action and notify its map indicator.
    // Pass incomingIndicator when superseding one action with another (e.g. from confirmAction):
    // if the old and new indicator are the same object, actionCancelled() is intentionally skipped
    // so that a show() call made before confirmAction() is not undone (e.g. goto -> goto).
    // Omit incomingIndicator (or pass undefined) for explicit user cancellation via the Cancel button
    // or auto-hide trigger, where the indicator must always be notified.
    function confirmCancelled(incomingIndicator) {
        reset()
        if (mapIndicator && mapIndicator !== incomingIndicator) {
            mapIndicator.actionCancelled()
        }
        mapIndicator = undefined
    }

    function _reallyShow() {
        dontAskAgainCheckBox.checked = false
        open()
    }

    Timer {
        id:             visibleTimer
        interval:       1000
        repeat:         false
        onTriggered:    _reallyShow()
    }

    QGCPalette { id: qgcPal }

    background: Rectangle {
        color:          qgcPal.window
        radius:         ScreenTools.defaultFontPixelHeight / 2
        border.color:   qgcPal.buttonText
        border.width:   1
        opacity:        0.95
    }

    contentItem: ColumnLayout {
        spacing: ScreenTools.defaultFontPixelHeight / 2

        QGCLabel {
            text:               control.title
            font.pointSize:     ScreenTools.largeFontPointSize
            font.bold:          true
            Layout.fillWidth:   true
        }

        QGCLabel {
            text:                   control.message
            wrapMode:               Text.WordWrap
            Layout.fillWidth:       true
            Layout.preferredWidth:  ScreenTools.defaultFontPixelWidth * 40
            visible:                control.message !== ""
        }

        QGCCheckBox {
            id:         optionCheckBox
            visible:    text !== ""
        }

        QGCCheckBox {
            id:         dontAskAgainCheckBox
            text:       qsTr("Don't show again")
            visible:    !_emergencyAction && !_requiresInteraction
        }

        RowLayout {
            Layout.fillWidth:   true
            spacing:            ScreenTools.defaultFontPixelWidth

            QGCDelayButton {
                objectName: "guidedActionConfirmButton"
                text:       control.title
                enabled:    true

                onActivated: {
                    if (dontAskAgainCheckBox.checked) {
                        guidedController.skipFutureConfirmations(control.action)
                    }
                    control.close()
                    var sliderOutputValue = 0
                    if (guidedValueSlider.visible) {
                        sliderOutputValue = guidedValueSlider.getOutputValue()
                        guidedValueSlider.visible = false
                    }
                    hideTrigger = false
                    let success = guidedController.executeAction(control.action, control.actionData, sliderOutputValue, control.optionChecked)
                    if (mapIndicator) {
                        if (success) {
                            mapIndicator.actionConfirmed()
                        } else {
                            mapIndicator.actionCancelled()
                        }
                        mapIndicator = undefined
                    }
                }
            }

            QGCButton {
                text:       qsTr("Cancel")
                onClicked:  confirmCancelled()
            }
        }
    }
}
