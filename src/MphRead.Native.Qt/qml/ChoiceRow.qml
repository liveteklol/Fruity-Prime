import QtQuick

// ChoiceRow: a label, the current answer in a fixed column and an arrow
// either side of it; Left and Right step, anything else steps forward.
FocusScope {
    id: row
    property string label
    property var options: []
    property int index: 0
    // A square at the right-hand end, for an answer better shown than named.
    property Component preview
    readonly property string value: options.length > 0 ? options[Math.max(0, Math.min(index, options.length - 1))] : ""
    signal changed()

    readonly property real previewRoom: preview ? 52 : 0
    readonly property real leftX: Math.max(Math.min(110, width * 0.42), width - previewRoom - 28 - 180 - 28)
    readonly property real rightX: width - previewRoom - 28

    activeFocusOnTab: true
    implicitWidth: 300
    implicitHeight: preview ? 48 : 34
    height: implicitHeight

    function step(direction) {
        if (options.length === 0)
            return
        index = (index + direction + options.length) % options.length
        changed()
    }

    Rectangle {
        anchors.fill: parent
        visible: row.activeFocus
        radius: 4
        color: Theme.panelLight
    }
    Text {
        x: 4
        anchors.verticalCenter: parent.verticalCenter
        text: row.label
        font.family: Theme.pixel; font.pixelSize: 13
        color: Theme.textDim
    }
    Text {
        readonly property real room: row.rightX - (row.leftX + 28) - 8
        width: Math.min(implicitWidth, Math.max(20, room))
        x: (row.leftX + 28 + row.rightX) / 2 - width / 2
        anchors.verticalCenter: parent.verticalCenter
        text: row.value
        font.family: Theme.pixel; font.weight: Font.DemiBold; font.pixelSize: 13
        color: Theme.text
        elide: Text.ElideRight
    }
    component Arrow: Canvas {
        property bool pointsLeft
        property bool hot
        width: 28; height: row.height
        onHotChanged: requestPaint()
        onPaint: {
            const c = getContext("2d")
            c.reset()
            const cx = width / 2, cy = height / 2
            c.fillStyle = hot ? Theme.accent : Theme.textDim
            c.beginPath()
            if (pointsLeft) { c.moveTo(cx + 4.5, cy - 6); c.lineTo(cx - 4.5, cy); c.lineTo(cx + 4.5, cy + 6) }
            else { c.moveTo(cx - 4.5, cy - 6); c.lineTo(cx + 4.5, cy); c.lineTo(cx - 4.5, cy + 6) }
            c.closePath()
            c.fill()
        }
    }
    Arrow { id: leftArrow; x: row.leftX; pointsLeft: true; hot: area.containsMouse && area.mouseX >= x && area.mouseX < x + width }
    Arrow { id: rightArrow; x: row.rightX; pointsLeft: false; hot: area.containsMouse && area.mouseX >= x && area.mouseX < x + width }
    Loader {
        x: row.width - 52 + 3; y: 3
        width: 46; height: row.height - 6
        sourceComponent: row.preview
    }
    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onPressed: { Theme.keyboardDriving = false; row.forceActiveFocus() }
        // Anywhere that is not the back arrow steps forward.
        onClicked: mouse => row.step(mouse.x >= leftArrow.x && mouse.x < leftArrow.x + 28 ? -1 : 1)
    }
    Keys.onLeftPressed: step(-1)
    Keys.onRightPressed: step(1)
    Keys.onReturnPressed: step(1)
    Keys.onEnterPressed: step(1)
    Keys.onSpacePressed: step(1)
}
