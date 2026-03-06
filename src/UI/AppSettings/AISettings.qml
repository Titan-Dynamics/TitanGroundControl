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
            text:               qsTr("- \"Arm the vehicle\"\n- \"Take off to 15 meters\"\n- \"Go to latitude 37.7749, longitude -122.4194\"\n- \"Return to launch\"\n- \"Land now\"\n- \"Change altitude by +10 meters\"")
            wrapMode:           Text.WordWrap
            font.pointSize:     ScreenTools.smallFontPointSize
        }
    }
}
