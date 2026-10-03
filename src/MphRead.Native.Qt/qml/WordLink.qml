import QtQuick

// UiWord: a word that is a link -- the display face, the accent when the
// pointer or the keyboard is on it.
FocusScope {
    id: word
    property string text
    property real size: 15
    property color colour: Theme.text
    signal clicked()
    activeFocusOnTab: true
    implicitWidth: label.implicitWidth + 4
    implicitHeight: label.implicitHeight + 2
    width: implicitWidth
    height: implicitHeight
    Text {
        id: label
        text: word.text
        font.family: Theme.pixel
        font.pointSize: Theme.pt(word.size)
        color: !word.enabled ? Theme.textDim
             : (area.containsMouse || word.activeFocus) ? Theme.shade(Theme.accent, 0.2)
             : word.colour
    }
    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onPressed: { Theme.keyboardDriving = false; word.forceActiveFocus() }
        onClicked: word.clicked()
    }
    Keys.onReturnPressed: clicked()
    Keys.onEnterPressed: clicked()
    Keys.onSpacePressed: clicked()
}
