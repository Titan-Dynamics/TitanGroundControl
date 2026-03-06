import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

import QGroundControl
import QGroundControl.Controls

Rectangle {
    id: aiChatWidget
    width: ScreenTools.defaultFontPixelWidth * 50
    height: _userHeight
    color: Qt.rgba(qgcPal.window.r, qgcPal.window.g, qgcPal.window.b, 0.9)
    radius: ScreenTools.defaultFontPixelWidth / 2
    visible: _showWidget && _activeVehicle

    property real _margins: ScreenTools.defaultFontPixelHeight / 2
    property real _minHeight: ScreenTools.defaultFontPixelHeight * 18
    property real _maxHeight: ScreenTools.defaultFontPixelHeight * 45
    property real _userHeight: ScreenTools.defaultFontPixelHeight * 22  // Default height
    property var _activeVehicle: globals.activeVehicle
    property var _flyViewSettings: QGroundControl.settingsManager.flyViewSettings
    property var _aiSettings: QGroundControl.settingsManager.aiSettings
    property bool _showWidget: _flyViewSettings.showAIChatWidget.rawValue
    property var _chatController: _activeVehicle ? _activeVehicle.aiChatController : null
    property string _apiKey: _aiSettings && _aiSettings.claudeApiKey ? _aiSettings.claudeApiKey.rawValue.toString() : ""
    property bool _hasApiKey: _apiKey.length > 0

    QGCPalette { id: qgcPal; colorGroupEnabled: enabled }

    DeadMouseArea { anchors.fill: parent }

    // Resize handle at bottom
    Rectangle {
        id: resizeHandle
        anchors.bottom: parent.bottom
        anchors.bottomMargin: _margins / 2
        anchors.horizontalCenter: parent.horizontalCenter
        width: ScreenTools.defaultFontPixelWidth * 8
        height: ScreenTools.defaultFontPixelHeight * 0.5
        radius: height / 2
        color: resizeMouseArea.containsMouse || resizeMouseArea.pressed ? qgcPal.text : Qt.rgba(qgcPal.text.r, qgcPal.text.g, qgcPal.text.b, 0.4)

        MouseArea {
            id: resizeMouseArea
            anchors.fill: parent
            anchors.margins: -ScreenTools.defaultFontPixelHeight / 2  // Larger hit area
            hoverEnabled: true
            cursorShape: Qt.SizeVerCursor

            property real startMouseY
            property real startHeight

            onPressed: function(mouse) {
                var globalPos = mapToItem(aiChatWidget.parent, mouse.x, mouse.y)
                startMouseY = globalPos.y
                startHeight = aiChatWidget._userHeight
            }

            onPositionChanged: function(mouse) {
                if (pressed) {
                    var globalPos = mapToItem(aiChatWidget.parent, mouse.x, mouse.y)
                    var delta = globalPos.y - startMouseY
                    var newHeight = startHeight + delta
                    aiChatWidget._userHeight = Math.max(aiChatWidget._minHeight, Math.min(aiChatWidget._maxHeight, newHeight))
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
        anchors.bottomMargin: _margins + resizeHandle.height + _margins
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

            QGCButton {
                text: qsTr("Clear")
                pointSize: ScreenTools.smallFontPointSize
                enabled: _chatController && _chatController.messages && _chatController.messages.count > 0
                onClicked: _chatController.clearHistory()
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
            color: qgcPal.windowShade
            radius: ScreenTools.defaultFontPixelWidth / 4
            clip: true

            QGCListView {
                id: messageList
                anchors.fill: parent
                anchors.margins: ScreenTools.defaultFontPixelWidth / 2
                model: _chatController ? _chatController.messages : null
                spacing: ScreenTools.defaultFontPixelHeight / 4
                verticalLayoutDirection: ListView.TopToBottom

                delegate: Item {
                    id: messageDelegate
                    width: messageList.width
                    height: messageBg.height
                    visible: object !== null && object !== undefined

                    property int safeRole: object ? object.role : 1
                    property string safeContent: object ? (object.content || "") : ""
                    property string safeAction: object ? (object.action || "") : ""
                    property int safeStatus: object ? (object.commandStatus || 0) : 0

                    Rectangle {
                        id: messageBg
                        width: parent.width
                        height: messageColumn.implicitHeight + (aiChatWidget._margins / 2)
                        color: messageDelegate.safeRole === 0 ? qgcPal.windowShadeDark : "transparent"
                        radius: ScreenTools.defaultFontPixelWidth / 4

                        ColumnLayout {
                            id: messageColumn
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.margins: ScreenTools.defaultFontPixelWidth / 2
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: ScreenTools.defaultFontPixelHeight / 4

                            // Role label
                            QGCLabel {
                                text: {
                                    if (messageDelegate.safeRole === 0) return qsTr("You:")
                                    if (messageDelegate.safeRole === 1) return qsTr("Titan AI:")
                                    return qsTr("System:")
                                }
                                font.bold: true
                                font.pointSize: ScreenTools.smallFontPointSize
                                color: messageDelegate.safeRole === 2 ? qgcPal.warningText : qgcPal.text
                            }

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
                                visible: messageDelegate.safeAction.length > 0 && messageDelegate.safeStatus === 2
                                spacing: aiChatWidget._margins / 2

                                QGCButton {
                                    text: qsTr("Execute: %1").arg(messageDelegate.safeAction)
                                    highlighted: true
                                    onClicked: _chatController.executeCommand(index)
                                }

                                QGCLabel {
                                    text: qsTr("(Requires confirmation)")
                                    font.pointSize: ScreenTools.smallFontPointSize
                                    color: qgcPal.warningText
                                }
                            }

                            // Command status
                            QGCLabel {
                                visible: messageDelegate.safeStatus === 3
                                text: qsTr("Command executed")
                                font.pointSize: ScreenTools.smallFontPointSize
                                color: qgcPal.colorGreen
                            }

                            QGCLabel {
                                visible: messageDelegate.safeStatus === 4
                                text: qsTr("Command failed")
                                font.pointSize: ScreenTools.smallFontPointSize
                                color: qgcPal.colorRed
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

        // Processing indicator
        RowLayout {
            Layout.fillWidth: true
            visible: _chatController && _chatController.isProcessing
            spacing: _margins / 2

            BusyIndicator {
                Layout.preferredWidth: ScreenTools.defaultFontPixelHeight
                Layout.preferredHeight: ScreenTools.defaultFontPixelHeight
                running: parent.visible
            }

            QGCLabel {
                text: qsTr("Processing...")
                font.pointSize: ScreenTools.smallFontPointSize
            }
        }

        // Input area
        RowLayout {
            Layout.fillWidth: true
            spacing: _margins / 2

            QGCTextField {
                id: inputField
                Layout.fillWidth: true
                Layout.preferredHeight: ScreenTools.defaultFontPixelHeight * 2.5
                placeholderText: qsTr("Type a command...")
                enabled: _chatController && !_chatController.isProcessing
                color: qgcPal.text
                placeholderTextColor: Qt.rgba(qgcPal.text.r, qgcPal.text.g, qgcPal.text.b, 0.5)
                onAccepted: sendMessage()

                background: Rectangle {
                    radius: ScreenTools.defaultFontPixelWidth / 4
                    color: qgcPal.windowShade
                    border.width: inputField.activeFocus ? 1 : 0
                    border.color: qgcPal.text
                }
            }

            QGCButton {
                text: qsTr("Send")
                Layout.preferredHeight: ScreenTools.defaultFontPixelHeight * 2.5
                enabled: inputField.text.trim().length > 0 && _chatController && !_chatController.isProcessing
                onClicked: sendMessage()
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
