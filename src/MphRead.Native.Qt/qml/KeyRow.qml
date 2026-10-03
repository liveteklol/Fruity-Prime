import QtQuick

// KeyRow: what a control does on the left, what it is bound to in a box on
// the right; a click or Enter listens, and the next key, mouse button or
// wheel turn is the answer (the page hands the keys over).
FocusScope {
    id: row
    property string label
    property string binding
    property bool listening: false
    property bool keysOnly: false
    property real labelWidth: 160
    signal listen()
    signal mouse(int button)
    signal wheel(bool up)
    // The pad's A: the matching pad row, or a word that there is none.
    signal padAccepted()
    function padAccept() {
        padAccepted()
        return true
    }

    activeFocusOnTab: true
    implicitWidth: 300
    implicitHeight: 32
    height: implicitHeight

    TrackedText {
        x: 4
        y: Math.round((row.height - height) / 2)
        text: row.label
        size: 12
        color: Theme.text
    }
    Rectangle {
        id: box
        x: row.labelWidth; y: 2
        width: Math.max(60, row.width - row.labelWidth - 4)
        height: row.height - 4
        radius: 4
        color: Theme.panelLight
        border.width: 1
        border.color: row.listening ? Theme.warm : (row.activeFocus || area.containsMouse) ? Theme.accent : Theme.edge
        Text {
            anchors.centerIn: parent
            width: Math.min(implicitWidth, parent.width - 12)
            elide: Text.ElideRight
            text: row.listening ? (row.keysOnly ? "press a key" : "press a key, a mouse button or the wheel") : row.binding
            font.family: Theme.pixel; font.weight: Font.DemiBold
            font.pointSize: Theme.pt(12); font.letterSpacing: 12 * 0.04
            color: row.listening ? Theme.warm : Theme.text
        }
    }
    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.AllButtons
        cursorShape: Qt.PointingHandCursor
        onPressed: mouse => {
            Theme.keyboardDriving = false
            row.forceActiveFocus()
            if (row.listening)
                row.mouse(mouse.button)
        }
        onReleased: mouse => {
            if (!row.listening && mouse.button === Qt.LeftButton && mouse.x >= box.x)
                row.listen()
        }
        onWheel: wheel => {
            if (row.listening && wheel.angleDelta.y !== 0)
                row.wheel(wheel.angleDelta.y > 0)
            else
                wheel.accepted = false
        }
    }
    Keys.onReturnPressed: if (!listening) listen()
    Keys.onEnterPressed: if (!listening) listen()
    Keys.onSpacePressed: if (!listening) listen()
}
