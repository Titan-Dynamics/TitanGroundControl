import QtQuick
import QtQuick.Window

import QGroundControl
import QGroundControl.Controls

Item {
    id:         _root
    width:      _pipSize
    height:     _pipSize * (9/16)
    visible:    item2 && item2.pipState !== item2.pipState.window && show

    property var    item1:                  null    // Required
    property var    item2:                  null    // Optional, may come and go
    property string item1IsFullSettingsKey          // Settings key to save whether item1 was saved in full mode
    property bool   show:                   true
    property real   topBound:               0       // Set externally to toolbar height

    readonly property string _pipExpandedSettingsKey: "IsPIPVisible"

    property var    _fullItem
    property var    _pipOrWindowItem
    property alias  _windowContentItem: window.contentItem
    property alias  _pipContentItem:    pipContent
    property bool   _isExpanded:        true
    property real   _pipSize:           parent.width * 0.35
    property real   _maxSize:           0.75                // Percentage of parent control size
    property real   _minSize:           0.10
    property real   _minAbsoluteWidth:  ScreenTools.defaultFontPixelWidth * 20
    property real   _iconSize:          ScreenTools.defaultFontPixelHeight * 2
    property bool   _componentComplete: false
    property real   _margin:            ScreenTools.defaultFontPixelWidth * 0.75
    property bool   _isOnRight:         parent ? (x + width / 2 > parent.width / 2) : false
    property real   _prevHeight:        0
    property real   _prevParentHeight:  0

    Component.onCompleted: {
        _initForItems()
        _placementTimer.start()
    }

    // Delay initial placement and activation until parent layout has settled
    Timer {
        id:         _placementTimer
        interval:   200
        onTriggered: {
            _root.x = _margin
            _root.y = _root.parent.height - _root.height - _margin
            _prevHeight = _root.height
            _prevParentHeight = _root.parent.height
            _componentComplete = true
        }
    }

    onItem2Changed: _initForItems()

    // When height changes (from resize), pin bottom edge by adjusting y
    onHeightChanged: {
        if (_componentComplete && parent && _prevHeight > 0) {
            y += _prevHeight - height
            var minY = topBound + _margin
            var maxY = parent.height - height - _margin
            y = Math.max(minY, Math.min(y, maxY))
        }
        _prevHeight = height
    }

    // When width changes (from resize), keep the snapped edge pinned
    onWidthChanged: {
        if (_componentComplete && parent && _isOnRight) {
            // Keep right edge pinned
            var rightEdge = parent.width - _margin
            x = rightEdge - width
        }
    }

    function showWindow() {
        window.width = _root.width
        window.height = _root.height
        window.show()
    }

    function _snapToEdge() {
        if (!parent) return

        var parentW = parent.width
        var parentH = parent.height
        var centerX = x + width / 2

        // Snap to left or right edge based on which half the center is in
        var targetX = centerX < parentW / 2
            ? _margin
            : parentW - width - _margin

        // Keep Y clamped within bounds (respect toolbar at top)
        var minY = topBound + _margin
        var targetY = Math.max(minY, Math.min(y, parentH - height - _margin))

        snapAnimX.to = targetX
        snapAnimY.to = targetY
        snapAnimX.start()
        snapAnimY.start()
    }

    NumberAnimation {
        id:         snapAnimX
        target:     _root
        property:   "x"
        duration:   250
        easing.type: Easing.OutCubic
    }

    NumberAnimation {
        id:         snapAnimY
        target:     _root
        property:   "y"
        duration:   250
        easing.type: Easing.OutCubic
    }

    function _initForItems() {
        var item1IsFull = QGroundControl.loadBoolGlobalSetting(item1IsFullSettingsKey, true)
        if (item1 && item2) {
            item1.pipState.state = item1IsFull ? item1.pipState.fullState : item1.pipState.pipState
            item2.pipState.state = item1IsFull ? item2.pipState.pipState : item2.pipState.fullState
            _fullItem = item1IsFull ? item1 : item2
            _pipOrWindowItem = item1IsFull ? item2 : item1
        } else {
            item1.pipState.state = item1.pipState.fullState
            _fullItem = item1
            _pipOrWindowItem = null
        }
        _setPipIsExpanded(QGroundControl.loadBoolGlobalSetting(_pipExpandedSettingsKey, true))
    }

    function _swapPip() {
        var item1IsFull = false
        if (item1.pipState.state === item1.pipState.fullState) {
            item1.pipState.state = item1.pipState.pipState
            item2.pipState.state = item2.pipState.fullState
            _fullItem = item2
            _pipOrWindowItem = item1
            item1IsFull = false
        } else {
            item1.pipState.state = item1.pipState.fullState
            item2.pipState.state = item2.pipState.pipState
            _fullItem = item1
            _pipOrWindowItem = item2
            item1IsFull = true
        }
        QGroundControl.saveBoolGlobalSetting(item1IsFullSettingsKey, item1IsFull)
    }

    function _setPipIsExpanded(isExpanded) {
        QGroundControl.saveBoolGlobalSetting(_pipExpandedSettingsKey, isExpanded)
        _isExpanded = isExpanded
    }

    Window {
        id:         window
        visible:    false
        onClosing: {
            var item = contentItem.children[0]
            if (item) {
                item.pipState.windowAboutToClose()
                item.pipState.state = item.pipState.pipState
            }
        }
    }

    Item {
        id:             pipContent
        anchors.fill:   parent
        visible:        _isExpanded
        clip:           true
    }

    // Dark overlay that fades in on hover for icon contrast
    Rectangle {
        id:             hoverOverlay
        anchors.fill:   parent
        color:          "black"
        opacity:        pipMouseArea.containsMouse ? 0.35 : 0.0
        visible:        _isExpanded

        Behavior on opacity {
            NumberAnimation { duration: 250; easing.type: Easing.InOutQuad }
        }
    }

    // Main mouse area for dragging and click-to-swap
    MouseArea {
        id:             pipMouseArea
        anchors.fill:   parent
        enabled:        _isExpanded
        preventStealing: true
        hoverEnabled:   true
        drag.target:    _root
        drag.threshold: 5
        drag.minimumX:  _margin
        drag.maximumX:  _root.parent ? _root.parent.width - _root.width - _margin : 0
        drag.minimumY:  topBound + _margin
        drag.maximumY:  _root.parent ? _root.parent.height - _root.height - _margin : 0

        property real _startX: 0
        property real _startY: 0

        onPressed: {
            snapAnimX.stop()
            snapAnimY.stop()
            _startX = _root.x
            _startY = _root.y
        }

        onReleased: {
            if (Math.abs(_root.x - _startX) > 5 || Math.abs(_root.y - _startY) > 5) {
                _snapToEdge()
            }
        }

        onClicked: _swapPip()
    }

    // MouseArea to drag in order to resize the PiP area
    MouseArea {
        id:                 pipResize
        anchors.fill:       pipResizeIcon
        preventStealing:    true
        cursorShape:        Qt.PointingHandCursor

        property real initialX:     0
        property real initialWidth: 0

        onPressed: (mouse) => {
            pipResize.anchors.fill = undefined
            pipResize.initialX = mouse.x
            pipResize.initialWidth = _root.width
        }

        onReleased: pipResize.anchors.fill = pipResizeIcon

        onPositionChanged: (mouse) => {
            if (pipResize.pressed) {
                var parentWidth = _root.parent.width
                var delta = mouse.x - pipResize.initialX
                // When on right side, dragging left (negative delta) should grow
                var newWidth = _isOnRight
                    ? pipResize.initialWidth - delta
                    : pipResize.initialWidth + delta
                var minWidth = Math.max(parentWidth * _minSize, _minAbsoluteWidth)
                if (newWidth < parentWidth * _maxSize && newWidth > minWidth) {
                    _pipSize = newWidth
                }
            }
        }
    }

    // Resize icon — corner on the outward side
    Image {
        id:             pipResizeIcon
        source:         "/qmlimages/pipResize.svg"
        fillMode:       Image.PreserveAspectFit
        mipmap:         true
        mirror:         _isOnRight
        anchors.top:    parent.top
        anchors.right:  parent.right
        visible:        _isExpanded && (ScreenTools.isMobile || pipMouseArea.containsMouse)
        height:         _iconSize
        width:          _iconSize
        sourceSize.height:  height
    }

    // Check min/max constraints on pip size when parent is resized
    Connections {
        target: _root.parent

        function onWidthChanged() {
            if (!_componentComplete) return
            var parentWidth = _root.parent.width
            var minWidth = Math.max(parentWidth * _minSize, _minAbsoluteWidth)
            if (_root.width > parentWidth * _maxSize) {
                _pipSize = parentWidth * _maxSize
            } else if (_root.width < minWidth) {
                _pipSize = minWidth
            }
            // Keep snapped to the same edge during parent resize
            if (_isOnRight) {
                _root.x = parentWidth - _root.width - _margin
            } else {
                _root.x = _margin
            }
        }

        function onHeightChanged() {
            if (!_componentComplete) return
            var parentHeight = _root.parent.height
            // If PiP was at the bottom, keep it there
            var wasAtBottom = (_prevParentHeight > 0) &&
                (_root.y + _root.height + _margin * 3 >= _prevParentHeight)
            if (wasAtBottom) {
                _root.y = parentHeight - _root.height - _margin
            } else {
                _root.y = Math.max(topBound + _margin, Math.min(_root.y, parentHeight - _root.height - _margin))
            }
            _prevParentHeight = parentHeight
        }
    }

    // Pip to Window
    Image {
        id:             popupPIP
        source:         "/qmlimages/PiP.svg"
        mipmap:         true
        fillMode:       Image.PreserveAspectFit
        anchors.left:   parent.left
        anchors.top:    parent.top
        visible:        _isExpanded && !ScreenTools.isMobile && pipMouseArea.containsMouse
        height:         _iconSize
        width:          _iconSize
        sourceSize.height:  height

        MouseArea {
            anchors.fill:   parent
            onClicked:      _pipOrWindowItem.pipState.state = _pipOrWindowItem.pipState.windowState
        }
    }

    Image {
        id:             hidePIP
        source:         "/qmlimages/pipHide.svg"
        mipmap:         true
        fillMode:       Image.PreserveAspectFit
        mirror:         _isOnRight
        anchors.left:   parent.left
        anchors.bottom: parent.bottom
        visible:        _isExpanded && (ScreenTools.isMobile || pipMouseArea.containsMouse)
        height:         _iconSize
        width:          _iconSize
        sourceSize.height:  height
        MouseArea {
            anchors.fill:   parent
            onClicked:      _root._setPipIsExpanded(false)
        }
    }

    Rectangle {
        id:                     showPip
        anchors.left:           parent.left
        anchors.bottom:         parent.bottom
        height:                 ScreenTools.defaultFontPixelHeight * 2
        width:                  ScreenTools.defaultFontPixelHeight * 2
        radius:                 ScreenTools.defaultFontPixelHeight / 3
        visible:                !_isExpanded
        color:                  _fullItem.pipState.isDark ? Qt.rgba(0,0,0,0.75) : Qt.rgba(0,0,0,0.5)
        Image {
            width:              parent.width  * 0.75
            height:             parent.height * 0.75
            sourceSize.height:  height
            source:             "/res/buttonRight.svg"
            mipmap:             true
            fillMode:           Image.PreserveAspectFit
            mirror:             _isOnRight
            anchors.verticalCenter:     parent.verticalCenter
            anchors.horizontalCenter:   parent.horizontalCenter
        }
        MouseArea {
            anchors.fill:   parent
            onClicked:      _root._setPipIsExpanded(true)
        }
    }

    // Swap icon sides when docked left vs right
    states: State {
        name: "dockedRight"
        when: _isOnRight

        AnchorChanges {
            target: pipResizeIcon
            anchors.right: undefined
            anchors.left: _root.left
        }
        AnchorChanges {
            target: popupPIP
            anchors.left: undefined
            anchors.right: _root.right
        }
        AnchorChanges {
            target: hidePIP
            anchors.left: undefined
            anchors.right: _root.right
        }
        AnchorChanges {
            target: showPip
            anchors.left: undefined
            anchors.right: _root.right
        }
    }
}
