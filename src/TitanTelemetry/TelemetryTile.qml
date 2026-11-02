import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

/// One telemetry panel tile: the value's name over its number, and for a bar, the bar between its range's ends.
/// Amber and red mark the value's caution and warning levels.
Item {
    id: root

    required property var item  ///< TelemetryPanelItem

    property bool editing:        false
    property real valuePointSize: ScreenTools.mediumFontPointSize
    property real barHeight:      ScreenTools.defaultFontPixelHeight * 0.3

    signal removeClicked()

    readonly property bool   _isBar:      item.style === TelemetryPanelItem.BarStyle
    readonly property color  _levelColor: item.level === 2 ? qgcPal.colorRed : item.level === 1 ? qgcPal.colorOrange : qgcPal.text
    readonly property bool   _hasNumber:  item.fact && !isNaN(item.fact.rawValue)
    readonly property string _valueText:  item.showPercentage ? (_hasNumber ? Math.round(item.fraction * 100) : "–")
                                                              : (item.fact ? item.fact.enumOrValueString : "–")
    readonly property string _unitsText:  item.showPercentage ? "%" : (item.fact && item.showUnits ? item.fact.units : "")

    QGCPalette { id: qgcPal; colorGroupEnabled: root.enabled }

    Rectangle {
        anchors.fill:       parent
        anchors.margins:    -ScreenTools.defaultFontPixelWidth / 4
        radius:             ScreenTools.defaultFontPixelWidth / 2
        color:              qgcPal.text
        opacity:            0.08
        border.color:       qgcPal.text
        border.width:       1
        visible:            root.editing
    }

    ColumnLayout {
        anchors.left:   parent.left
        anchors.right:  parent.right
        anchors.top:    parent.top
        spacing:        0

        QGCLabel {
            Layout.fillWidth:   true
            text:               root.item.displayLabel
            font.pointSize:     ScreenTools.smallFontPointSize
            elide:              Text.ElideRight
            opacity:            0.75
        }

        RowLayout {
            Layout.fillWidth:   true
            spacing:            ScreenTools.defaultFontPixelWidth / 3

            QGCLabel {
                Layout.alignment:   Qt.AlignBaseline
                text:               root._valueText
                font.pointSize:     root.valuePointSize
                font.weight:        Font.DemiBold
                color:              root._levelColor
            }

            QGCLabel {
                Layout.alignment:   Qt.AlignBaseline
                Layout.fillWidth:   true
                text:               root._unitsText
                font.pointSize:     ScreenTools.smallFontPointSize
                elide:              Text.ElideRight
                opacity:            0.75
            }
        }

        Item {
            Layout.fillWidth:       true
            Layout.topMargin:       root.barHeight
            Layout.preferredHeight: root.barHeight
            visible:                root._isBar

            Rectangle {
                anchors.fill:   parent
                radius:         height / 2
                color:          qgcPal.text
                opacity:        0.2
            }

            Rectangle {
                width:      parent.width * root.item.fraction
                height:     parent.height
                radius:     height / 2
                color:      root._levelColor
                opacity:    root.item.level === 0 ? 0.85 : 1

                Behavior on width { NumberAnimation { duration: 150 } }
            }
        }
    }

    // Remove, in edit mode
    Rectangle {
        anchors.horizontalCenter:   parent.right
        anchors.verticalCenter:     parent.top
        width:                      ScreenTools.defaultFontPixelHeight
        height:                     width
        radius:                     width / 2
        color:                      qgcPal.window
        border.color:               qgcPal.text
        border.width:               1
        visible:                    root.editing

        QGCColoredImage {
            anchors.centerIn:   parent
            width:              parent.width * 0.6
            height:             width
            sourceSize.height:  height
            source:             "/InstrumentValueIcons/close.svg"
            color:              qgcPal.text
        }

        QGCMouseArea {
            anchors.fill:   parent
            onClicked:      root.removeClicked()
        }
    }
}
