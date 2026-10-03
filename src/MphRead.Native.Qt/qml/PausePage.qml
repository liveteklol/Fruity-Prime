import QtQuick

// PauseMenuView: a short card over the match with the deck buttons stacked
// and centred, Resume first and Quit last; the vote answers, spectating
// and recording appear when they apply.
FocusScope {
    id: page
    signal settings()
    signal voteMap()
    readonly property real em: Theme.em

    // Polled as the Avalonia view's vote timer does, every 0.2 s.
    property var state: shell.pauseState()
    Timer { interval: 200; repeat: true; running: true; onTriggered: page.state = shell.pauseState() }

    // UiLayout.Backdrop(overGame): the scrim; then the sheet's own.
    Rectangle { anchors.fill: parent; color: Theme.scrim }
    Rectangle { anchors.fill: parent; color: Theme.sheet }

    // SheetPad: 0.9 em at the sides, 1.1 above and below.
    Item {
        anchors.fill: parent
        anchors.leftMargin: Theme.roundEven(page.em * 0.9); anchors.rightMargin: anchors.leftMargin
        anchors.topMargin: Theme.roundEven(page.em * 1.1); anchors.bottomMargin: anchors.topMargin

        DeckCard {
            id: card
            em: page.em
            maxWidthEms: 19   // UiLayout.WellShort
            anchors.centerIn: parent
            width: implicitWidth
            height: implicitHeight
            // FitToHost: never taller than the host, down to half size.
            readonly property int shown: {
                let n = 0
                for (const child of menu.children)
                    if (child.visible) n++
                return n
            }
            scale: Math.max(0.5, Math.min(1, parent.height / (shown * 26 + (shown - 1) * 6 + 44 + 84 + 70)))

            Column {
                id: menu
                width: parent.width
                spacing: 6
                component Entry: DeckButton {
                    anchors.horizontalCenter: parent.horizontalCenter
                    em: page.em; sizeEms: 1.2; padXEms: 0.8; padYEms: 0.5; lip: 4
                    face: Theme.slate
                    KeyNavigation.priority: KeyNavigation.BeforeItem
                }
                Entry { id: resume; text: "Resume"; face: Theme.moss; focus: true; onClicked: shell.resume() }
                Entry { text: "Accept map vote"; face: Theme.moss; visible: page.state.vote; onClicked: shell.answerVote(true) }
                Entry { text: "Deny map vote"; face: Theme.rust; visible: page.state.vote; onClicked: shell.answerVote(false) }
                Entry { text: "Vote map"; visible: page.state.net; onClicked: page.voteMap() }
                Entry { text: "Rejoin match"; visible: page.state.spectating; onClicked: shell.rejoin() }
                Entry { text: "Spectate"; visible: page.state.canSpectate; onClicked: shell.spectate() }
                // A phone has no window to change.
                Entry { text: shell.windowLabel; visible: !Theme.phone; onClicked: shell.toggleFullscreen() }
                Entry { text: page.state.recording ? "Stop recording" : "Record demo"; visible: page.state.net
                        onClicked: shell.toggleRecording() }
                Entry { text: "Settings"; onClicked: page.settings() }
                Entry { text: "Leave match"; face: Theme.brass; onClicked: shell.leaveMatch() }
                Entry { text: "Quit"; face: Theme.rust; onClicked: shell.quitFromMatch() }
            }
        }
    }
    // Up and Down walk the visible entries.
    function walk(step) {
        const items = []
        for (const child of menu.children)
            if (child.visible) items.push(child)
        let at = items.findIndex(item => item.activeFocus)
        at = at < 0 ? 0 : Math.max(0, Math.min(items.length - 1, at + step))
        items[at].forceActiveFocus()
    }
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Up || event.key === Qt.Key_Down) {
            Theme.keyboardDriving = true
            walk(event.key === Qt.Key_Up ? -1 : 1)
            event.accepted = true
        } else if (event.key === Qt.Key_Tab) {
            Theme.keyboardDriving = true
        }
    }
    Keys.onEscapePressed: shell.resume()
    Component.onCompleted: resume.forceActiveFocus()
}
