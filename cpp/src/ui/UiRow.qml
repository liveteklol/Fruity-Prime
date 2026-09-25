import QtQuick

// One setting: a label on the left, a control on the right, the two ends of
// one row rather than of the window (the well is a fixed width).
Item {
    id: root
    property string label
    default property alias content: holder.data
    implicitHeight: 38
    width: parent ? parent.width : 0

    Text {
        anchors.verticalCenter: parent.verticalCenter
        text: root.label
        color: Theme.text
        font.pixelSize: Theme.bodySize
    }
    Item {
        id: holder
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        implicitHeight: childrenRect.height
        width: childrenRect.width
    }
    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Theme.edge
        opacity: 0.5
    }
}
