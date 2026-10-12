import FruityPrime.Launcher
import QtQuick

// Download maps: the levels on FruityPrime's map server, each with its
// picture (or a Quake 3 badge when its pack has none), fetched one pack at a
// time into the map folder, then prepared as the others are.
Page {
    id: page
    property var nav

    widthEms: 44
    fill: true
    heading: "download maps"

    DownloadsModel { id: downloads }

    body: Item {
        DeckGrid {
            id: tiles
            width: parent.width
            height: parent.height
            model: downloads.maps
            delegate: DeckTile {
                property var modelData: ({})
                property int index
                code: modelData.level || ""
                blurb: (modelData.file || "") + " · " + (modelData.size || 0).toFixed(1) + " MB"
                picture: modelData.preview || ""
                quakeBadge: (modelData.preview || "").length === 0
                verb: modelData.installed ? "Installed" : modelData.downloading ? "Downloading…" : "Download"
                chosen: !!modelData.installed
                chosenVerb: "Installed"
                onClicked: if (!modelData.installed) downloads.download(modelData.file)
            }
        }
    }

    no: Mark { shape: "cancel"; label: "back"; onClicked: if (page.nav) page.nav.pop() }
    extra: Mark {
        shape: "add"; label: "refresh"
        enabled: !downloads.loading
        onClicked: downloads.refresh()
    }
    yes: Mark {
        shape: "accept"
        label: downloads.missing > 0 ? "download all (" + downloads.missing + ")" : "all installed"
        enabled: downloads.missing > 0 && !downloads.busy
        onClicked: downloads.downloadAll()
    }
    note: Note {
        text: downloads.note.length > 0 ? downloads.note
            : downloads.maps.length + " maps on the server. Add yours at net.livetek.fr/fruityprime/maps"
        color: Theme.textDim
    }
    Keys.onEscapePressed: if (nav) nav.pop()
}
