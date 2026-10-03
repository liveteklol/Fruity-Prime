import QtQuick

// PickRow: a label and its value with a caret, opening a page for a choice
// longer than the arrows can cycle.
FocusScope {
    id: row
    property string label
    property string value
    signal clicked()
    activeFocusOnTab: true
    implicitWidth: 300
    implicitHeight: 34
    height: implicitHeight
    readonly property bool lit: area.containsMouse || activeFocus

    Text {
        x: 4
        anchors.verticalCenter: parent.verticalCenter
        text: row.label
        font.family: Theme.pixel; font.pixelSize: 13
        color: Theme.textDim
    }
    Text {
        x: row.width - width - 4
        anchors.verticalCenter: parent.verticalCenter
        text: row.value + "   >"
        font.family: Theme.pixel; font.weight: Font.DemiBold; font.pixelSize: 13
        color: row.lit ? Theme.accent : Theme.text
    }
    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: { Theme.keyboardDriving = false; row.forceActiveFocus(); row.clicked() }
    }
    Keys.onReturnPressed: clicked()
    Keys.onEnterPressed: clicked()
    Keys.onSpacePressed: clicked()
    Keys.onRightPressed: clicked()
}
