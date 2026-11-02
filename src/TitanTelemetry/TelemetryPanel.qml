pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQml.Models

import QGroundControl
import QGroundControl.Controls

/// Titan's Fly View telemetry panel: a grid of tiles, each showing a value as a number or a bar. The pencil, a right
/// click or a press and hold enters edit mode, where tiles are added, removed, dragged into order and set up.
Item {
    id:             root
    implicitWidth:  content.width + _margin * 2
    implicitHeight: content.height + _margin * 2

    property real extraWidth: 0     ///< Extra width for the background, to run on under the instrument panel
    property bool editing:    false

    readonly property real _margin:         ScreenTools.defaultFontPixelWidth * 0.75
    readonly property real _spacing:        ScreenTools.defaultFontPixelWidth * 1.25
    readonly property var  _valuePointSizes: [ ScreenTools.defaultFontPointSize, ScreenTools.mediumFontPointSize, ScreenTools.largeFontPointSize ]
    readonly property real _valuePointSize: _valuePointSizes[controller.textSize]
    readonly property real _barHeight:      Math.max(3, ScreenTools.defaultFontPixelHeight * 0.3)
    readonly property real _tileWidth:      Math.max(valueMetrics.width + unitsMetrics.width + ScreenTools.defaultFontPixelWidth, ScreenTools.defaultFontPixelWidth * 10)
    readonly property real _tileHeight:     labelMetrics.height + valueMetrics.height + (controller.hasBars ? _barHeight * 2 : 0)
    readonly property int  _rows:           Math.max(1, Math.ceil(controller.items.count / controller.columns))

    function openEditor(item) {
        editorFactory.open({ item: item, controller: controller })
    }

    TelemetryPanelController { id: controller }

    QGCPalette { id: qgcPal; colorGroupEnabled: root.enabled }

    TextMetrics { id: valueMetrics; font.pointSize: root._valuePointSize; font.weight: Font.DemiBold; text: "00:00:00" }
    TextMetrics { id: unitsMetrics; font.pointSize: ScreenTools.smallFontPointSize; text: "km/h" }
    TextMetrics { id: labelMetrics; font.pointSize: ScreenTools.smallFontPointSize; text: "Alt" }

    Rectangle {
        width:      root.width + root.extraWidth
        height:     root.height
        color:      qgcPal.window
        radius:     ScreenTools.defaultFontPixelWidth / 2
        opacity:    0.75
    }

    // Clicks on the panel stay off the map underneath
    QGCMouseArea {
        anchors.fill:       parent
        acceptedButtons:    Qt.LeftButton | Qt.RightButton
        onClicked: (mouse) => {
            if (mouse.button === Qt.RightButton) {
                root.editing = true
            }
        }
        onPressAndHold: root.editing = true
    }

    HoverHandler { id: hoverHandler }

    ColumnLayout {
        id:         content
        x:          root._margin
        y:          root._margin
        spacing:    root._spacing / 2

        RowLayout {
            Layout.fillWidth:   true
            spacing:            ScreenTools.defaultFontPixelWidth / 2
            visible:            root.editing

            QGCButton {
                text:       qsTr("Add")
                iconSource: "/InstrumentValueIcons/add-outline.svg"
                small:      true
                onClicked:  root.openEditor(controller.appendItem())
            }

            QGCLabel {
                Layout.leftMargin:  ScreenTools.defaultFontPixelWidth
                text:               qsTr("Columns")
                font.pointSize:     ScreenTools.smallFontPointSize
            }

            QGCButton {
                text:       "−"
                small:      true
                enabled:    controller.columns > controller.minColumns
                onClicked:  controller.columns = controller.columns - 1
            }

            QGCLabel {
                text: controller.columns
            }

            QGCButton {
                text:       "+"
                small:      true
                enabled:    controller.columns < controller.maxColumns
                onClicked:  controller.columns = controller.columns + 1
            }

            QGCComboBox {
                Layout.leftMargin:  ScreenTools.defaultFontPixelWidth
                model:              [ qsTr("Small"), qsTr("Medium"), qsTr("Large") ]
                currentIndex:       controller.textSize
                sizeToContents:     true
                onActivated: (index) => controller.textSize = index
            }

            // Reset and Done at the far right
            Item { Layout.fillWidth: true }

            QGCButton {
                Layout.leftMargin:  ScreenTools.defaultFontPixelWidth
                text:               qsTr("Reset")
                small:              true
                onClicked:          controller.resetToDefaults()
            }

            QGCButton {
                text:       qsTr("Done")
                small:      true
                primary:    true
                onClicked:  root.editing = false
            }
        }

        GridView {
            id:                     grid
            Layout.preferredWidth:  Math.max(1, Math.min(controller.columns, count)) * cellWidth
            Layout.preferredHeight: root._rows * cellHeight
            cellWidth:              root._tileWidth + root._spacing
            cellHeight:             root._tileHeight + root._spacing / 2
            interactive:            false
            visible:                count > 0

            displaced: Transition {
                NumberAnimation { properties: "x,y"; duration: 150; easing.type: Easing.OutCubic }
            }

            model: DelegateModel {
                model: controller.items

                // Each cell takes a dragged tile; the tiles reorder as it passes over them
                delegate: DropArea {
                    id:     cell
                    width:  grid.cellWidth
                    height: grid.cellHeight

                    required property var object

                    property int visualIndex: DelegateModel.itemsIndex

                    onEntered: (drag) => controller.moveItem(drag.source.visualIndex, cell.visualIndex)

                    TelemetryTile {
                        id:                         tile
                        anchors.horizontalCenter:   parent.horizontalCenter
                        anchors.verticalCenter:     parent.verticalCenter
                        width:                      root._tileWidth
                        height:                     root._tileHeight
                        item:                       cell.object
                        editing:                    root.editing
                        valuePointSize:             root._valuePointSize
                        barHeight:                  root._barHeight
                        onRemoveClicked:            controller.removeItem(cell.object)

                        property int visualIndex: cell.visualIndex

                        Drag.active:    dragHandler.active
                        Drag.source:    tile
                        Drag.hotSpot.x: width / 2
                        Drag.hotSpot.y: height / 2

                        DragHandler {
                            id:         dragHandler
                            enabled:    root.editing
                        }

                        TapHandler {
                            enabled:    root.editing
                            onTapped:   root.openEditor(cell.object)
                        }

                        HoverHandler {
                            enabled:        root.editing
                            cursorShape:    dragHandler.active ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                        }

                        states: State {
                            when: dragHandler.active

                            ParentChange {
                                target: tile
                                parent: grid
                            }

                            AnchorChanges {
                                target:                     tile
                                anchors.horizontalCenter:   undefined
                                anchors.verticalCenter:     undefined
                            }

                            PropertyChanges {
                                tile.z:         1
                                tile.opacity:   0.85
                            }
                        }
                    }
                }
            }
        }

        QGCLabel {
            text:           grid.count > 0 ? qsTr("Drag tiles to reorder them, click one to change it") : qsTr("No values. Add one to show its telemetry here.")
            font.pointSize: ScreenTools.smallFontPointSize
            opacity:        0.75
            visible:        root.editing || grid.count === 0
        }
    }

    // Edit, shown on hover; always on touch screens, which have no hover
    Rectangle {
        anchors.right:  parent.right
        anchors.top:    parent.top
        anchors.margins: root._margin / 2
        width:          ScreenTools.defaultFontPixelHeight * 1.2
        height:         width
        radius:         width / 2
        color:          qgcPal.window
        opacity:        hoverHandler.hovered ? 1 : 0.6
        visible:        !root.editing && (hoverHandler.hovered || ScreenTools.isMobile)

        QGCColoredImage {
            anchors.centerIn:   parent
            width:              parent.width * 0.6
            height:             width
            sourceSize.height:  height
            source:             "/InstrumentValueIcons/edit-pencil.svg"
            color:              qgcPal.text
        }

        QGCMouseArea {
            anchors.fill:   parent
            onClicked:      root.editing = true
        }
    }

    QGCPopupDialogFactory {
        id:                 editorFactory
        dialogComponent:    editorComponent
    }

    Component {
        id: editorComponent

        TelemetryItemEditor { }
    }
}
