pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

/// Sets up one telemetry panel tile: the value from a searchable list, how it shows, and when it turns amber or red
QGCPopupDialog {
    id:         root
    title:      qsTr("Telemetry Value")
    buttons:    Dialog.Close

    required property var item          ///< TelemetryPanelItem
    required property var controller    ///< TelemetryPanelController

    property var    _catalog:   controller.valueCatalog()
    property string _filter:    ""
    readonly property var _shown: _catalog.filter(entry => _filter === ""
                                                     || entry.description.toLowerCase().includes(_filter)
                                                     || entry.groupLabel.toLowerCase().includes(_filter)
                                                     || entry.name.toLowerCase().includes(_filter))
    readonly property string _units: item.fact ? item.fact.units : ""
    readonly property real _fieldWidth: ScreenTools.defaultFontPixelWidth * 9

    function _number(text) {
        const value = parseFloat(text)
        return isNaN(value) ? NaN : value
    }

    function _text(value) {
        return isNaN(value) ? "" : String(value)
    }

    QGCPalette { id: qgcPal; colorGroupEnabled: root.enabled }

    /// A text field in the style of the settings screen's search box
    component ShadedTextField: QGCTextField {
        id: shadedField

        color:                  fieldPal.text
        placeholderTextColor:   Qt.rgba(fieldPal.text.r, fieldPal.text.g, fieldPal.text.b, 0.5)

        background: Rectangle {
            radius:         ScreenTools.defaultFontPixelWidth / 4
            color:          fieldPal.windowShade
            border.width:   shadedField.activeFocus ? 1 : 0
            border.color:   fieldPal.text
        }

        QGCPalette { id: fieldPal; colorGroupEnabled: shadedField.enabled }
    }

    RowLayout {
        spacing: ScreenTools.defaultFontPixelWidth * 2

        // The list runs the full height of the settings beside it
        ColumnLayout {
            Layout.fillHeight:  true
            spacing:            ScreenTools.defaultFontPixelHeight / 2

            ShadedTextField {
                Layout.fillWidth:   true
                placeholderText:    qsTr("Search values")
                onTextChanged:      root._filter = text.trim().toLowerCase()
            }

            Rectangle {
                Layout.preferredWidth:  ScreenTools.defaultFontPixelWidth * 36
                Layout.minimumHeight:   ScreenTools.defaultFontPixelHeight * 12
                Layout.fillHeight:      true
                color:                  qgcPal.windowShade
                radius:                 ScreenTools.defaultFontPixelWidth / 2

                QGCListView {
                    id:                 valueList
                    anchors.fill:       parent
                    anchors.margins:    ScreenTools.defaultFontPixelWidth / 2
                    clip:               true
                    model:              root._shown
                    section.property:   "groupLabel"

                    section.delegate: QGCLabel {
                        required property string section

                        width:          valueList.width
                        topPadding:     ScreenTools.defaultFontPixelHeight / 3
                        text:           section
                        font.bold:      true
                        font.pointSize: ScreenTools.smallFontPointSize
                        opacity:        0.75
                    }

                    delegate: Rectangle {
                        id: valueDelegate

                        required property var modelData

                        readonly property bool _selected: modelData.group.toLowerCase() === root.item.factGroupName.toLowerCase()
                                                          && modelData.name.toLowerCase() === root.item.factName.toLowerCase()

                        width:  valueList.width
                        height: valueRow.height + ScreenTools.defaultFontPixelHeight / 3
                        radius: ScreenTools.defaultFontPixelWidth / 2
                        color:  valueDelegate._selected ? qgcPal.buttonHighlight : "transparent"

                        RowLayout {
                            id:                     valueRow
                            anchors.left:           parent.left
                            anchors.right:          parent.right
                            anchors.margins:        ScreenTools.defaultFontPixelWidth / 2
                            anchors.verticalCenter: parent.verticalCenter

                            QGCLabel {
                                Layout.fillWidth:   true
                                text:               valueDelegate.modelData.description
                                elide:              Text.ElideRight
                                color:              valueDelegate._selected ? qgcPal.buttonHighlightText : qgcPal.text
                            }

                            QGCLabel {
                                text:           valueDelegate.modelData.units
                                font.pointSize: ScreenTools.smallFontPointSize
                                color:          valueDelegate._selected ? qgcPal.buttonHighlightText : qgcPal.text
                                opacity:        0.75
                            }
                        }

                        QGCMouseArea {
                            anchors.fill:   parent
                            onClicked:      root.item.setFact(valueDelegate.modelData.group, valueDelegate.modelData.name)
                        }
                    }
                }
            }
        }

        ColumnLayout {
            Layout.alignment:   Qt.AlignTop
            spacing:            ScreenTools.defaultFontPixelHeight / 2

            SettingsGroupLayout {
                heading: qsTr("Display")

                QGCCheckBoxSlider {
                    Layout.fillWidth:   true
                    text:               qsTr("Show bar")
                    checked:            root.item.style === TelemetryPanelItem.BarStyle
                    onClicked: {
                        if (checked) {
                            root.item.style = TelemetryPanelItem.BarStyle
                            // A percentage already has its range
                            if (!root.item.showPercentage) {
                                root.item.suggestBarRange()
                            }
                        } else {
                            root.item.style = TelemetryPanelItem.NumberStyle
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth:   true
                    spacing:            ScreenTools.defaultFontPixelWidth

                    QGCLabel {
                        Layout.fillWidth:   true
                        text:               qsTr("Label")
                    }

                    ShadedTextField {
                        Layout.preferredWidth:  root._fieldWidth * 2
                        text:                   root.item.label
                        placeholderText:        root.item.fact ? root.item.fact.shortDescription : ""
                        onEditingFinished:      root.item.label = text.trim()
                    }
                }

                QGCCheckBoxSlider {
                    Layout.fillWidth:   true
                    text:               qsTr("Show units")
                    checked:            root.item.showUnits
                    enabled:            !root.item.showPercentage
                    onClicked:          root.item.showUnits = checked
                }

                QGCCheckBoxSlider {
                    Layout.fillWidth:   true
                    text:               qsTr("Show percentage")
                    checked:            root.item.showPercentage
                    onClicked: {
                        root.item.showPercentage = checked
                        // A bar already has its range
                        if (checked && root.item.style !== TelemetryPanelItem.BarStyle) {
                            root.item.suggestBarRange()
                        }
                    }
                }
            }

            SettingsGroupLayout {
                heading:            qsTr("Range")
                headingDescription: qsTr("The values at empty and full, for the bar and the percentage")
                visible:            root.item.style === TelemetryPanelItem.BarStyle || root.item.showPercentage

                RowLayout {
                    spacing: ScreenTools.defaultFontPixelWidth

                    QGCLabel { text: qsTr("From") }

                    QGCTextField {
                        Layout.preferredWidth:  root._fieldWidth
                        text:                   root._text(root.item.barMinimum)
                        numericValuesOnly:      true
                        onEditingFinished: {
                            const value = root._number(text)
                            if (!isNaN(value)) {
                                root.item.barMinimum = value
                            }
                        }
                    }

                    QGCLabel { text: qsTr("to") }

                    QGCTextField {
                        Layout.preferredWidth:  root._fieldWidth
                        text:                   root._text(root.item.barMaximum)
                        numericValuesOnly:      true
                        onEditingFinished: {
                            const value = root._number(text)
                            if (!isNaN(value)) {
                                root.item.barMaximum = value
                            }
                        }
                    }

                    QGCLabel { text: root._units }
                }
            }

            SettingsGroupLayout {
                heading: qsTr("Alert")

                RowLayout {
                    Layout.fillWidth:   true
                    spacing:            ScreenTools.defaultFontPixelWidth

                    QGCLabel {
                        Layout.fillWidth:   true
                        text:               qsTr("Highlight")
                    }

                    QGCComboBox {
                        model:          [ qsTr("Never"), qsTr("When above"), qsTr("When below") ]
                        currentIndex:   root.item.alert
                        sizeToContents: true
                        onActivated: (index) => root.item.alert = index
                    }
                }

                GridLayout {
                    columns:        3
                    columnSpacing:  ScreenTools.defaultFontPixelWidth
                    enabled:        root.item.alert !== TelemetryPanelItem.NoAlert

                    QGCLabel {
                        text:   qsTr("Caution")
                        color:  qgcPal.colorOrange
                    }

                    QGCTextField {
                        Layout.preferredWidth:  root._fieldWidth
                        text:                   root._text(root.item.cautionValue)
                        placeholderText:        qsTr("Off")
                        numericValuesOnly:      true
                        onEditingFinished:      root.item.cautionValue = root._number(text)
                    }

                    QGCLabel { text: root._units }

                    QGCLabel {
                        text:   qsTr("Warning")
                        color:  qgcPal.colorRed
                    }

                    QGCTextField {
                        Layout.preferredWidth:  root._fieldWidth
                        text:                   root._text(root.item.warningValue)
                        placeholderText:        qsTr("Off")
                        numericValuesOnly:      true
                        onEditingFinished:      root.item.warningValue = root._number(text)
                    }

                    QGCLabel { text: root._units }
                }
            }

            QGCButton {
                Layout.alignment:   Qt.AlignRight
                text:               qsTr("Remove Value")
                onClicked: {
                    // Closed first, so nothing here is left bound to the removed tile
                    const item = root.item
                    const controller = root.controller
                    root.close()
                    controller.removeItem(item)
                }
            }
        }
    }
}
