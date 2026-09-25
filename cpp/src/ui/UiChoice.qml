import QtQuick

// A choice with an arrow either side, the way every settings row of the C#
// picks one of a list (ChoiceRow). The whole control is one width whatever
// the value is, so a column of rows ends in a column.
Item {
    id: root
    property var values: []
    property int index: 0
    property int valueWidth: 200
    signal picked(int index)

    implicitWidth: 300
    implicitHeight: 34

    function nameAt(i) {
        if (i < 0 || i >= values.length)
            return "-";
        var v = values[i];
        return (typeof v === "object" && v !== null) ? v.name : v;
    }
    function step(delta) {
        if (values.length === 0)
            return;
        index = (index + delta + values.length) % values.length;
        picked(index);
    }

    UiButton {
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        implicitWidth: 34
        implicitHeight: 34
        text: "\u2039"
        onClicked: root.step(-1)
    }
    Rectangle {
        anchors.centerIn: parent
        width: root.valueWidth
        height: 34
        radius: Theme.radius
        color: Theme.panel
        border.width: 1
        border.color: Theme.edge
        Text {
            anchors.fill: parent
            anchors.margins: 8
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            text: root.nameAt(root.index)
            color: Theme.text
            font.pixelSize: Theme.bodySize
        }
    }
    UiButton {
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        implicitWidth: 34
        implicitHeight: 34
        text: "\u203a"
        onClicked: root.step(1)
    }
}
