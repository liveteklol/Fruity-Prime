import QtQuick

// UiListRow: what it is on the left, what is worth knowing on the right;
// lit, it wears an accent caret rather than a fill.
Item {
    id: row
    property string title
    property string detail
    property bool selected: false
    readonly property bool lit: area.containsMouse || activeFocus || selected
    signal clicked()
    signal activated()
    activeFocusOnTab: true
    height: 30

    Rectangle { visible: row.lit; x: 0; y: 6; width: 3; height: row.height - 12; color: Theme.accent }
    Text {
        id: detailText
        visible: row.detail.length > 0
        width: Math.min(implicitWidth, Math.max(40, row.width * 0.45))
        x: row.width - width - 4
        anchors.verticalCenter: parent.verticalCenter
        text: row.detail
        font.family: Theme.pixel; font.pixelSize: 12
        color: Theme.textDim
        elide: Text.ElideRight
        maximumLineCount: 1
    }
    Text {
        readonly property real rightEdge: row.detail.length > 0 ? row.width - detailText.width - 14 : row.width
        x: 14
        width: Math.min(implicitWidth, Math.max(40, rightEdge - 14))
        anchors.verticalCenter: parent.verticalCenter
        text: row.title
        font.family: Theme.pixel; font.weight: Font.DemiBold; font.pixelSize: 14
        color: row.lit ? Theme.accent : Theme.text
        elide: Text.ElideRight
        maximumLineCount: 1
    }
    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: { Theme.keyboardDriving = false; row.forceActiveFocus(); row.clicked() }
        onDoubleClicked: { row.clicked(); row.activated() }
    }
    Keys.onReturnPressed: { clicked(); activated() }
    Keys.onEnterPressed: { clicked(); activated() }
    Keys.onSpacePressed: { clicked(); activated() }
}
