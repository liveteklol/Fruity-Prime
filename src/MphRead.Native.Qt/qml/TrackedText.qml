import QtQuick

// DeckText.DrawTracked: the display face with 0.04 em between letters and a
// fixed 0.42 em for a space.
Row {
    id: tracked
    property string text
    property real size: 13
    property bool strong: true
    property color color: Theme.text
    readonly property real tracking: size * 0.04
    spacing: 0
    Repeater {
        model: tracked.text.split(" ")
        Row {
            required property string modelData
            required property int index
            Item { visible: index > 0; width: tracked.size * 0.42 + tracked.tracking; height: 1 }
            Text {
                text: modelData
                font.family: Theme.pixel
                font.weight: tracked.strong ? Font.DemiBold : Font.Normal
                font.pointSize: Theme.pt(tracked.size)
                font.letterSpacing: tracked.tracking
                color: tracked.color
            }
        }
    }
}
