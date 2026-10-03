import QtQuick

// ButtonToggleRow: a label and an OFF/ON pair of small deck faces -- rust
// when off is chosen, moss when on is.
Item {
    id: row
    property string label
    property bool on: false
    signal toggled(bool on)
    implicitWidth: 200
    implicitHeight: Math.max(32, onButton.height)
    height: implicitHeight

    Text {
        x: 4
        width: offButton.x - 5 - 12
        anchors.verticalCenter: parent.verticalCenter
        text: row.label
        elide: Text.ElideRight
        font.family: Theme.pixel; font.pixelSize: 12
        color: Theme.textDim
    }
    DeckButton {
        id: offButton
        x: onButton.x - 5 - width
        anchors.verticalCenter: parent.verticalCenter
        text: "OFF"
        em: Theme.em
        sizeEms: 0.72; padXEms: 0.62; padYEms: 0.26; lip: 3
        face: row.on ? Theme.slate : Theme.rust
        width: implicitWidth; height: implicitHeight
        onClicked: if (row.on) { row.on = false; row.toggled(false) }
    }
    DeckButton {
        id: onButton
        x: row.width - width
        anchors.verticalCenter: parent.verticalCenter
        text: "ON"
        em: Theme.em
        sizeEms: 0.72; padXEms: 0.72; padYEms: 0.26; lip: 3
        face: row.on ? Theme.moss : Theme.slate
        width: implicitWidth; height: implicitHeight
        onClicked: if (!row.on) { row.on = true; row.toggled(true) }
    }
}
