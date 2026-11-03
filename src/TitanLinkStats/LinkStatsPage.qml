import QtQuick
import QtQuick.Layouts
import QtGraphs

import QGroundControl
import QGroundControl.AnalyzeView
import QGroundControl.Controls

/// Titan: TitanPlanner's Link Stats, under the MAVLink Inspector: traffic both ways on the vehicle's link, lost packets,
/// link quality and the longest gap between packets, over a chart of the link's bandwidth (LinkStatsController)
AnalyzePage {
    id:                 linkStatsPage
    pageComponent:      linkStatsComponent
    pageDescription:    qsTr("Traffic on the vehicle's link: bytes and packets each way, lost packets, link quality and bandwidth.")
    allowPopout:        true

    Component {
        id: linkStatsComponent

        ColumnLayout {
            id:         root
            width:      linkStatsPage.availableWidth
            height:     linkStatsPage.availableHeight
            spacing:    ScreenTools.defaultFontPixelHeight / 2

            /// Bytes as TitanPlanner shows them, in steps of 1024
            function bytesText(count) {
                if (count > 1024 * 1024 * 1024) {
                    return (count / (1024 * 1024 * 1024)).toFixed(2) + " GB"
                }
                if (count > 1024 * 1024) {
                    return (count / (1024 * 1024)).toFixed(2) + " MB"
                }
                if (count > 1024) {
                    return (count / 1024).toFixed(2) + " KB"
                }
                return Math.round(count) + " B"
            }

            function summaryText(name, min, max, mean) {
                return qsTr("%1 (Min: %2 Max: %3 Mean: %4)").arg(name).arg(Math.round(min)).arg(Math.round(max)).arg(mean.toFixed(1))
            }

            QGCPalette { id: qgcPal; colorGroupEnabled: true }

            LinkStatsController { id: controller }

            RowLayout {
                spacing: ScreenTools.defaultFontPixelWidth * 2

                SettingsGroupLayout {
                    Layout.alignment:   Qt.AlignTop
                    heading:            qsTr("Download")

                    LabelledLabel {
                        label:      qsTr("Bytes")
                        labelText:  root.bytesText(controller.bytesReceived)
                    }
                    LabelledLabel {
                        label:      qsTr("Bytes/s")
                        labelText:  root.bytesText(controller.bytesReceivedPerSecond)
                    }
                    LabelledLabel {
                        label:      qsTr("Packets")
                        labelText:  controller.packetsReceived.toString()
                    }
                    LabelledLabel {
                        label:      qsTr("Packets/s")
                        labelText:  Math.round(controller.packetsPerSecond).toString()
                    }
                    LabelledLabel {
                        label:      qsTr("Packets lost")
                        labelText:  controller.packetsLost.toString()
                    }
                    LabelledLabel {
                        label:      qsTr("Quality")
                        labelText:  isNaN(controller.quality) ? "--" : Math.round(controller.quality * 100) + "%"
                    }
                    LabelledLabel {
                        label:      qsTr("Max time between packets (ms)")
                        labelText:  isNaN(controller.maxPacketIntervalMs) ? "--" : controller.maxPacketIntervalMs.toFixed(1)
                    }
                }

                ColumnLayout {
                    Layout.alignment:   Qt.AlignTop
                    spacing:            ScreenTools.defaultFontPixelHeight / 2

                    SettingsGroupLayout {
                        Layout.fillWidth:   true
                        heading:            qsTr("Upload")

                        LabelledLabel {
                            label:      qsTr("Total bytes")
                            labelText:  root.bytesText(controller.bytesSent)
                        }
                        LabelledLabel {
                            label:      qsTr("Bytes/s")
                            labelText:  root.bytesText(controller.bytesSentPerSecond)
                        }
                    }

                    QGCLabel {
                        text: controller.hasLink ? qsTr("Link: %1").arg(controller.linkName) : qsTr("No link connected")
                    }

                    QGCButton {
                        text:       qsTr("Reset")
                        primary:    true
                        onClicked:  controller.reset()
                    }
                }
            }

            QGCLabel {
                Layout.alignment:   Qt.AlignHCenter
                text:               qsTr("Link Bandwidth")
                font.bold:          true
            }

            RowLayout {
                Layout.alignment:   Qt.AlignHCenter
                spacing:            ScreenTools.defaultFontPixelWidth * 2

                QGCLabel {
                    text:           root.summaryText(qsTr("RX"), controller.receivedMin, controller.receivedMax, controller.receivedMean)
                    color:          qgcPal.colorGreen
                    font.pointSize: ScreenTools.smallFontPointSize
                }

                QGCLabel {
                    text:           root.summaryText(qsTr("TX"), controller.sentMin, controller.sentMax, controller.sentMean)
                    color:          qgcPal.colorOrange
                    font.pointSize: ScreenTools.smallFontPointSize
                }
            }

            // Styled as the MAVLink Inspector's charts (MAVLinkChart.qml)
            GraphsView {
                id:                     graphsView
                Layout.fillWidth:       true
                Layout.fillHeight:      true
                Layout.minimumHeight:   ScreenTools.defaultFontPixelHeight * 12
                marginBottom:           ScreenTools.defaultFontPixelHeight * 1.5
                marginTop:              0
                marginLeft:             ScreenTools.defaultFontPixelWidth
                marginRight:            0

                theme: GraphsTheme {
                    colorScheme:                qgcPal.globalTheme === QGCPalette.Light ? GraphsTheme.ColorScheme.Light : GraphsTheme.ColorScheme.Dark
                    backgroundColor:            qgcPal.window
                    backgroundVisible:          true
                    plotAreaBackgroundColor:    qgcPal.window
                    grid.mainColor:             Qt.rgba(qgcPal.text.r, qgcPal.text.g, qgcPal.text.b, 0.3)
                    grid.subColor:              Qt.rgba(qgcPal.text.r, qgcPal.text.g, qgcPal.text.b, 0.15)
                    grid.mainWidth:             1
                    labelBackgroundVisible:     false
                    labelTextColor:             qgcPal.text
                    axisXLabelFont.family:      ScreenTools.fixedFontFamily
                    axisXLabelFont.pointSize:   ScreenTools.smallFontPointSize
                    axisYLabelFont.family:      ScreenTools.fixedFontFamily
                    axisYLabelFont.pointSize:   ScreenTools.smallFontPointSize
                }

                axisX: ValueAxis {
                    min:            controller.chartTimeMin
                    max:            controller.chartTimeMax
                    tickInterval:   5
                    subTickCount:   0
                    labelDecimals:  0
                    titleText:      qsTr("Time (s)")
                    titleVisible:   true
                    titleColor:     qgcPal.text
                }

                axisY: ValueAxis {
                    min:            0
                    max:            controller.chartRateMax
                    labelDecimals:  0
                    lineVisible:    false
                    titleText:      qsTr("Bytes/s")
                    titleVisible:   true
                    titleColor:     qgcPal.text
                }

                LineSeries {
                    id:     receivedSeries
                    color:  qgcPal.colorGreen
                    width:  2
                }

                LineSeries {
                    id:     sentSeries
                    color:  qgcPal.colorOrange
                    width:  2
                }

                Component.onCompleted: controller.setSeries(receivedSeries, sentSeries)
            }
        }
    }
}
