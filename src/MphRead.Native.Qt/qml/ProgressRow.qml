import QtQuick

// ProgressRow: the stage on the left, the share on the right, and a round
// bar under both.
Item {
    id: row
    property real fraction: 0
    property string stage
    height: 44
    Text {
        y: 2
        text: row.stage
        font.family: Theme.pixel; font.pixelSize: 12
        color: Theme.textDim
    }
    Text {
        y: 2
        anchors.right: parent.right
        text: Theme.roundEven(Math.max(0, Math.min(1, row.fraction)) * 100) + "%"
        font.family: Theme.pixel; font.weight: Font.DemiBold; font.pixelSize: 12
        color: Theme.text
    }
    Rectangle {
        y: row.height - 8 - 2
        width: row.width; height: 8; radius: 4
        color: Theme.ink
        Rectangle {
            readonly property real filled: row.width * Math.max(0, Math.min(1, row.fraction))
            visible: filled > 1
            width: Math.max(filled, 8); height: 8; radius: 4
            color: Theme.accent
        }
    }
}
