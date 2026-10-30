import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

/// "Stats for nerds", as in Mission Planner's 3D map: frame timing with a graph, terrain streaming, the view and
/// the GPU.
Rectangle {
    id: root

    property var stats: ({})

    readonly property real _fontSize: ScreenTools.smallFontPointSize
    readonly property color _labelColor: "#9aa4b0"
    readonly property color _valueColor: "#f0f0f0"
    readonly property color _accentColor: "#ffa040"
    readonly property color _badColor: "#ff6060"

    function _number(value, decimals) {
        return (value === undefined || isNaN(value)) ? "-" : Number(value).toFixed(decimals)
    }

    width:      layout.implicitWidth + ScreenTools.defaultFontPixelWidth * 2
    height:     layout.implicitHeight + ScreenTools.defaultFontPixelHeight
    radius:     ScreenTools.defaultFontPixelHeight / 3
    color:      Qt.rgba(0.07, 0.08, 0.1, 0.82)

    component Section: QGCLabel {
        font.pointSize: root._fontSize
        font.bold:      true
        color:          root._accentColor
    }

    component Row: RowLayout {
        property alias label: rowLabel.text
        property alias value: rowValue.text
        property color valueColor: root._valueColor

        spacing: ScreenTools.defaultFontPixelWidth

        QGCLabel {
            id:                     rowLabel
            Layout.preferredWidth:  ScreenTools.defaultFontPixelWidth * 10
            font.pointSize:         root._fontSize
            font.family:            ScreenTools.fixedFontFamily
            color:                  root._labelColor
        }
        QGCLabel {
            id:             rowValue
            font.pointSize: root._fontSize
            font.family:    ScreenTools.fixedFontFamily
            color:          valueColor
        }
    }

    ColumnLayout {
        id:                 layout
        anchors.centerIn:   parent
        spacing:            ScreenTools.defaultFontPixelHeight * 0.15

        Section { text: qsTr("FRAME") }
        Row { label: qsTr("Rate"); value: root._number(root.stats.fps, 0) + " fps  " + root._number(root.stats.frameMs, 1) + " ms" }
        Row { label: qsTr("CPU"); value: root._number(root.stats.cpuMs, 1) + " ms  (terrain " + root._number(root.stats.terrainMs, 1) + " ms)" }

        Canvas {
            id:                     graph
            Layout.preferredWidth:  ScreenTools.defaultFontPixelWidth * 34
            Layout.preferredHeight: ScreenTools.defaultFontPixelHeight * 2.5

            property var samples: root.stats.frameGraph || []

            onSamplesChanged: requestPaint()

            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                ctx.fillStyle = Qt.rgba(1, 1, 1, 0.06)
                ctx.fillRect(0, 0, width, height)
                if (samples.length < 2) {
                    return
                }
                // 16.7 ms (60 fps) at mid-height, 33 ms at the top
                const scale = height / 33.3
                ctx.strokeStyle = Qt.rgba(1, 1, 1, 0.25)
                ctx.beginPath()
                ctx.moveTo(0, height - 16.7 * scale)
                ctx.lineTo(width, height - 16.7 * scale)
                ctx.stroke()
                ctx.strokeStyle = root._accentColor
                ctx.beginPath()
                for (let i = 0; i < samples.length; i++) {
                    const x = i * width / (samples.length - 1)
                    const y = height - Math.min(height, samples[i] * scale)
                    if (i === 0) {
                        ctx.moveTo(x, y)
                    } else {
                        ctx.lineTo(x, y)
                    }
                }
                ctx.stroke()
            }
        }

        Section { text: qsTr("TERRAIN") }
        Row { label: qsTr("Tiles"); value: (root.stats.tilesDrawn || 0) + " drawn, " + (root.stats.tilesResident || 0) + " resident, " + (root.stats.nodes || 0) + " nodes" }
        Row { label: qsTr("Zooms"); value: (root.stats.minZoom || 0) + " - " + (root.stats.maxZoom || 0) + "  (max " + (root.stats.maxZoomSetting || 0) + ")" }
        Row { label: qsTr("GPU"); value: root._number(root.stats.gpuMB, 0) + " / " + root._number(root.stats.gpuBudgetMB, 0) + " MB" }
        Row { label: qsTr("Queue"); value: (root.stats.queued || 0) + " queued, " + (root.stats.inFlight || 0) + " / " + (root.stats.workers || 0) + " loading" }
        Row { label: qsTr("Source"); value: (root.stats.cacheHits || 0) + " from disk cache, " + (root.stats.networkFetches || 0) + " downloaded" }
        Row { label: qsTr("Loaded"); value: (root.stats.loadedTotal || 0) + " (" + root._number(root.stats.loadedPerSecond, 1) + "/s)  fetch " + root._number(root.stats.fetchMs, 0) + " ms, build " + root._number(root.stats.buildMs, 1) + " ms" }
        Row {
            label:      qsTr("Health")
            value:      (root.stats.failed || 0) + " failed, " + (root.stats.noData || 0) + " no data"
            valueColor: (root.stats.failed || 0) > 0 ? root._badColor : root._valueColor
        }
        Row {
            label:      qsTr("Elevation")
            value:      (root.stats.waitingForElevation || 0) > 0 ? (root.stats.waitingForElevation + " tiles waiting") : qsTr("ready")
        }
        Row {
            label:      qsTr("Last error")
            value:      root.stats.lastError || ""
            valueColor: root._badColor
            visible:    !!root.stats.lastError
        }

        Section { text: qsTr("VIEW") }
        Row { label: qsTr("Camera"); value: (root.stats.mode || "") + (root.stats.connected ? qsTr("  vehicle connected") : qsTr("  following 2D map")) }
        Row { label: qsTr("Look"); value: root._number(root.stats.heading, 0) + "° heading, " + root._number(root.stats.pitch, 0) + "° pitch, " + root._number(root.stats.fov, 0) + "° fov" }
        Row { label: qsTr("Range"); value: root._number(root.stats.viewRadius / 1000, 0) + " km, fog " + root._number(root.stats.fogStart / 1000, 0) + "-" + root._number(root.stats.fogEnd / 1000, 0) + " km" }
        Row { label: qsTr("Grid"); value: "UTM " + (root.stats.utmZone || 0) + ", origin moved " + (root.stats.originMoves || 0) + "x" }

        Section { text: qsTr("SYSTEM") }
        Row { label: qsTr("Renderer"); value: (root.stats.backend || "") + "  " + (root.stats.gpu || "") }
        Row { label: qsTr("Imagery"); value: root.stats.imagery || "" }
    }
}
