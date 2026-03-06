import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FactControls

SettingsPage {
    QGCPalette { id: qgcPal }

    property var _settingsManager:          QGroundControl.settingsManager
    property var _aiSettings:               _settingsManager.aiSettings
    property Fact _claudeApiKey:            _aiSettings.claudeApiKey
    property Fact _groqApiKey:              _aiSettings.groqApiKey
    property Fact _enableTextToSpeech:      _aiSettings.enableTextToSpeech
    property Fact _confirmDangerousCommands: _aiSettings.confirmDangerousCommands
    property Fact _includeVehicleState:     _aiSettings.includeVehicleState
    property Fact _customContext:           _aiSettings.customContext

    SettingsGroupLayout {
        Layout.fillWidth: true
        heading: qsTr("Claude API Configuration")

        LabelledFactTextField {
            Layout.fillWidth:   true
            label:              qsTr("API Key")
            fact:               _claudeApiKey
            visible:            _claudeApiKey.visible
            textField.echoMode: TextInput.Password
            textField.placeholderText: qsTr("Enter your Claude API key")
            textFieldPreferredWidth: ScreenTools.defaultFontPixelWidth * 40
        }

        QGCLabel {
            Layout.fillWidth:   true
            text:               qsTr("Get your API key from console.anthropic.com")
            wrapMode:           Text.WordWrap
            font.pointSize:     ScreenTools.smallFontPointSize
            color:              qgcPal.text
        }
    }

    SettingsGroupLayout {
        Layout.fillWidth: true
        heading: qsTr("Voice Input (Whisper)")

        LabelledFactTextField {
            Layout.fillWidth:   true
            label:              qsTr("Groq API Key")
            fact:               _groqApiKey
            visible:            _groqApiKey.visible
            textField.echoMode: TextInput.Password
            textField.placeholderText: qsTr("Enter your Groq API key for voice input")
            textFieldPreferredWidth: ScreenTools.defaultFontPixelWidth * 40
        }

        QGCLabel {
            Layout.fillWidth:   true
            text:               qsTr("Get your free API key from console.groq.com. Required for voice commands.")
            wrapMode:           Text.WordWrap
            font.pointSize:     ScreenTools.smallFontPointSize
            color:              qgcPal.text
        }

        FactCheckBoxSlider {
            Layout.fillWidth:   true
            text:               qsTr("Enable Text-to-Speech response")
            fact:               _enableTextToSpeech
            visible:            _enableTextToSpeech.visible
        }

        QGCLabel {
            Layout.fillWidth:   true
            text:               qsTr("When enabled, Titan AI will speak responses aloud.")
            wrapMode:           Text.WordWrap
            font.pointSize:     ScreenTools.smallFontPointSize
            color:              qgcPal.text
        }
    }

    SettingsGroupLayout {
        Layout.fillWidth: true
        heading: qsTr("Safety Settings")

        FactCheckBoxSlider {
            Layout.fillWidth:   true
            text:               qsTr("Require confirmation for dangerous commands")
            fact:               _confirmDangerousCommands
            visible:            _confirmDangerousCommands.visible
        }

        QGCLabel {
            Layout.fillWidth:   true
            text:               qsTr("When enabled, commands like arm, takeoff, and emergency stop will require manual confirmation before execution.")
            wrapMode:           Text.WordWrap
            font.pointSize:     ScreenTools.smallFontPointSize
            color:              qgcPal.text
        }
    }

    SettingsGroupLayout {
        Layout.fillWidth: true
        heading: qsTr("AI Context")

        FactCheckBoxSlider {
            Layout.fillWidth:   true
            text:               qsTr("Include vehicle state in AI context")
            fact:               _includeVehicleState
            visible:            _includeVehicleState.visible
        }

        QGCLabel {
            Layout.fillWidth:   true
            text:               qsTr("When enabled, the AI will receive current vehicle information (position, altitude, armed state, etc.) to provide context-aware responses.")
            wrapMode:           Text.WordWrap
            font.pointSize:     ScreenTools.smallFontPointSize
            color:              qgcPal.text
        }
    }

    SettingsGroupLayout {
        Layout.fillWidth: true
        heading: qsTr("Usage Tips")

        QGCLabel {
            Layout.fillWidth:   true
            text:               qsTr("Example commands you can try:")
            wrapMode:           Text.WordWrap
            font.bold:          true
        }

        QGCLabel {
            Layout.fillWidth:   true
            text:               qsTr("- \"Arm the vehicle\"\n- \"Take off to 15 meters\"\n- \"Head north for 100 meters\"\n- \"Return to launch\"\n- \"Set speed to 10 m/s\"\n- \"Orbit here with 20m radius\"")
            wrapMode:           Text.WordWrap
            font.pointSize:     ScreenTools.smallFontPointSize
        }
    }

    SettingsGroupLayout {
        Layout.fillWidth: true
        heading: qsTr("Custom AI Context")

        QGCLabel {
            Layout.fillWidth:   true
            text:               qsTr("Provide additional context to the AI such as ArduPilot parameter documentation, MAVLink references, or custom instructions. This will be included in every AI request.")
            wrapMode:           Text.WordWrap
            font.pointSize:     ScreenTools.smallFontPointSize
            color:              qgcPal.text
        }

        Rectangle {
            Layout.fillWidth:   true
            Layout.preferredHeight: ScreenTools.defaultFontPixelHeight * 15
            color:              qgcPal.windowShade
            radius:             ScreenTools.defaultFontPixelWidth / 4
            border.color:       customContextArea.activeFocus ? qgcPal.text : "transparent"
            border.width:       1

            ScrollView {
                anchors.fill:       parent
                anchors.margins:    ScreenTools.defaultFontPixelWidth / 2

                TextArea {
                    id:                 customContextArea
                    text:               _customContext.rawValue
                    wrapMode:           TextEdit.Wrap
                    color:              qgcPal.text
                    selectionColor:     qgcPal.textHighlight
                    selectedTextColor:  qgcPal.textHighlightForeground
                    font.family:        ScreenTools.fixedFontFamily
                    font.pointSize:     ScreenTools.smallFontPointSize
                    placeholderText:    qsTr("Paste ArduPilot documentation, parameter lists, or custom instructions here...")
                    placeholderTextColor: Qt.rgba(qgcPal.text.r, qgcPal.text.g, qgcPal.text.b, 0.5)
                    background:         null

                    onTextChanged: {
                        if (text !== _customContext.rawValue) {
                            _customContext.rawValue = text
                        }
                    }
                }
            }
        }

        QGCLabel {
            Layout.fillWidth:   true
            text:               qsTr("Character count: %1").arg(_customContext.rawValue.length)
            font.pointSize:     ScreenTools.smallFontPointSize
            color:              qgcPal.text
        }
    }
}
