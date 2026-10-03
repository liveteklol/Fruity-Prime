import QtQuick

// DeckChip: a small key over its value in a well, on a four-point lip.
Item {
    id: root
    property string key
    property string value
    readonly property string keyText: key.toUpperCase()
    readonly property string valueText: value.toUpperCase()

    TextMetrics { id: k; font.family: Theme.pixel; font.pixelSize: 10; text: root.keyText }
    TextMetrics { id: v; font.family: Theme.pixel; font.weight: Font.DemiBold; font.pixelSize: Theme.pixelSize(17); text: root.valueText }
    FontMetrics { id: kf; font: k.font }
    FontMetrics { id: vf; font: v.font }

    implicitWidth: (keyText.length > 0 ? Math.max(k.advanceWidth, v.advanceWidth) : v.advanceWidth) + 22
    implicitHeight: keyText.length > 0 ? kf.height + vf.height + 18 : vf.height + 14
    readonly property real faceHeight: height - 4

    Rectangle {
        width: root.width; height: root.faceHeight; radius: 7
        color: Qt.rgba(18 / 255, 21 / 255, 28 / 255, 210 / 255)
    }
    Rectangle {
        y: root.faceHeight; width: root.width; height: 4; radius: 2
        color: Qt.rgba(0, 0, 0, 128 / 255)
    }
    Text {
        visible: root.keyText.length > 0
        x: Theme.roundEven((root.width - k.advanceWidth) / 2); y: 5
        text: root.keyText; font: k.font; color: Theme.textDim
    }
    readonly property real valueY: keyText.length > 0 ? Theme.roundEven(kf.height + 7)
                                                        : Theme.roundEven((faceHeight - vf.height) / 2)
    Rectangle {
        visible: root.keyText.length > 0
        x: 5; y: root.valueY - 2; width: root.width - 10; height: vf.height + 4; radius: 4
        color: Theme.panelLight
    }
    Text {
        x: Theme.roundEven((root.width - v.advanceWidth) / 2); y: root.valueY
        text: root.valueText; font: v.font; color: Theme.text
    }
}
