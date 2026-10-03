import QtQuick

// FieldRow: a label and a Fluent text box held to the right.
FocusScope {
    id: row
    property string label
    property alias text: input.text
    property real boxWidth: 150
    signal edited(string text)
    readonly property int maxLength: input.maximumLength
    // The pad's A: the on-screen keyboard fills this box.
    function padAccept() {
        if (!Theme.keyboard)
            return false
        Theme.keyboard.open(row)
        return true
    }
    function keyboardText(value) {
        input.text = value
        edited(value)
    }
    implicitWidth: 300
    implicitHeight: 36
    height: implicitHeight

    Text {
        x: 4
        anchors.verticalCenter: parent.verticalCenter
        text: row.label
        font.family: Theme.pixel; font.pixelSize: 13
        color: Theme.textDim
    }
    // The Fluent dark TextBox: a faint fill and a light hairline; with the
    // keyboard, black inside the system accent, heavier along its foot.
    Rectangle {
        id: box
        x: row.width - width
        width: row.boxWidth
        height: Math.round(metrics.height) + 8 + 2
        anchors.verticalCenter: parent.verticalCenter
        radius: 4
        color: input.activeFocus ? "#000000" : Qt.rgba(1, 1, 1, 0.045)
        border.width: 1
        border.color: input.activeFocus ? "#0078d7" : Qt.rgba(1, 1, 1, 0.55)
        Rectangle {
            visible: input.activeFocus
            x: 2; width: parent.width - 4
            y: parent.height - 2; height: 1
            color: "#0078d7"
        }
        FontMetrics { id: metrics; font: input.font }
        TextInput {
            id: input
            focus: true
            x: 8; width: parent.width - 16
            anchors.verticalCenter: parent.verticalCenter
            font.family: Theme.pixel; font.pixelSize: 13
            color: Theme.text
            selectionColor: Qt.rgba(1, 179 / 255, 71 / 255, 90 / 255)
            selectedTextColor: Theme.text
            clip: true
            onTextEdited: row.edited(text)
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.IBeamCursor
            onPressed: mouse => { input.forceActiveFocus(); mouse.accepted = false }
        }
    }
}
