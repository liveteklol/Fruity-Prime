import QtQuick

// PadRow: a pad action and its two slots. A click on either half (or Enter
// on the chosen one) listens for a button; a clash asks how to settle it.
FocusScope {
    id: row
    property string label
    property var pad: ({})
    // The model's words while it is listening or has something to say.
    property string live
    property int chosenSlot: 0
    property real labelWidth: 160
    readonly property bool listening: !!pad.listening
    readonly property string conflict: pad.conflict || ""
    signal pickSlot(int slot)
    signal listen()
    signal key(int key)
    signal choose(int choice)
    signal abandoned()

    activeFocusOnTab: true
    implicitWidth: 300
    implicitHeight: conflict.length > 0 ? 108 : listening ? 72 : 32
    height: implicitHeight
    onActiveFocusChanged: if (!activeFocus && listening) abandoned()

    readonly property string idle: (chosenSlot === 0 && activeFocus ? "> " : "") + "Primary: " + (pad.primary || "")
                                   + "    " + (chosenSlot === 1 && activeFocus ? "> " : "") + "Secondary: " + (pad.secondary || "")

    component Tracked: Text {
        property real size: 12
        font.family: Theme.pixel; font.weight: Font.DemiBold
        font.pointSize: Theme.pt(size)
        font.letterSpacing: size * 0.04
    }

    Tracked {
        x: 4
        y: Math.round((32 - height) / 2)
        text: row.label
        size: 12
        color: Theme.text
    }
    Rectangle {
        id: box
        x: row.labelWidth; y: 2
        width: Math.max(60, row.width - row.labelWidth - 4)
        height: 28
        radius: 4
        color: Theme.panelLight
        border.width: 1
        border.color: row.listening ? Theme.warm : (row.activeFocus || area.containsMouse) ? Theme.accent : Theme.edge
        Tracked {
            anchors.centerIn: parent
            width: Math.min(implicitWidth, parent.width - 12)
            elide: Text.ElideRight
            text: row.live.length > 0 ? row.live : row.idle
            size: 12
            color: row.listening ? Theme.warm : Theme.text
        }
    }
    Tracked {
        visible: row.listening && row.conflict.length === 0
        x: 4; y: 40
        width: row.width - 8
        wrapMode: Text.Wrap
        text: row.pad.hint || ""
        size: 11
        color: Theme.textDim
    }
    Tracked {
        visible: row.conflict.length > 0
        x: 4; y: 36
        width: row.width - 8
        elide: Text.ElideRight
        text: row.conflict
        size: 11
        color: Theme.text
    }
    Repeater {
        model: row.conflict.length > 0 ? (row.pad.resolutions || []) : []
        Tracked {
            required property int index
            required property string modelData
            x: index * row.width / 4 + 4; y: 74
            text: (row.pad.choice === index ? "> " : "") + modelData
            size: 12
            color: row.pad.choice === index ? Theme.accent : Theme.text
        }
    }
    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onPressed: mouse => {
            Theme.keyboardDriving = false
            row.forceActiveFocus()
            if (row.conflict.length > 0 && mouse.y > 60)
                row.choose(Math.max(0, Math.min(3, Math.floor(mouse.x / Math.max(1, row.width / 4)))))
        }
        onReleased: mouse => {
            if (row.listening || mouse.x < box.x || mouse.y > box.y + box.height)
                return
            row.pickSlot(mouse.x < box.x + box.width / 2 ? 0 : 1)
            row.listen()
        }
    }
    Keys.onPressed: event => {
        if (listening) {
            key(event.key)
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_Left || event.key === Qt.Key_Right) {
            pickSlot(event.key === Qt.Key_Left ? 0 : 1)
            event.accepted = true
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
            listen()
            event.accepted = true
        }
    }
}
