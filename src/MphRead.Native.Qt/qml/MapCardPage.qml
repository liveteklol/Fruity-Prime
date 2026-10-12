import FruityPrime.Launcher
import QtQuick

// MapCardPicker: one map, chosen from the cards.
Page {
    id: page
    property var nav
    property string selected
    signal done(string room)
    signal cancelled()

    widthEms: 44
    fill: true
    heading: "choose map"

    body: Item {
        MapSetSwitch {
            id: mapSets
            rooms: ShellHost.rooms
            Component.onCompleted: {
                const chosen = ShellHost.rooms.find(r => r.key === page.selected)
                if (chosen && chosen.custom)
                    index = 1
            }
        }
        DeckGrid {
            id: tiles
            y: mapSets.height + 6
            width: parent.width
            height: parent.height - note.height - 12 - mapSets.height
            model: mapSets.pick(ShellHost.rooms)
            delegate: DeckTile {
                property var modelData: ({})
                property int index
                roomKey: modelData.key || ""
                code: modelData.code || ""
                blurb: modelData.name || ""
                chosen: page.selected === roomKey
                onClicked: page.selected = roomKey
            }
        }
        Note {
            id: note
            y: parent.height - height
            width: parent.width
            text: ShellHost.rooms.length === 0 ? "No multiplayer rooms were found. Set the game files up from Settings."
                : page.selected.length === 0 ? "Choose a map." : "Selected: " + ShellHost.roomName(page.selected)
            color: ShellHost.rooms.length === 0 ? Theme.warm : Theme.textDim
        }
    }

    no: Mark { shape: "cancel"; label: "back"; onClicked: page.cancelled() }
    yes: Mark {
        shape: "accept"; label: "use map"
        enabled: page.selected.length > 0
        onClicked: page.done(page.selected)
    }
    Keys.onEscapePressed: cancelled()
}
