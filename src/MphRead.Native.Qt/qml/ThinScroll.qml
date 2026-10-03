import QtQuick

// The Fluent scroll bar at rest, as the references show it: a two-point
// thumb twelve points into a sixteen-point bar, clear of the arrow buttons.
Item {
    id: bar
    property Flickable flick
    property color thumb: Qt.rgba(133 / 255, 133 / 255, 133 / 255, 1)
    anchors.right: flick ? flick.right : undefined
    anchors.top: flick ? flick.top : undefined
    anchors.bottom: flick ? flick.bottom : undefined
    width: 16
    visible: flick && flick.contentHeight > flick.height + 0.5
    readonly property real track: height - 32
    Rectangle {
        x: 12; width: 2
        y: 16 + (bar.flick ? bar.track * bar.flick.visibleArea.yPosition : 0)
        height: bar.flick ? Math.round(bar.track * bar.flick.visibleArea.heightRatio) : 0
        color: bar.thumb
    }
}
