import QtQuick

// Caption: a small upper-case heading over a group of rows, on a rule.
Item {
    id: caption
    property string text
    implicitHeight: 26
    height: 26
    Text {
        id: label
        y: caption.height - height - 4
        text: caption.text.toUpperCase()
        font.family: Theme.pixel; font.weight: Font.DemiBold; font.pixelSize: 11
        color: Theme.textDim
    }
    Rectangle { y: caption.height - 2.5; width: caption.width; height: 1; color: Theme.edge }
}
