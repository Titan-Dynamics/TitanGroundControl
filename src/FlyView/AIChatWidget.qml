import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

import QGroundControl
import QGroundControl.Controls

Rectangle {
    id: aiChatWidget
    width: _chatWidth
    height: _chatHeight
    color: Qt.rgba(qgcPal.window.r, qgcPal.window.g, qgcPal.window.b, 0.9)
    radius: ScreenTools.defaultFontPixelWidth / 2
    visible: _showWidget && _activeVehicle

    property real _margins: ScreenTools.defaultFontPixelHeight / 2
    property real _chatWidth: ScreenTools.defaultFontPixelWidth * 50
    property real _chatHeight: ScreenTools.defaultFontPixelHeight * 22
    property real _minWidth: ScreenTools.defaultFontPixelWidth * 30
    property real _minHeight: ScreenTools.defaultFontPixelHeight * 14
    property real _maxWidth: ScreenTools.defaultFontPixelWidth * 80
    property real _maxHeight: ScreenTools.defaultFontPixelHeight * 40
    property real _dragMargin: ScreenTools.defaultFontPixelWidth
    property var _activeVehicle: globals.activeVehicle
    property var _flyViewSettings: QGroundControl.settingsManager.flyViewSettings
    property var _aiSettings: QGroundControl.settingsManager.aiSettings
    property bool _showWidget: _flyViewSettings.showAIChatWidget.rawValue
    property var _chatController: _activeVehicle ? _activeVehicle.aiChatController : null
    property string _apiKey: _aiSettings && _aiSettings.claudeApiKey ? _aiSettings.claudeApiKey.rawValue.toString() : ""
    property bool _hasApiKey: _apiKey.length > 0

    QGCPalette { id: qgcPal; colorGroupEnabled: enabled }

    DeadMouseArea { anchors.fill: parent }

    // Bottom-left corner resize grip
    Item {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        width: _dragMargin * 3
        height: _dragMargin * 3
        z: 10

        // Grip visual (three diagonal lines)
        Column {
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.margins: _dragMargin * 0.4
            spacing: 2
            opacity: 0.4

            Repeater {
                model: 3
                Rectangle {
                    width: (3 - index) * _dragMargin * 0.5
                    height: 1
                    color: qgcPal.text
                }
            }
        }

        MouseArea {
            id: resizeHandle
            anchors.fill: parent
            cursorShape: Qt.SizeBDiagCursor
            preventStealing: true

            property real _lastScreenX
            property real _lastScreenY
            property real _stepW: ScreenTools.defaultFontPixelWidth
            property real _stepH: ScreenTools.defaultFontPixelHeight
            property real _accumX: 0
            property real _accumY: 0

            onPressed: (mouse) => {
                var sp = mapToGlobal(mouse.x, mouse.y)
                _lastScreenX = sp.x
                _lastScreenY = sp.y
                _accumX = 0
                _accumY = 0
            }

            onPositionChanged: (mouse) => {
                var sp = mapToGlobal(mouse.x, mouse.y)
                _accumX += (sp.x - _lastScreenX)
                _accumY += (sp.y - _lastScreenY)
                _lastScreenX = sp.x
                _lastScreenY = sp.y

                // Only resize in discrete steps
                if (Math.abs(_accumX) >= _stepW) {
                    var stepsX = Math.trunc(_accumX / _stepW)
                    _chatWidth = Math.max(_minWidth, Math.min(_maxWidth, _chatWidth - stepsX * _stepW))
                    _accumX -= stepsX * _stepW
                }
                if (Math.abs(_accumY) >= _stepH) {
                    var stepsY = Math.trunc(_accumY / _stepH)
                    _chatHeight = Math.max(_minHeight, Math.min(_maxHeight, _chatHeight + stepsY * _stepH))
                    _accumY -= stepsY * _stepH
                }
            }
        }
    }

    ColumnLayout {
        id: contentColumn
        anchors.fill: parent
        anchors.leftMargin: _margins
        anchors.topMargin: _margins / 3
        anchors.rightMargin: _margins
        anchors.bottomMargin: _margins
        spacing: _margins / 2

        // Header
        RowLayout {
            Layout.fillWidth: true
            spacing: _margins

            QGCLabel {
                text: qsTr("Titan AI")
                font.bold: true
                Layout.fillWidth: true
            }

            Rectangle {
                width: ScreenTools.defaultFontPixelHeight * 1.8
                height: width
                radius: width / 2
                color: clearMouseArea.containsMouse ? qgcPal.windowShade : "transparent"
                opacity: clearMouseArea.enabled ? 1.0 : 0.3

                QGCColoredImage {
                    anchors.centerIn: parent
                    width: ScreenTools.defaultFontPixelHeight * 0.9
                    height: width
                    source: "/res/TrashDelete.svg"
                    color: qgcPal.text
                }

                MouseArea {
                    id: clearMouseArea
                    anchors.fill: parent
                    hoverEnabled: true
                    enabled: _chatController && _chatController.messages && _chatController.messages.count > 0
                    onClicked: _chatController.clearHistory()
                }
            }
        }

        // API Key missing warning
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: apiKeyWarning.implicitHeight + _margins
            color: Qt.rgba(qgcPal.warningText.r, qgcPal.warningText.g, qgcPal.warningText.b, 0.2)
            radius: ScreenTools.defaultFontPixelWidth / 4
            visible: !_hasApiKey

            QGCLabel {
                id: apiKeyWarning
                anchors.centerIn: parent
                width: parent.width - _margins
                text: qsTr("API key not configured. Go to Application Settings > Titan AI to enter your Claude API key.")
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
                color: qgcPal.warningText
            }
        }

        // Message list
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: ScreenTools.defaultFontPixelHeight * 8
            color: Qt.rgba(qgcPal.windowShade.r, qgcPal.windowShade.g, qgcPal.windowShade.b, 0.5)
            radius: ScreenTools.defaultFontPixelWidth / 4
            clip: true

            QGCListView {
                id: messageList
                anchors.fill: parent
                anchors.margins: ScreenTools.defaultFontPixelWidth / 2
                anchors.bottomMargin: typingIndicator.visible ? typingIndicator.height + ScreenTools.defaultFontPixelWidth : ScreenTools.defaultFontPixelWidth / 2
                model: _chatController ? _chatController.messages : null
                spacing: ScreenTools.defaultFontPixelHeight / 2
                verticalLayoutDirection: ListView.TopToBottom

                delegate: Item {
                    id: messageDelegate
                    width: messageList.width
                    height: messageDelegate._isUser ? userText.implicitHeight : messageBg.height
                    visible: object !== null && object !== undefined

                    property int safeRole: object ? object.role : 1
                    property string safeContent: object ? (object.content || "") : ""
                    property string safeAction: object ? (object.action || "") : ""
                    property int safeStatus: object ? (object.commandStatus || 0) : 0
                    property int safeActionCount: object ? (object.actionCount || 0) : 0
                    property int safeExecutedCount: object ? (object.executedActionCount || 0) : 0
                    property bool _isUser: safeRole === 0

                    // User message - right-aligned plain text, no bubble
                    TextEdit {
                        id: userText
                        anchors.right: parent.right
                        width: Math.min(implicitWidth, parent.width * 0.85)
                        visible: messageDelegate._isUser
                        text: messageDelegate.safeContent
                        wrapMode: TextEdit.Wrap
                        readOnly: true
                        selectByMouse: true
                        horizontalAlignment: TextEdit.AlignRight
                        color: qgcPal.text
                        selectionColor: qgcPal.textHighlight
                        selectedTextColor: qgcPal.textHighlightForeground
                        font.family: ScreenTools.normalFontFamily
                        font.pointSize: ScreenTools.defaultFontPointSize
                    }

                    // AI/System message - left-aligned bubble
                    Rectangle {
                        id: messageBg
                        visible: !messageDelegate._isUser
                        width: Math.min(messageColumn.implicitWidth + ScreenTools.defaultFontPixelWidth * 3, parent.width * 0.85)
                        height: messageColumn.implicitHeight + aiChatWidget._margins * 1.5
                        anchors.left: parent.left
                        color: qgcPal.windowShadeDark
                        radius: ScreenTools.defaultFontPixelWidth * 1.5

                        ColumnLayout {
                            id: messageColumn
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.margins: ScreenTools.defaultFontPixelWidth * 1.5
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: ScreenTools.defaultFontPixelHeight / 4

                            // Message content (selectable for copy/paste)
                            TextEdit {
                                Layout.fillWidth: true
                                text: messageDelegate.safeContent
                                wrapMode: TextEdit.Wrap
                                readOnly: true
                                selectByMouse: true
                                color: qgcPal.text
                                selectionColor: qgcPal.textHighlight
                                selectedTextColor: qgcPal.textHighlightForeground
                                font.family: ScreenTools.normalFontFamily
                                font.pointSize: ScreenTools.defaultFontPointSize
                            }

                            // Command execution button
                            RowLayout {
                                Layout.fillWidth: true
                                visible: messageDelegate.safeActionCount > 0 && messageDelegate.safeStatus === 2
                                spacing: aiChatWidget._margins / 2

                                QGCButton {
                                    text: messageDelegate.safeActionCount > 1
                                        ? qsTr("Execute %1 actions").arg(messageDelegate.safeActionCount)
                                        : qsTr("Execute: %1").arg(messageDelegate.safeAction)
                                    highlighted: true
                                    onClicked: _chatController.executeCommand(index)
                                }

                                QGCLabel {
                                    text: qsTr("(Requires confirmation)")
                                    font.pointSize: ScreenTools.smallFontPointSize
                                    color: qgcPal.warningText
                                }
                            }

                            // Execution progress (status 1 = Pending/Executing)
                            RowLayout {
                                Layout.fillWidth: true
                                visible: messageDelegate.safeActionCount > 0 && messageDelegate.safeStatus === 1
                                spacing: aiChatWidget._margins / 2

                                BusyIndicator {
                                    Layout.preferredWidth: ScreenTools.defaultFontPixelHeight
                                    Layout.preferredHeight: ScreenTools.defaultFontPixelHeight
                                    running: parent.visible
                                }

                                QGCLabel {
                                    text: messageDelegate.safeActionCount > 1
                                        ? qsTr("Executing: %1 of %2").arg(messageDelegate.safeExecutedCount + 1).arg(messageDelegate.safeActionCount)
                                        : qsTr("Executing...")
                                    font.pointSize: ScreenTools.smallFontPointSize
                                    color: qgcPal.text
                                }
                            }

                            // Command status (completed)
                            QGCLabel {
                                visible: messageDelegate.safeStatus === 3
                                text: messageDelegate.safeActionCount > 1
                                    ? qsTr("%1 commands executed").arg(messageDelegate.safeActionCount)
                                    : qsTr("Command executed")
                                font.pointSize: ScreenTools.smallFontPointSize
                                color: qgcPal.colorGreen
                            }

                            QGCLabel {
                                visible: messageDelegate.safeStatus === 4
                                text: qsTr("Command failed")
                                font.pointSize: ScreenTools.smallFontPointSize
                                color: qgcPal.colorRed
                            }

                            QGCLabel {
                                visible: messageDelegate.safeStatus === 5
                                text: qsTr("Cancelled")
                                font.pointSize: ScreenTools.smallFontPointSize
                                color: qgcPal.warningText
                            }
                        }
                    }
                }

                onCountChanged: {
                    // Auto-scroll to bottom on new messages
                    Qt.callLater(function() {
                        messageList.positionViewAtEnd()
                    })
                }
            }

            // Typing indicator - three pulsing dots
            Rectangle {
                id: typingIndicator
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.margins: ScreenTools.defaultFontPixelWidth / 2
                width: dotRow.width + ScreenTools.defaultFontPixelWidth * 2
                height: dotRow.height + aiChatWidget._margins
                radius: ScreenTools.defaultFontPixelWidth * 1.5
                color: qgcPal.windowShadeDark
                visible: _chatController && _chatController.isProcessing
                onVisibleChanged: if (visible) Qt.callLater(function() { messageList.positionViewAtEnd() })

                Row {
                    id: dotRow
                    anchors.centerIn: parent
                    spacing: ScreenTools.defaultFontPixelWidth / 2

                    Repeater {
                        model: 3
                        Rectangle {
                            width: ScreenTools.defaultFontPixelHeight / 3
                            height: width
                            radius: width / 2
                            color: qgcPal.text

                            SequentialAnimation on opacity {
                                running: typingIndicator.visible
                                loops: Animation.Infinite
                                PauseAnimation { duration: index * 200 }
                                NumberAnimation { from: 0.3; to: 1.0; duration: 400; easing.type: Easing.InOutQuad }
                                NumberAnimation { from: 1.0; to: 0.3; duration: 400; easing.type: Easing.InOutQuad }
                                PauseAnimation { duration: (2 - index) * 200 }
                            }
                        }
                    }
                }
            }
        }

        // Error message
        QGCLabel {
            Layout.fillWidth: true
            visible: _chatController && _chatController.errorMessage && _chatController.errorMessage.length > 0
            text: _chatController && _chatController.errorMessage ? _chatController.errorMessage : ""
            wrapMode: Text.WordWrap
            color: qgcPal.warningText
            font.pointSize: ScreenTools.smallFontPointSize
        }

        // Input area
        RowLayout {
            Layout.fillWidth: true
            spacing: _margins / 2

            QGCTextField {
                id: inputField
                Layout.fillWidth: true
                Layout.preferredHeight: ScreenTools.defaultFontPixelHeight * 2.5
                placeholderText: _chatController && _chatController.isListening ? qsTr("Listening...") : qsTr("Type a command...")
                enabled: _chatController && !_chatController.isProcessing && !_chatController.isListening
                color: qgcPal.text
                placeholderTextColor: Qt.rgba(qgcPal.text.r, qgcPal.text.g, qgcPal.text.b, 0.5)
                onAccepted: sendMessage()

                background: Rectangle {
                    radius: ScreenTools.defaultFontPixelWidth / 4
                    color: qgcPal.windowShade
                    border.width: inputField.activeFocus ? 1 : 0
                    border.color: _chatController && _chatController.isListening ? qgcPal.colorRed : qgcPal.text
                }
            }

            // Microphone button for voice input
            Rectangle {
                id: micButton
                Layout.preferredWidth: ScreenTools.defaultFontPixelHeight * 2.5
                Layout.preferredHeight: ScreenTools.defaultFontPixelHeight * 2.5
                radius: ScreenTools.defaultFontPixelWidth / 4
                color: _chatController && _chatController.isListening ? qgcPal.colorRed : qgcPal.windowShade
                visible: _chatController && _chatController.voiceInputAvailable
                opacity: _chatController && _chatController.isProcessing ? 0.4 : 1.0

                QGCColoredImage {
                    anchors.centerIn: parent
                    width: ScreenTools.defaultFontPixelHeight * 1.1
                    height: width
                    source: "/res/mic.svg"
                    color: qgcPal.text
                }

                MouseArea {
                    anchors.fill: parent
                    enabled: _chatController && !_chatController.isProcessing
                    onClicked: {
                        if (_chatController.isListening) {
                            _chatController.stopListening()
                        } else {
                            inputField.text = ""
                            _chatController.startListening()
                        }
                    }
                }
            }

            Rectangle {
                Layout.preferredWidth: ScreenTools.defaultFontPixelHeight * 2.5
                Layout.preferredHeight: ScreenTools.defaultFontPixelHeight * 2.5
                radius: ScreenTools.defaultFontPixelWidth / 4
                color: inputField.text.trim().length > 0 && !(_chatController && _chatController.isProcessing) ? qgcPal.buttonHighlight : qgcPal.windowShade
                opacity: _chatController && _chatController.isProcessing ? 0.4 : 1.0

                QGCColoredImage {
                    anchors.centerIn: parent
                    width: ScreenTools.defaultFontPixelHeight * 1.1
                    height: width
                    source: _chatController && _chatController.isListening ? "/res/XDelete.svg" : "/res/send.svg"
                    color: qgcPal.text
                }

                MouseArea {
                    id: sendMouseArea
                    anchors.fill: parent
                    enabled: _chatController && !_chatController.isProcessing && (_chatController.isListening || inputField.text.trim().length > 0)
                    onClicked: {
                        if (_chatController.isListening) {
                            _chatController.cancelListening()
                        } else {
                            sendMessage()
                        }
                    }
                }
            }
        }
    }

    function sendMessage() {
        if (inputField.text.trim().length > 0 && _chatController) {
            _chatController.sendMessage(inputField.text.trim())
            inputField.text = ""
        }
    }
}
