import QtQuick
import FruityPrime.Launcher

// CreateServerScreen: a lobby on a server that offers one, or the dedicated
// server on this machine; the host and the maps each open their own page.
Item {
    id: screen
    property var nav
    property bool dedicated: false
    // "form", "hosts" or "maps".
    property string showing: "form"

    CreateServerModel {
        id: create
        kind: screen.dedicated ? 1 : 0
    }

    function leave() {
        if (create.busy)
            return
        create.leave()
        if (screen.nav)
            screen.nav.pop()
    }

    Page {
        id: form
        anchors.fill: parent
        visible: screen.showing === "form"
        focus: visible
        widthEms: 44
        heading: "create lobby"

        body: Item {
            implicitHeight: column.height
            Flickable {
                id: flick
                anchors.fill: parent
                contentHeight: column.height
                boundsBehavior: Flickable.StopAtBounds
                clip: true
                Column {
                    id: column
                    width: flick.width
                    spacing: 2
                    FieldRow {
                        id: nameRow
                        width: parent.width
                        label: "Lobby name"
                        boxWidth: 230
                        text: create.lobbyName
                        focus: true
                        KeyNavigation.down: modeRow
                    }
                    ChoiceRow { id: modeRow; width: parent.width; label: "Game type"; options: create.modes; KeyNavigation.down: hunterRow }
                    ChoiceRow {
                        id: hunterRow
                        width: parent.width
                        label: "Your hunter"
                        options: create.hunters
                        index: create.lastHunter
                        KeyNavigation.down: mapsRow
                    }
                    PickRow {
                        id: mapsRow
                        width: parent.width
                        label: "Map rotation"
                        value: create.mapsLabel
                        onClicked: if (!create.busy) screen.showing = "maps"
                        KeyNavigation.down: hostRow.visible ? hostRow : kindRow
                    }
                    PickRow {
                        id: hostRow
                        width: parent.width
                        visible: create.kind !== 1
                        label: "Host on"
                        value: create.hostLabel
                        onClicked: if (!create.busy) screen.showing = "hosts"
                        KeyNavigation.down: kindRow
                    }
                    ChoiceRow {
                        id: kindRow
                        width: parent.width
                        visible: create.canRunHere
                        label: "Hosting"
                        options: ["Hosted lobby", "Dedicated server"]
                        index: create.kind
                        onChanged: create.kind = index
                    }
                    ProgressRow {
                        width: parent.width
                        visible: create.progressVisible
                        fraction: create.fraction
                        stage: create.stage
                    }
                    Note { width: parent.width; text: create.note; color: create.noteColour }
                }
            }
            ThinScroll { flick: flick }
        }

        no: Mark { shape: "cancel"; label: "back"; enabled: !create.busy; onClicked: screen.leave() }
        yes: Mark {
            shape: "accept"
            label: create.goLabel
            enabled: create.goEnabled
            onClicked: create.go(nameRow.text, modeRow.index, hunterRow.index)
        }
        extra: Mark {
            shape: "fetch"
            label: "files required -- install"
            visible: create.fetchVisible
            onClicked: create.fetch()
        }
        Keys.onEscapePressed: screen.leave()
    }

    Loader {
        anchors.fill: parent
        active: screen.showing === "hosts"
        focus: active
        sourceComponent: HostPickerPage {
            model: create
            onDone: screen.showing = "form"
            onCancelled: screen.showing = "form"
        }
        onLoaded: item.forceActiveFocus()
    }
    Loader {
        anchors.fill: parent
        active: screen.showing === "maps"
        focus: active
        sourceComponent: MapRotationPage {
            picked: create.rotation
            maxRotation: create.maxRotation
            onDone: p => { create.rotation = p; screen.showing = "form" }
            onCancelled: screen.showing = "form"
        }
        onLoaded: item.forceActiveFocus()
    }
    onShowingChanged: if (showing === "form") nameRow.forceActiveFocus()
    // After Main hands the page the focus: the name box takes it.
    Component.onCompleted: Qt.callLater(() => nameRow.forceActiveFocus())
}
