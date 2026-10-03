import QtQuick

// DeckField: a hole to type in -- PanelLight with both shadows inset, the
// hairline turning into a two-point accent while it has the keyboard.
FocusScope {
    id: field
    property alias text: input.text
    property string placeholder
    property real widthEms: 11
    property real em: Theme.em
    readonly property real size: em * 0.95
    readonly property real padX: Theme.roundEven(size * 0.7)
    readonly property real padY: Theme.roundEven(size * 0.4)
    signal editingFinished()
    readonly property int maxLength: input.maximumLength
    // The pad's A: the on-screen keyboard fills this field.
    function padAccept() {
        if (!Theme.keyboard)
            return false
        Theme.keyboard.open(field)
        return true
    }
    function keyboardText(value) {
        input.text = value
        editingFinished()
    }
    signal accepted()

    implicitWidth: widthEms > 0 ? Theme.roundEven(size * widthEms) : 100
    implicitHeight: metrics.height + padY * 2
    FontMetrics { id: metrics; font: input.font }

    Rectangle {
        anchors.fill: parent
        radius: field.size * 0.35
        color: Theme.panelLight
        // inset 0 2px 0 rgba(0,0,0,.4)
        Rectangle {
            x: 0; y: 0; width: parent.width; height: 2
            radius: parent.radius
            color: Qt.rgba(0, 0, 0, 0.4)
        }
        border.width: input.activeFocus ? 2 : 1
        border.color: input.activeFocus ? Theme.accent : Theme.edge
    }
    TextInput {
        id: input
        focus: true
        x: field.padX
        width: field.width - field.padX * 2
        anchors.verticalCenter: parent.verticalCenter
        font.family: Theme.mono
        font.pointSize: Theme.pt(field.size)
        color: Theme.text
        selectionColor: Qt.rgba(1, 179 / 255, 71 / 255, 90 / 255)
        selectedTextColor: Theme.text
        clip: true
        onEditingFinished: field.editingFinished()
        onAccepted: field.accepted()
        Text {
            visible: input.text.length === 0 && !input.activeFocus
            text: field.placeholder
            font: input.font
            color: Theme.textDim
        }
    }
}
