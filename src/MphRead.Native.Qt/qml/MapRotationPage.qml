import QtQuick
import FruityPrime.Launcher

// MapRotationPicker: an ordered cycle of maps, or one map alone.
Page {
    id: page
    property var nav
    property bool single: false
    property var picked: []
    property int maxRotation: 16
    property string noteText
    property color noteColour: Theme.textDim
    signal done(var picked)
    signal cancelled()

    widthEms: 44
    fill: true
    heading: single ? "choose map" : "map rotation"

    CreateServerModel { id: names; sample: true }

    function close() {
        if (page.nav)
            page.nav.pop()
        else
            page.cancelled()
    }
    function toggle(room) {
        if (single) {
            picked = [room]
        } else {
            const at = picked.indexOf(room)
            if (at < 0) {
                if (picked.length >= maxRotation) {
                    noteText = maxRotation + " maps is as long as a rotation can be sent."
                    noteColour = Theme.warm
                    return
                }
                picked = picked.concat([room])
            } else {
                const next = picked.slice()
                next.splice(at, 1)
                picked = next
            }
        }
        mark()
    }
    function mark() {
        noteColour = Theme.textDim
        if (picked.length === 0) {
            noteText = single ? "Pick a map." : "Press maps to build the cycle. They are played in the order you press them."
        } else if (single) {
            noteText = "Selected: " + names.roomName(picked[0])
        } else {
            noteText = picked.map(r => names.roomName(r)).join("  >  ")
        }
    }
    function commit() {
        if (picked.length === 0) {
            noteText = single ? "Pick a map." : "Pick at least one map."
            noteColour = Theme.warm
            return
        }
        done(picked)
        if (page.nav)
            page.nav.pop()
    }
    Component.onCompleted: { mark(); list.focusFirst() }

    body: Item {
        UiList {
            id: list
            width: parent.width
            height: parent.height - note.height
            model: shell.rooms.map(r => ({ title: names.roomName(r.key), key: r.key,
                                           detail: page.picked.indexOf(r.key) >= 0 ? "#" + (page.picked.indexOf(r.key) + 1) : "" }))
            onChosen: index => page.toggle(shell.rooms[index].key)
        }
        Note {
            id: note
            y: parent.height - height
            width: parent.width
            text: page.noteText
            color: page.noteColour
        }
    }

    no: Mark { shape: "cancel"; label: "back"; onClicked: page.close() }
    yes: Mark { shape: "accept"; label: page.single ? "use this map" : "use these maps"; onClicked: page.commit() }

    Keys.onPressed: event => {
        if (event.key === Qt.Key_Escape) {
            page.close()
            event.accepted = true
        } else if (list.handleKey(event.key)) {
            event.accepted = true
        }
    }
}
