import QtQuick

// ToggleRow: one setting that is on or off, a pill on the right.
FocusScope {
    id: row
    property string label
    property bool on: false
    signal changed()
    activeFocusOnTab: true
    implicitWidth: 300
    implicitHeight: 34
    height: implicitHeight
    function flip() { on = !on; changed() }

    Rectangle { anchors.fill: parent; visible: row.activeFocus; radius: 4; color: Theme.panelLight }
    Text {
        x: 4; anchors.verticalCenter: parent.verticalCenter
        text: row.label
        font.family: Theme.pixel; font.pixelSize: 13
        color: Theme.textDim
    }
    Rectangle {
        id: track
        width: 40; height: 20; radius: 10
        x: row.width - width - 4
        y: (row.height - height) / 2
        color: row.on ? Theme.accent : Theme.edge
        Rectangle {
            width: 14; height: 14; radius: 7
            x: (row.on ? track.width - 10 : 10) - 7
            y: 3
            color: row.on ? Theme.ink : Theme.textDim
        }
    }
    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onPressed: { Theme.keyboardDriving = false; row.forceActiveFocus() }
        onClicked: row.flip()
    }
    Keys.onReturnPressed: flip()
    Keys.onEnterPressed: flip()
    Keys.onSpacePressed: flip()
    Keys.onLeftPressed: flip()
    Keys.onRightPressed: flip()
}
