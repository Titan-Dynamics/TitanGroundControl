import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

import QtLocation
import QtPositioning
import QtQuick.Window
import QtQuick.Effects
import QtQml.Models

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FlyView
import QGroundControl.FlightMap
import QGroundControl.Toolbar
import QGroundControl.Viewer3D
import QGroundControl.TitanMap3D

Item {
    id: _root

    readonly property bool _is3DMode:       QGCViewer3DManager.displayMode === QGCViewer3DManager.View3D
    readonly property bool _keepSceneAlive: QGroundControl.settingsManager.viewer3DSettings.keepSceneAlive.rawValue

    // These should only be used by MainRootWindow
    property var planController:    _planController
    property var guidedController:  _guidedController

    PlanMasterController {
        id:                     _planController
        flyView:                true
        Component.onCompleted:  start()
    }

    property bool   _mainWindowIsMap:       _mapControl ? _mapControl.pipState.state === _mapControl.pipState.fullState : true
    property bool   _isFullWindowItemDark:  _mainWindowIsMap ? (_mapControl ? _mapControl.isSatelliteMap : true) : true
    property var    _activeVehicle:         QGroundControl.multiVehicleManager.activeVehicle
    property var    _missionController:     _planController.missionController
    property var    _geoFenceController:    _planController.geoFenceController
    property var    _rallyPointController:  _planController.rallyPointController
    property real   _margins:               ScreenTools.defaultFontPixelWidth / 2
    property var    _guidedController:      guidedActionsController
    property var    _guidedValueSlider:     guidedValueSlider
    property var    _widgetLayer:           widgetLayer
    property real   _toolsMargin:           ScreenTools.defaultFontPixelWidth * 0.75
    property real   _rightPanelWidth:       ScreenTools.defaultFontPixelWidth * 30
    property var    _mapControl:            mapEngineLoader.item
    property real   _widgetMargin:          ScreenTools.defaultFontPixelWidth * 0.75
    property var    _layerOccluders:        widgetLayer.occluders
    property var    _mapOccluders:          _layerOccluders.all.concat(customOverlay.customOccluders).map(r => Qt.rect(r.x + widgetLayer.x, r.y + widgetLayer.y, r.width, r.height))

    // Titan: the 3D map covers the full-window item; the 2D map is hidden, so not drawn, once it is fully under it
    property bool   _map3DCoversMap:    Map3DState.coversMap && _mainWindowIsMap && map3DLoader.opacity === 1

    // Titan: share of the right half taken by the split-screen 3D map, 0 to 1, and its width. The full-window item
    // narrows by that width with the Settings split's 250 ms animation, which starts once the 3D map's slow first frame
    // is out; the widgets and the PiP keep the whole view. Full-screen video closes the split.
    property bool   _map3DSplitShown:   Map3DState.shown && Map3DState.splitScreen && !QGroundControl.videoManager.fullScreen
    property real   _map3DSplit:        _map3DSplitShown && map3DSplitLoader._framesShown >= 2 ? 1 : 0
    property real   _map3DSplitWidth:   _map3DSplit * mapHolder.width / 2

    Behavior on _map3DSplit {
        NumberAnimation { duration: 250; easing.type: Easing.OutCubic }
    }

    // Titan: the map keeps the vehicle within the widget layer, left of a split-screen 3D map
    property rect   _mapViewportRect:   Qt.rect(widgetLayer.x, widgetLayer.y, Math.min(widgetLayer.width, mapHolder.width - _map3DSplitWidth - _widgetMargin - widgetLayer.x), widgetLayer.height)

    property real   _fullItemZorder:    0
    property real   _pipItemZorder:     QGroundControl.zOrderWidgets

    function dropMainStatusIndicatorTool(showMessages = false) {
        toolbar.dropMainStatusIndicatorTool(showMessages);
    }

    Item {
        id:                 mapHolder
        anchors.fill:       parent

        // Map engine switch (issue #14901): QtLocation FlyViewMap or the
        // experimental GeoMap engine, only one instantiated at a time. The
        // loaded item is positioned/reparented by its PipState, not the Loader.
        // Engine choice is snapshotted at startup (setting is
        // qgcRebootRequired): a live swap would leave stale cross-map state
        // (guided controller registrations, ROI coordinate).
        Loader {
            id: mapEngineLoader
            Component.onCompleted: sourceComponent = QGroundControl.settingsManager.flyViewSettings.useGeoMapEngine.rawValue ? geoMapEngineComponent : qtLocationEngineComponent
        }

        Component {
            id: qtLocationEngineComponent

            FlyViewMap {
                objectName:             "flyViewMap"
                anchors.rightMargin:    pipState.state === pipState.fullState ? _map3DSplitWidth : 0  // Titan: split-screen 3D map
                planMasterController:   _planController
                rightPanelWidth:        ScreenTools.defaultFontPixelHeight * 9
                pipView:                _pipView
                pipMode:                !_mainWindowIsMap
                viewportRect:           _mapViewportRect
                occluders:              _mapOccluders
                mapName:                "FlightDisplayView"
                enabled:                !_is3DMode && !_map3DCoversMap
                visible:                !_is3DMode && !_map3DCoversMap
            }
        }

        Component {
            id: geoMapEngineComponent

            FlyViewGeoMapAdapter {
                objectName:             "flyViewGeoMapAdapter"
                anchors.rightMargin:    pipState.state === pipState.fullState ? _map3DSplitWidth : 0  // Titan: split-screen 3D map
                planMasterController:   _planController
                rightPanelWidth:        ScreenTools.defaultFontPixelHeight * 9
                pipView:                _pipView
                pipMode:                !_mainWindowIsMap
                viewportRect:           _mapViewportRect
                occluders:              _mapOccluders
                chromeTopMargin:        widgetLayer.y + _layerOccluders.topRight.y + _layerOccluders.topRight.height + _widgetMargin
                chromeBottomMargin:     mapHolder.height - widgetLayer.y - Math.min(_layerOccluders.pipView.y, _layerOccluders.virtualJoystickLeft.y) + ScreenTools.defaultFontPixelWidth / 2
                mapName:                "FlightDisplayView"
                enabled:                !_is3DMode && !_map3DCoversMap
                visible:                !_is3DMode && !_map3DCoversMap
            }
        }

        FlyViewVideo {
            id:                     videoControl
            anchors.rightMargin:    pipState.state === pipState.fullState ? _root._map3DSplitWidth : 0    // Titan: split-screen 3D map
            pipView:                _pipView
        }

        PipView {
            id:                     _pipView
            objectName:             "flyViewPipView"
            item1IsFullSettingsKey: "MainFlyWindowIsMap"
            item1:                  _mapControl
            item2:                  QGroundControl.videoManager.hasVideo ? videoControl : null
            show:                   QGroundControl.videoManager.hasVideo && !QGroundControl.videoManager.fullScreen &&
                                        (videoControl.pipState.state === videoControl.pipState.pipState ||
                                         (_mapControl && _mapControl.pipState.state === _mapControl.pipState.pipState))
            z:                      QGroundControl.zOrderWidgets
            topBound:               toolbar.height

            // The PiP is draggable, so widgets only make way for it in their own corner: the virtual joysticks rise
            // above it bottom-left, the tool strip moves to the bottom-left corner from under it top-left, and the
            // bottom-right row moves to the bottom-left corner from under it bottom-right
            property bool inBottomLeft:     x + width / 2 < parent.width / 2 && y + height / 2 > parent.height / 2
            property bool inTopLeft:        x + width / 2 < parent.width / 2 && y + height / 2 <= parent.height / 2
            property bool inBottomRight:    x + width / 2 >= parent.width / 2 && y + height / 2 > parent.height / 2
        }

        FlyViewWidgetLayer {
            id:                     widgetLayer
            objectName:             "flyViewWidgetLayer"
            anchors.top:            parent.top
            anchors.bottom:         parent.bottom
            anchors.left:           parent.left
            anchors.right:          guidedValueSlider.visible ? guidedValueSlider.left : parent.right
            anchors.margins:        _widgetMargin
            anchors.topMargin:      toolbar.height + _widgetMargin
            z:                      _fullItemZorder + 2
            pipViewRect:            _pipView.visible && _pipView.inBottomLeft ? Qt.rect(_pipView.x + _pipView.occupiedRect.x - x, _pipView.y + _pipView.occupiedRect.y - y, _pipView.occupiedRect.width, _pipView.occupiedRect.height) : Qt.rect(0, height, 0, 0)
            pipViewBottom:          _pipView.visible && _pipView.inTopLeft ? _pipView.y + _pipView.occupiedRect.y + _pipView.occupiedRect.height - y : 0
            pipInBottomRight:       _pipView.visible && _pipView.inBottomRight
            mapControl:             _mapControl
            viewer3DCameraController: viewer3DLoader.item ? viewer3DLoader.item.cameraController : null
            visible:                !QGroundControl.videoManager.fullScreen
        }

        FlyViewCustomLayer {
            id:                 customOverlay
            anchors.fill:       widgetLayer
            z:                  _fullItemZorder + 2
            occluders:          _layerOccluders
            mapControl:         _mapControl
            visible:            !QGroundControl.videoManager.fullScreen
        }

        // Development tool for visualizing the occluders, show if needed
        FlyViewOccluderViewer {
            id:                     occluderViewer
            anchors.fill:           widgetLayer
            z:                      widgetLayer.z + 1
            occluders:              _layerOccluders
            customOccluders:        customOverlay.customOccluders
            visible:                false
        }

        GuidedActionsController {
            id:                 guidedActionsController
            missionController:  _missionController
            guidedValueSlider:  _guidedValueSlider
        }

        GuidedActionConfirm {
            id:                 guidedActionConfirm
            anchors.centerIn:   parent
            guidedController:   _guidedController
            guidedValueSlider:  _guidedValueSlider
            z:                  _fullItemZorder + 3
        }

        //-- Guided value slider (e.g. altitude)
        GuidedValueSlider {
            id:                 guidedValueSlider
            objectName:         "guidedValueSlider"
            anchors.right:      parent.right
            anchors.top:        parent.top
            anchors.bottom:     parent.bottom
            anchors.topMargin:  toolbar.height
            z:                  QGroundControl.zOrderTopMost
            visible:            false
        }

        // Titan: the 3D map, shown full screen by map3DLoader or beside the full-window item by map3DSplitLoader
        Component {
            id: map3DViewComponent

            Map3DView {
                map2DCenter:    _mapControl ? _mapControl.center : undefined
                topInset:       toolbar.height
                rightInset:     mapHolder.width - widgetLayer.x - _layerOccluders.topRight.x
            }
        }

        // Titan: Mission Planner style 3D map over the full-window item, below the widgets and the PiP. It fades and
        // grows in and out like the toolbar drawers. Its first frame sets up the renderer and takes long enough to
        // swallow the fade, so the fade waits for that frame, drawn transparent.
        Loader {
            id:             map3DLoader
            anchors.fill:   parent
            z:              _fullItemZorder + 1
            active:         Map3DState.coversMap || opacity > 0
            visible:        active
            opacity:        0
            scale:          0.95

            property int _framesShown: 0

            onActiveChanged: _framesShown = 0

            // Two frames: the first can be one that was already under way when the map was created
            Connections {
                target:                 (map3DLoader.status === Loader.Ready) && (map3DLoader._framesShown < 2) ? map3DLoader.Window.window : null
                function onFrameSwapped() { map3DLoader._framesShown++ }
            }

            states: State {
                name:   "shown"
                when:   Map3DState.coversMap && (map3DLoader._framesShown >= 2)
                PropertyChanges { map3DLoader.opacity: 1; map3DLoader.scale: 1 }
            }

            transitions: [
                Transition {
                    to: "shown"
                    NumberAnimation { properties: "opacity,scale"; duration: 200; easing.type: Easing.OutCubic }
                },
                Transition {
                    from: "shown"
                    NumberAnimation { properties: "opacity,scale"; duration: 150; easing.type: Easing.InCubic }
                }
            ]

            sourceComponent: map3DViewComponent
        }

        // Titan: the 3D map's Split-screen setting puts it on the right half, beside the full-window item and below the
        // widgets and the PiP, which keep the whole view. It slides in from the right edge as the full-window item
        // narrows, once its first frame is out, drawn just past that edge.
        Loader {
            id:             map3DSplitLoader
            x:              parent.width - _root._map3DSplitWidth
            width:          parent.width / 2
            height:         parent.height
            z:              _fullItemZorder + 1
            active:         _root._map3DSplitShown || _root._map3DSplit > 0

            property int _framesShown: 0

            onActiveChanged: _framesShown = 0

            // Two frames: the first can be one that was already under way when the map was created
            Connections {
                target:                 (map3DSplitLoader.status === Loader.Ready) && (map3DSplitLoader._framesShown < 2) ? map3DSplitLoader.Window.window : null
                function onFrameSwapped() { map3DSplitLoader._framesShown++ }
            }

            sourceComponent: map3DViewComponent
        }

        // Titan: divider between the full-window item and the split-screen 3D map
        Rectangle {
            x:          map3DSplitLoader.x - width / 2
            width:      2
            height:     parent.height
            z:          _fullItemZorder + 1
            color:      QGroundControl.globalPalette.window
            visible:    _root._map3DSplitWidth > 0
        }

        Loader {
            id:           viewer3DLoader
            z:            1
            anchors.fill: parent
            visible:      _is3DMode
        }

        Connections {
            target: QGCViewer3DManager
            function onDisplayModeChanged() {
                if (QGCViewer3DManager.displayMode === QGCViewer3DManager.View3D) {
                    if (!viewer3DLoader.item) {
                        viewer3DLoader.setSource("qrc:/qml/QGroundControl/Viewer3D/Models3D/Viewer3DModel.qml")
                    }
                } else if (!_keepSceneAlive) {
                    viewer3DLoader.source = ""
                }
            }
        }
    }

    // Frosted glass blur behind the toolbar
    Item {
        id:             toolbarBlur
        anchors.left:   parent.left
        anchors.right:  parent.right
        anchors.top:    parent.top
        height:         toolbar.height
        clip:           true
        visible:        toolbar.visible

        ShaderEffectSource {
            id:             blurSource
            sourceItem:     mapHolder
            anchors.fill:   parent
            sourceRect:     Qt.rect(0, 0, toolbarBlur.width, toolbarBlur.height)
            visible:        false
        }

        MultiEffect {
            anchors.fill:   parent
            source:         blurSource
            blurEnabled:    true
            blur:           0.6
            blurMax:        48
        }
    }

    FlyViewToolBar {
        id:                 toolbar
        visible:            !QGroundControl.videoManager.fullScreen
    }
}
