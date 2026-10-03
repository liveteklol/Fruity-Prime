import QtQuick
import FruityPrime.Launcher

// HostPicker: the machines that could be asked to run a lobby.
Page {
    id: page
    property var nav
    // The capture's fleet, with a model of its own.
    property bool sample: false
    property var model: own
    signal done()
    signal cancelled()

    widthEms: 44
    heading: "host on"

    CreateServerModel { id: own; sample: page.sample }

    function close() {
        if (page.nav && page.model === own)
            page.nav.pop()
        else
            page.cancelled()
    }

    body: Item {
        implicitHeight: list.contentHeight + note.height
        UiList {
            id: list
            width: parent.width
            height: parent.height - note.height
            autoSelectFirst: true
            model: page.model.hosts
            delegate: Component {
                Item {
                    id: entry
                    property var modelData: ({})
                    property bool selected: false
                    signal clicked()
                    signal activated()
                    height: modelData.note !== undefined ? noteText.height + 8 : 30
                    activeFocusOnTab: !!modelData.usable
                    ListRow {
                        id: listRow
                        visible: entry.modelData.note === undefined
                        width: entry.width
                        title: entry.modelData.title || ""
                        detail: entry.modelData.detail || ""
                        selected: entry.selected && !!entry.modelData.usable
                        enabled: !!entry.modelData.usable
                        focus: true
                        onClicked: entry.clicked()
                        onActivated: { entry.activated(); page.model.chooseHost(entry.modelData.index); page.done() }
                    }
                    Note {
                        id: noteText
                        visible: entry.modelData.note !== undefined
                        y: 4; width: entry.width
                        text: entry.modelData.note || ""
                        color: entry.modelData.colour || Theme.textDim
                    }
                }
            }
            onChosen: index => {
                const row = page.model.hosts[index]
                if (row && row.usable) {
                    page.model.chooseHost(row.index)
                    page.done()
                }
            }
        }
        Note {
            id: note
            y: parent.height - height
            width: parent.width
            text: page.model.hostsNote
            color: page.model.hostsNoteColour
        }
    }

    no: Mark { shape: "cancel"; label: "back"; onClicked: page.close() }
    extra: Mark { shape: "add"; label: "ask again"; onClicked: page.model.askAgain() }

    Keys.onPressed: event => {
        if (event.key === Qt.Key_Escape) {
            page.close()
            event.accepted = true
        } else if (list.handleKey(event.key)) {
            event.accepted = true
        }
    }
    Component.onCompleted: list.focusFirst()
}
