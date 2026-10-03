import QtQuick

// SliderRow: an upper-case label, a thin track from a fixed column and the
// value on the right; Left and Right step it by the row's own step.
FocusScope {
    id: row
    property string label
    property int value: 0
    property int min: 0
    property int max: 100
    property int step: 5
    property real labelWidth: 120
    // The value as the row shows it.
    property string valueText: value + "%"
    signal moved(int value)

    readonly property real gutter: 112
    readonly property real trackX: labelWidth
    readonly property real trackWidth: Math.max(40, width - labelWidth - gutter)
    readonly property bool hot: area.containsMouse && area.mouseX >= labelWidth
    readonly property color dim: Qt.rgba(70 / 255, 76 / 255, 90 / 255, 1)
    readonly property color fill: !enabled ? dim : (activeFocus || hot || area.pressed) ? Theme.shade(Theme.accent, 0.15) : Theme.accent
    readonly property real fraction: (value - min) / Math.max(1, max - min)

    activeFocusOnTab: true
    implicitWidth: 300
    implicitHeight: 34
    height: implicitHeight

    function set(v) {
        v = Math.max(min, Math.min(max, v))
        if (v !== value) {
            value = v
            moved(v)
        }
    }
    function fromPointer(x) {
        const f = Math.max(0, Math.min(1, (x - trackX) / Math.max(1, trackWidth)))
        set(min + Theme.roundEven(f * (max - min)))
    }

    TrackedText {
        x: 4
        y: Math.round((row.height - height) / 2)
        text: row.label.toUpperCase()
        size: 11
        color: row.enabled ? Theme.textDim : row.dim
    }
    Rectangle {
        x: row.trackX; y: row.height / 2 - 2
        width: row.trackWidth; height: 4
        color: Theme.panelLight
    }
    Rectangle {
        x: row.trackX; y: row.height / 2 - 2
        width: row.trackWidth * row.fraction; height: 4
        color: row.fill
    }
    Rectangle {
        x: row.trackX + row.trackWidth * row.fraction - 5
        y: row.height / 2 - 5
        width: 10; height: 10; radius: 5
        color: row.fill
    }
    TrackedText {
        x: row.width - 4 - width
        y: Math.round((row.height - height) / 2)
        text: row.valueText
        size: 12
        color: row.enabled ? Theme.text : row.dim
    }
    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        preventStealing: true
        onPressed: mouse => {
            Theme.keyboardDriving = false
            row.forceActiveFocus()
            if (mouse.x >= row.labelWidth)
                row.fromPointer(mouse.x)
        }
        onPositionChanged: mouse => { if (pressed) row.fromPointer(mouse.x) }
    }
    Keys.onLeftPressed: set(value - step)
    Keys.onRightPressed: set(value + step)
}
