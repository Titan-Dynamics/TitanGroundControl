pragma ComponentBehavior: Bound

import QtQuick
import QtLocation
import QtPositioning

import QGroundControl
import QGroundControl.Controls

/// An ADS-B aircraft on the Fly View map, as TitanPlanner shows traffic: an icon for its category turned to its track,
/// larger for large and heavy aircraft, orange, red while the vehicle reports a high collision threat (or the feed an
/// alert), and a translucent grey on the ground. Hovering, or a tap on touch screens, shows TitanPlanner's details.
MapQuickItem {
    id: root

    // Not required: a required property would make MapItemView stop providing the delegate's object
    property var    adsbVehicle:    null    ///< ADSBVehicle
    property var    map
    property real   size:           ScreenTools.defaultFontPixelHeight * 2.2
    property bool   showDetails:    true    ///< Off in the PiP

    property bool   _pinned:        false   ///< Details kept open by a tap

    readonly property int  _emitterType:    adsbVehicle ? adsbVehicle.emitterType : 0
    readonly property bool _onGround:       adsbVehicle ? adsbVehicle.onGround : false
    readonly property real _heading:        adsbVehicle ? adsbVehicle.heading : 0
    readonly property bool _danger:         adsbVehicle ? (adsbVehicle.alert || adsbVehicle.threatLevel >= 2) : false
    readonly property bool _detailsShown:   showDetails && !!adsbVehicle && (hoverHandler.hovered || _pinned)
    readonly property real _scale: {
        switch (_emitterType) {
        case 3:     // Large
            return 1.25
        case 4:     // High vortex large
        case 5:     // Heavy
            return 1.5
        default:
            return 1
        }
    }

    coordinate:     adsbVehicle ? adsbVehicle.coordinate : QtPositioning.coordinate()
    anchorPoint.x:  sourceItem.width / 2
    anchorPoint.y:  sourceItem.height / 2
    visible:        coordinate.isValid
    z:              _detailsShown ? QGroundControl.zOrderVehicles + 1 : QGroundControl.zOrderVehicles

    function _details() {
        const traffic = adsbVehicle
        const units = QGroundControl.unitsConversion
        const altitudeUnits = units.appSettingsVerticalDistanceUnitsString
        const speedUnits = units.appSettingsSpeedUnitsString
        const address = ("000000" + (traffic.icaoAddress & 0xFFFFFF).toString(16).toUpperCase()).slice(-6)
        const lines = [
            qsTr("ICAO: %1%2").arg((traffic.icaoAddress & 0x1000000) ? "~" : "").arg(address),
            qsTr("Callsign: %1").arg(traffic.callsign),
            qsTr("Type/Category: %1").arg(traffic.aircraftType !== "" ? traffic.aircraftType + " / " + traffic.categoryName : traffic.categoryName),
            qsTr("Squawk: %1").arg(traffic.squawk),
            qsTr("Alt: %1 %2").arg(Math.round(units.metersToAppSettingsVerticalDistanceUnits(traffic.altitude))).arg(altitudeUnits),
            qsTr("Speed: %1 %2").arg(Math.round(units.metersSecondToAppSettingsSpeedUnits(traffic.velocity))).arg(speedUnits),
            qsTr("VSpeed: %1 %2").arg(units.metersSecondToAppSettingsSpeedUnits(traffic.verticalVel).toFixed(1)).arg(speedUnits),
            qsTr("Heading: %1°").arg(Math.round(traffic.heading)) + (traffic.onGround ? qsTr(" (On Ground)") : "")
        ]
        const own = QGroundControl.multiVehicleManager.activeVehicle
        if (own && own.coordinate.isValid) {
            const distance = own.coordinate.distanceTo(traffic.coordinate)
            lines.push(qsTr("Distance: %1").arg(distance >= 1000 ? qsTr("%1 km").arg((distance / 1000).toFixed(1))
                                                                 : Math.round(units.metersToAppSettingsHorizontalDistanceUnits(distance)) + " " + units.appSettingsHorizontalDistanceUnitsString))
            const delta = Math.round(units.metersToAppSettingsVerticalDistanceUnits(traffic.altitude - own.altitudeAMSL.rawValue))
            lines.push(qsTr("Alt Delta: %1%2 %3").arg(delta >= 0 ? "+" : "").arg(delta).arg(altitudeUnits))
        }
        if (traffic.threatLevel > 0 || traffic.alert) {
            lines.push(qsTr("Collision risk: %1").arg(_danger ? qsTr("Danger") : qsTr("Warning")))
        }
        return lines.join("\n")
    }

    sourceItem: Item {
        width:  root.size * root._scale
        height: width

        TitanAdsbIcon {
            id:             icon
            anchors.fill:   parent
            emitterType:    root._emitterType
            fillColor:      root._onGround ? Qt.rgba(0.66, 0.66, 0.66, 0.5) : (root._danger ? "#ff0000" : "#ffa500")
            outlineColor:   root._onGround ? Qt.rgba(0.1, 0.1, 0.1, 0.6) : "#1a1a1a"
            rotation:       icon.upright ? 0 : root._heading - (root.map ? root.map.bearing : 0)
        }

        HoverHandler {
            id:         hoverHandler
            enabled:    root.showDetails
        }

        TapHandler {
            enabled:    root.showDetails
            onTapped:   root._pinned = !root._pinned
        }

        Loader {
            x:          parent.width + ScreenTools.defaultFontPixelWidth / 2
            y:          (parent.height - height) / 2
            active:     root._detailsShown

            sourceComponent: Rectangle {
                width:          detailsLabel.contentWidth + ScreenTools.defaultFontPixelWidth * 1.5
                height:         detailsLabel.contentHeight + ScreenTools.defaultFontPixelWidth
                radius:         ScreenTools.defaultFontPixelWidth / 2
                color:          Qt.rgba(qgcPal.window.r, qgcPal.window.g, qgcPal.window.b, 0.9)
                border.color:   qgcPal.text
                border.width:   1

                QGCPalette { id: qgcPal }

                QGCLabel {
                    id:                 detailsLabel
                    anchors.centerIn:   parent
                    text:               root._details()
                    font.pointSize:     ScreenTools.smallFontPointSize
                }
            }
        }
    }
}
