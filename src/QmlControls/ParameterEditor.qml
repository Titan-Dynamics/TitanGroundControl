import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FactControls

Item {
    id:         _root

    property Fact   _editorDialogFact: Fact { }
    property int    _rowHeight:         ScreenTools.defaultFontPixelHeight * 2
    property int    _rowWidth:          10 // Dynamic adjusted at runtime
    property bool   _searchFilter:      searchText.text.trim() != "" || controller.showModifiedOnly  ///< true: showing results of search
    property var    _searchResults      ///< List of parameter names from search results
    property var    _activeVehicle:     QGroundControl.multiVehicleManager.activeVehicle
    property bool   _showRCToParam:     _activeVehicle.px4Firmware
    property var    _appSettings:       QGroundControl.settingsManager.appSettings
    property var    _controller:        controller

    ParameterEditorController {
        id: controller
    }

    Timer {
        id:         clearTimer
        interval:   100;
        running:    false;
        repeat:     false
        onTriggered: {
            searchText.text = ""
            controller.searchText = ""
        }
    }

    QGCMenu {
        id:                 toolsMenu
        QGCMenuItem {
            text:           qsTr("Refresh")
            onTriggered:	controller.refresh()
        }
        QGCMenuItem {
            text:           qsTr("Reset all to firmware's defaults")
            onTriggered:    QGroundControl.showMessageDialog(_root, qsTr("Reset All"),
                                                         qsTr("Select Reset to reset all parameters to their defaults.\n\nNote that this will also completely reset everything, including UAVCAN nodes, all vehicle settings, setup and calibrations."),
                                                         Dialog.Cancel | Dialog.Reset,
                                                         function() { controller.resetAllToDefaults() })
        }
        QGCMenuItem {
            text:           qsTr("Reset to vehicle's configuration defaults")
            visible:        !_activeVehicle.apmFirmware
            onTriggered:    QGroundControl.showMessageDialog(_root, qsTr("Reset All"),
                                                         qsTr("Select Reset to reset all parameters to the vehicle's configuration defaults."),
                                                         Dialog.Cancel | Dialog.Reset,
                                                         function() { controller.resetAllToVehicleConfiguration() })
        }
        QGCMenuSeparator { }
        QGCMenuItem {
            text:           qsTr("Load from file for review...")
            onTriggered: {
                fileDialog.title =          qsTr("Load Parameters")
                fileDialog.openForLoad()
            }
        }
        QGCMenuItem {
            text:           qsTr("Save to file...")
            onTriggered: {
                fileDialog.title =          qsTr("Save Parameters")
                fileDialog.openForSave()
            }
        }
        QGCMenuSeparator { visible: _showRCToParam }
        QGCMenuItem {
            text:           qsTr("Clear all RC to Param")
            onTriggered:	_activeVehicle.clearAllParamMapRC()
            visible:        _showRCToParam
        }
        QGCMenuSeparator { }
        QGCMenuItem {
            text:           qsTr("Reboot Vehicle")
            onTriggered:    QGroundControl.showMessageDialog(_root, qsTr("Reboot Vehicle"),
                                                         qsTr("Select Ok to reboot vehicle."),
                                                         Dialog.Cancel | Dialog.Ok,
                                                         function() { _activeVehicle.rebootVehicle() })
        }
    }


    QGCFileDialog {
        id:             fileDialog
        folder:         _appSettings.parameterSavePath
        nameFilters:    [ qsTr("Parameter Files (*.%1)").arg(_appSettings.parameterFileExtension) , qsTr("All Files (*)") ]

        onAcceptedForSave: (file) => {
            controller.saveToFile(file)
            close()
        }

        onAcceptedForLoad: (file) => {
            close()
            if (controller.buildDiffFromFile(file)) {
                parameterDiffDialogFactory.open()
            }
        }
    }

    QGCPopupDialogFactory {
        id: editorDialogFactory

        dialogComponent: editorDialogComponent
    }

    Component {
        id: editorDialogComponent

        ParameterEditorDialog {
            fact:           _editorDialogFact
            showRCToParam:  _showRCToParam
        }
    }

    QGCPopupDialogFactory {
        id: parameterDiffDialogFactory

        dialogComponent: parameterDiffDialog
    }

    Component {
        id: parameterDiffDialog

        ParameterDiffDialog {
            paramController: _controller
        }
    }

    RowLayout {
        id:             header
        anchors.left:   parent.left
        anchors.right:  parent.right

        RowLayout {
            Layout.alignment:   Qt.AlignLeft
            spacing:            ScreenTools.defaultFontPixelWidth

            QGCTextField {
                id:                     searchText
                placeholderText:        qsTr("Search")
                Layout.preferredWidth:  ScreenTools.defaultFontPixelWidth * 30
                Layout.preferredHeight: toolsButton.height
                onDisplayTextChanged:   controller.searchText = displayText
            }

            QGCButton {
                text: qsTr("Clear")
                Layout.preferredHeight: toolsButton.height
                onClicked: {
                    if(ScreenTools.isMobile) {
                        Qt.inputMethod.hide();
                    }
                    clearTimer.start()
                }
            }

            QGCCheckBox {
                text:       qsTr("Show modified only")
                checked:    controller.showModifiedOnly
                onClicked:  controller.showModifiedOnly = checked
                visible:    QGroundControl.multiVehicleManager.activeVehicle.px4Firmware
            }
        }

        QGCButton {
            id:                     toolsButton
            Layout.alignment:       Qt.AlignRight
            Layout.rightMargin:     ScreenTools.defaultFontPixelWidth * 2
            text:                   qsTr("Tools")
            onClicked:              toolsMenu.popup()
        }
    }

    /// Group buttons
    QGCFlickable {
        id :                groupScroll
        width:              ScreenTools.defaultFontPixelWidth * 25
        anchors.top:        header.bottom
        anchors.bottom:     parent.bottom
        clip:               true
        pixelAligned:       true
        contentHeight:      groupedViewCategoryColumn.height
        flickableDirection: Flickable.VerticalFlick
        visible:            !_searchFilter

        ColumnLayout {
            id:             groupedViewCategoryColumn
            anchors.left:   parent.left
            anchors.right:  parent.right
            spacing:        Math.ceil(ScreenTools.defaultFontPixelHeight * 0.25)

            Repeater {
                model: controller.categories

                Column {
                    Layout.fillWidth:   true
                    spacing:            Math.ceil(ScreenTools.defaultFontPixelHeight * 0.25)


                    SectionHeader {
                        id:             categoryHeader
                        anchors.left:   parent.left
                        anchors.right:  parent.right
                        text:           object.name
                        checked:        object == controller.currentCategory

                        onCheckedChanged: {
                            if (checked) {
                                controller.currentCategory  = object
                            }
                        }
                    }

                    Repeater {
                        model: categoryHeader.checked ? object.groups : 0

                        QGCButton {
                            width:          ScreenTools.defaultFontPixelWidth * 25
                            text:           object.name
                            height:         _rowHeight
                            checked:        object == controller.currentGroup
                            autoExclusive:  true

                            onClicked: {
                                if (!checked) _rowWidth = 10
                                checked = true
                                controller.currentGroup = object
                            }
                        }
                    }
                }
            }
        }
    }

    Item {
        id:                 tableContainer
        anchors.leftMargin: ScreenTools.defaultFontPixelWidth * 2
        anchors.topMargin:  ScreenTools.defaultFontPixelHeight
        anchors.top:        header.bottom
        anchors.bottom:     parent.bottom
        anchors.left:       _searchFilter ? parent.left : groupScroll.right
        anchors.right:      parent.right
        clip:               true

        property real _colParamWidth:  width * 0.25
        property real _colValueWidth:  width * 0.20
        property real _colDescWidth:   width * 0.55

        // Column headers
        Rectangle {
            id:             tableHeader
            anchors.left:   parent.left
            anchors.right:  parent.right
            height:         ScreenTools.defaultFontPixelHeight * 1.75
            color:          qgcPal.toolbarBackground
            radius:         ScreenTools.defaultFontPixelHeight * 0.25

            Row {
                anchors.fill:           parent
                anchors.leftMargin:     ScreenTools.defaultFontPixelWidth * 1.5
                anchors.rightMargin:    ScreenTools.defaultFontPixelWidth * 1.5

                Item {
                    width:  tableContainer._colParamWidth
                    height: parent.height
                    QGCLabel {
                        anchors.verticalCenter: parent.verticalCenter
                        text:                   qsTr("Parameter")
                        font.bold:              true
                        font.pointSize:         ScreenTools.defaultFontPointSize
                        color:                  qgcPal.buttonHighlightText
                    }
                }
                Item {
                    width:  tableContainer._colValueWidth
                    height: parent.height
                    QGCLabel {
                        anchors.verticalCenter: parent.verticalCenter
                        text:                   qsTr("Value")
                        font.bold:              true
                        font.pointSize:         ScreenTools.defaultFontPointSize
                        color:                  qgcPal.buttonHighlightText
                    }
                }
                Item {
                    width:  tableContainer._colDescWidth
                    height: parent.height
                    QGCLabel {
                        anchors.verticalCenter: parent.verticalCenter
                        text:                   qsTr("Description")
                        font.bold:              true
                        font.pointSize:         ScreenTools.defaultFontPointSize
                        color:                  qgcPal.buttonHighlightText
                    }
                }
            }
        }

        // Parameter rows
        ListView {
            id:             paramListView
            anchors.top:    tableHeader.bottom
            anchors.topMargin: ScreenTools.defaultFontPixelHeight * 0.25
            anchors.left:   parent.left
            anchors.right:  parent.right
            anchors.bottom: parent.bottom
            clip:           true
            model:          controller.parameters

            delegate: Rectangle {
                width:      paramListView.width
                height:     ScreenTools.defaultFontPixelHeight * 1.75
                color:      mouseArea.containsMouse
                                ? Qt.rgba(qgcPal.buttonHighlight.r, qgcPal.buttonHighlight.g, qgcPal.buttonHighlight.b, 0.15)
                                : (index % 2 === 0 ? "transparent" : Qt.rgba(qgcPal.text.r, qgcPal.text.g, qgcPal.text.b, 0.04))
                radius:     ScreenTools.defaultFontPixelHeight * 0.15

                property var rowFact: model.fact

                Row {
                    anchors.fill:           parent
                    anchors.leftMargin:     ScreenTools.defaultFontPixelWidth * 1.5
                    anchors.rightMargin:    ScreenTools.defaultFontPixelWidth * 1.5

                    // Parameter name
                    Item {
                        width:  tableContainer._colParamWidth
                        height: parent.height
                        QGCLabel {
                            anchors.verticalCenter: parent.verticalCenter
                            width:                  parent.width - ScreenTools.defaultFontPixelWidth
                            text:                   model.display
                            font.family:            ScreenTools.fixedFontFamily
                            font.pointSize:         ScreenTools.defaultFontPointSize
                            elide:                  Text.ElideRight
                            maximumLineCount:       1
                        }
                    }

                    // Value
                    Item {
                        width:  tableContainer._colValueWidth
                        height: parent.height
                        QGCLabel {
                            anchors.verticalCenter: parent.verticalCenter
                            width:                  parent.width - ScreenTools.defaultFontPixelWidth
                            text:                   valueString()
                            color:                  valueColor()
                            font.pointSize:         ScreenTools.defaultFontPointSize
                            elide:                  Text.ElideRight
                            maximumLineCount:       1

                            function valueString() {
                                if (rowFact.enumStrings.length === 0)
                                    return rowFact.valueString + " " + rowFact.units
                                if (rowFact.bitmaskStrings.length !== 0)
                                    return rowFact.selectedBitmaskStrings.join(', ')
                                return rowFact.enumStringValue
                            }

                            function valueColor() {
                                if (rowFact.defaultValueAvailable)
                                    return rowFact.valueEqualsDefault ? qgcPal.text : qgcPal.warningText
                                return qgcPal.text
                            }
                        }
                    }

                    // Description
                    Item {
                        width:  tableContainer._colDescWidth
                        height: parent.height
                        QGCLabel {
                            anchors.verticalCenter: parent.verticalCenter
                            width:                  parent.width - ScreenTools.defaultFontPixelWidth
                            text:                   rowFact.shortDescription
                            font.pointSize:         ScreenTools.smallFontPointSize
                            color:                  Qt.rgba(qgcPal.text.r, qgcPal.text.g, qgcPal.text.b, 0.7)
                            elide:                  Text.ElideRight
                            maximumLineCount:       1
                        }
                    }
                }

                MouseArea {
                    id:             mouseArea
                    anchors.fill:   parent
                    hoverEnabled:   true
                    cursorShape:    Qt.PointingHandCursor
                    onClicked: {
                        _editorDialogFact = rowFact
                        editorDialogFactory.open()
                    }
                }
            }
        }
    }
}
