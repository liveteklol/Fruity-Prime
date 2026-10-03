import QtQuick

// ControllerKeyboard: text entry with the pad -- the keys as words, a
// preview, and Space/Shift/Delete/Done/Cancel, over the field it fills.
// Modal: the focus stays inside until it closes.
FocusScope {
    id: board
    anchors.fill: parent
    z: 100
    visible: field !== null
    // The field being filled: it has text, maxLength and keyboardText(text).
    property Item field: null
    property string text
    property bool upper: false
    readonly property bool navModal: visible

    function open(target) {
        field = target
        text = target.text
        upper = false
        Theme.keyboardDriving = true
        firstKey.forceActiveFocus()
    }
    function close(accept) {
        if (!field)
            return
        const target = field
        field = null
        if (accept)
            target.keyboardText(text)
        target.forceActiveFocus()
    }
    function append(value) {
        const limit = Math.min(1024, field && field.maxLength > 0 ? field.maxLength : 1024)
        if (text.length + value.length > limit)
            return
        text += value
    }

    // Swallows the pointer around the panel.
    MouseArea { anchors.fill: parent; enabled: board.visible }

    Rectangle {
        id: panel
        readonly property point centre: board.field
            ? board.field.mapToItem(board, board.field.width / 2, board.field.height / 2) : Qt.point(0, 0)
        width: column.width + 32
        height: column.height + 32
        x: Math.max(0, Math.min(board.width - width, centre.x - width / 2))
        y: Math.max(0, Math.min(board.height - height, centre.y - height / 2))
        color: Theme.panel
        border.width: 1
        border.color: Theme.accent

        Column {
            id: column
            x: 16; y: 16
            spacing: 8
            Text {
                width: Math.min(540, Math.max(implicitWidth, 1))
                text: board.text
                wrapMode: Text.Wrap
                font.family: Theme.pixel; font.pixelSize: 14
                color: Theme.text
            }
            Repeater {
                model: ["1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm", ".:-_/@[]+"]
                Row {
                    required property string modelData
                    required property int index
                    readonly property int rowIndex: index
                    spacing: 12
                    Repeater {
                        model: modelData.split("")
                        Item {
                            required property string modelData
                            required property int index
                            width: Math.max(28, key.implicitWidth)
                            height: key.implicitHeight
                            WordLink {
                                id: key
                                size: 24
                                text: board.upper ? modelData.toUpperCase() : modelData
                                onClicked: board.append(text)
                                Component.onCompleted: if (parent.parent.rowIndex === 0 && index === 0) board.firstKeyItem = key
                            }
                        }
                    }
                }
            }
            Row {
                spacing: 16
                WordLink { size: 19; text: "Space"; onClicked: board.append(" ") }
                WordLink { size: 19; text: "Shift"; onClicked: board.upper = !board.upper }
                WordLink { size: 19; text: "Delete"; onClicked: board.text = board.text.slice(0, -1) }
                WordLink { size: 19; text: "Done"; onClicked: board.close(true) }
                WordLink { size: 19; text: "Cancel"; onClicked: board.close(false) }
            }
        }
    }
    property Item firstKeyItem
    readonly property Item firstKey: firstKeyItem
    Keys.onEscapePressed: close(false)
}
