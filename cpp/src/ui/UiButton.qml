import QtQuick

// One thing to press. Acts on the release inside it, never on the press: a
// drag that starts on a button is a scroll, not a click (Tap.cs).
Rectangle {
    id: root
    property alias text: label.text
    property bool primary: false
    property bool enabledLook: true
    signal clicked

    implicitWidth: label.implicitWidth + 40
    implicitHeight: 40
    radius: Theme.radius
    color: !enabledLook ? Theme.panel : area.pressed ? Qt.lighter(Theme.panelLight, 1.4)
                                                     : area.containsMouse ? Theme.panelLight : Theme.panel
    border.width: 1
    border.color: primary ? Theme.accent : Theme.edge

    Text {
        id: label
        anchors.centerIn: parent
        color: !root.enabledLook ? Theme.dim : root.primary ? Theme.accent : Theme.text
        font.pixelSize: Theme.bodySize
        font.bold: root.primary
    }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        enabled: root.enabledLook
        onClicked: root.clicked()
    }
}
