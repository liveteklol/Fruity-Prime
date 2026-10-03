import QtQuick

// DeckWordmark: FRUITY over PRIME in Pixelify Bold, each with a three-point
// ink outline and a ten-point drop.
Item {
    id: root
    property real em: 10.81
    property real sizeEms: 7.6
    readonly property real size: Theme.pixelSize(em * sizeEms)
    readonly property real outline: 3
    readonly property real drop: 10

    TextMetrics { id: top; font: face.font; text: "FRUITY" }
    TextMetrics { id: bottom; font: face.font; text: "PRIME" }
    FontMetrics { id: face; font.family: Theme.wordmark; font.weight: Font.Bold; font.pixelSize: root.size }

    implicitWidth: Math.max(top.advanceWidth, bottom.advanceWidth) + outline * 2
    implicitHeight: face.height + face.height * 0.88 + outline * 2 + drop

    component Word: Item {
        id: word
        property string text
        property color colour
        width: parent.width
        height: face.height
        readonly property real inkX: Theme.roundEven((root.width - label.implicitWidth) / 2)
        Text {
            x: word.inkX; y: root.drop
            text: word.text; font: face.font; color: Qt.rgba(0, 0, 0, 140 / 255)
        }
        Repeater {
            model: [[-3, 0], [3, 0], [0, -3], [0, 3], [-3, 3], [3, 3]]
            Text {
                required property var modelData
                x: word.inkX + modelData[0]; y: modelData[1]
                text: word.text; font: face.font; color: Theme.ink
            }
        }
        Text { id: label; x: word.inkX; text: word.text; font: face.font; color: word.colour }
    }

    Word { y: Theme.roundEven(root.outline); text: "FRUITY"; colour: "#f2ede2" }
    Word { y: Theme.roundEven(Theme.roundEven(root.outline) + face.height * 0.88); text: "PRIME"; colour: Theme.accent }
}
