import QtQuick

/// Old-television static for when there is no picture: grey snow, faint scanlines and a slow rolling band, stepped
/// about thirty times a second like an old set
ShaderEffect {
    id: root

    /// Steps the snow; leave it off while the static is hidden or covered
    property bool running: visible

    property real seed: 0
    property real time: 0
    readonly property size resolution: Qt.size(width, height)

    fragmentShader: "qrc:/titanhud/shaders/tvstatic.frag.qsb"

    Timer {
        interval:   33
        repeat:     true
        running:    root.running
        onTriggered: {
            root.seed = Math.random() * 1000
            root.time += interval / 1000
        }
    }
}
