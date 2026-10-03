import QtQuick
import FruityPrime.Launcher

// ServerRow: one server as a slab with its map faded behind -- the flag,
// the name (its tail in the accent), map, mode, players and ping.
Item {
    id: row
    property var modelData: ({})
    property bool selected: false
    readonly property string name: modelData.name || ""
    readonly property string endpoint: modelData.endpoint || ""
    readonly property bool asking: modelData.asking === true
    readonly property bool answered: modelData.answered === true
    readonly property bool live: answered && !asking
    readonly property bool lit: (area.containsMouse || activeFocus || selected) && live
    readonly property bool down: area.pressed && live
    signal clicked()
    signal activated()
    activeFocusOnTab: true
    height: 29.6

    // ServerRow.Columns, in the row's own em.
    readonly property real rowEm: 13.3333
    readonly property bool narrow: Window.width <= 560
    readonly property real padX: 0.6 * rowEm
    readonly property real gap: 0.55 * rowEm
    readonly property real numberWidth: 2.6 * 0.86 * rowEm
    readonly property real flagX: padX
    readonly property real nameX: padX + 20 + gap
    readonly property real rest: Math.max(0, width - padX * 2 - gap * 5 - 20 - numberWidth * 2)
    readonly property real free: Math.max(0, width - padX * 2 - gap * 3 - 20 - numberWidth)
    readonly property real nameWidth: narrow ? free * (1.4 / 2.4) : rest * (1.5 / 3.8)
    readonly property real mapWidth: narrow ? free - nameWidth : rest * (1.3 / 3.8)
    readonly property real modeWidth: narrow ? 0 : rest - nameWidth - mapWidth
    readonly property real mapX: nameX + nameWidth + gap
    readonly property real modeX: mapX + mapWidth + gap
    readonly property real playersX: modeX + modeWidth + gap
    readonly property real pingX: narrow ? mapX + mapWidth + gap : playersX + numberWidth + gap

    // FindNameTail: a short ".tag" or "#n" at the end wears the accent.
    readonly property int cut: {
        const n = name
        for (let at = n.length - 1; at > 0; --at) {
            if (n.length - at > 7)
                break
            const ch = n[at]
            if (ch === "." || ch === "#")
                return at < n.length - 1 ? at : -1
            if (ch === "-")
                break
        }
        return -1
    }

    Item {
        id: slab
        y: row.down ? 3 : 0
        width: row.width; height: 29.6
        scale: area.containsMouse && !row.down && row.live ? 1.012 : 1
        opacity: row.asking ? 0.72 : 1
        readonly property real radius: 0.4 * row.rowEm

        Rectangle {
            width: parent.width; height: parent.height
            y: row.lit ? 5 : 3
            radius: slab.radius
            color: Qt.rgba(0, 0, 0, row.lit ? 0.5 : 0.45)
        }
        Rectangle {
            readonly property real spread: row.lit || row.selected ? 2 : 1
            x: -spread; y: -spread; width: parent.width + spread * 2; height: parent.height + spread * 2
            radius: slab.radius + spread
            color: row.selected ? Theme.accent : row.lit ? Theme.hoverRing : Theme.edge
        }
        Rectangle {
            anchors.fill: parent
            radius: slab.radius
            color: Theme.panelDeep
            clip: true
            Image {
                id: shot
                visible: row.answered && status === Image.Ready
                source: row.answered ? shell.mapShot(row.modelData.roomKey || "") : ""
                width: parent.width
                height: sourceSize.width > 0 ? sourceSize.height * width / sourceSize.width : 0
                y: (parent.height - height) / 2
                opacity: row.lit ? 0.72 : 0.5
                smooth: false
                asynchronous: !Theme.still
                cache: true
            }
            Rectangle {
                visible: shot.visible
                anchors.fill: parent
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: Qt.rgba(10 / 255, 12 / 255, 16 / 255, 240 / 255) }
                    GradientStop { position: 0.45; color: Qt.rgba(10 / 255, 12 / 255, 16 / 255, 184 / 255) }
                    GradientStop { position: 1; color: Qt.rgba(10 / 255, 12 / 255, 16 / 255, 224 / 255) }
                }
            }
        }
        ServerBadge {
            x: Theme.roundEven(row.flagX)
            y: Theme.roundEven((29.6 - 1.05 * row.rowEm) / 2)
            width: 20; height: 14
            endpoint: row.endpoint
            answered: row.answered
        }
        // The name, with a two-point drop under it; its tail in the accent.
        Item {
            id: nameBox
            x: Theme.roundEven(row.nameX); width: Theme.roundEven(row.nameWidth); height: 29.6
            clip: true
            readonly property color ink: row.asking || !row.answered ? Theme.textDim : Theme.text
            readonly property string stem: row.cut >= 0 ? row.name.slice(0, row.cut) : row.name
            readonly property string tail: row.cut >= 0 ? row.name.slice(row.cut) : ""
            component Shadowed: Item {
                property alias text: over.text
                property alias color: over.color
                property real room
                // Avalonia's run leaves a trailing space out of its width.
                readonly property real inkWidth: Math.min(over.width, trimmed.advanceWidth)
                TextMetrics { id: trimmed; font: over.font; text: over.text.replace(/\s+$/, "") }
                width: over.width; height: over.height
                anchors.verticalCenter: parent.verticalCenter
                Text { y: 2; text: over.text; font: over.font; width: over.width; elide: Text.ElideRight
                       color: Qt.rgba(0, 0, 0, 204 / 255) }
                Text { id: over; font.family: Theme.pixel; font.pixelSize: 16
                       width: Math.min(implicitWidth, parent.room); elide: Text.ElideRight }
            }
            Shadowed { id: stemText; text: nameBox.stem; color: nameBox.ink; room: nameBox.width }
            Shadowed {
                x: stemText.inkWidth
                visible: nameBox.tail.length > 0 && room > 16 * 0.5
                text: nameBox.tail; color: Theme.accent; room: nameBox.width - stemText.inkWidth
            }
        }
        component Cell: Text {
            property real x0
            property real w
            property bool alignRight: false
            property real ems: 0.86
            property bool display: false
            x: Theme.roundEven(alignRight ? x0 + w - Math.min(implicitWidth, w) : x0)
            width: Math.min(implicitWidth, Theme.roundEven(w))
            visible: text.length > 0 && w > 2
            anchors.verticalCenter: parent.verticalCenter
            font.family: display ? Theme.pixel : Theme.mono
            font.pointSize: Theme.pt(row.rowEm * ems)
            elide: Text.ElideRight
            maximumLineCount: 1
        }
        Cell { x0: row.mapX; w: row.mapWidth; ems: 1.02; display: true
               text: row.modelData.map || ""; color: row.answered ? "#cfd6e4" : Theme.textDim }
        Cell { x0: row.modeX; w: row.modeWidth; ems: 0.82; visible: !row.narrow && text.length > 0
               text: row.modelData.mode || ""; color: Theme.textDim }
        Cell { x0: row.playersX; w: row.numberWidth; alignRight: true; visible: !row.narrow && text.length > 0
               text: row.modelData.players || ""; color: Theme.text }
        Cell { x0: row.pingX; w: row.numberWidth; alignRight: true
               text: row.modelData.ping || ""; color: row.modelData.pingColour || Theme.textDim }
    }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: row.live ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: if (row.live) { Theme.keyboardDriving = false; row.forceActiveFocus(); row.clicked() }
        onDoubleClicked: if (row.live) { row.clicked(); row.activated() }
    }
    Keys.onReturnPressed: if (live) { clicked(); activated() }
    Keys.onEnterPressed: if (live) { clicked(); activated() }
    Keys.onSpacePressed: if (live) { clicked(); activated() }
}
