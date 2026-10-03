import QtQuick
import FruityPrime.Launcher

// PlayScreen: the four ways to choose something to play -- Online, Offline,
// Story and Clips -- and the map ballot opened from the pause menu.
Page {
    id: play
    property var nav
    // 0 Online, 1 Offline, 2 Story, 3 Clips, 4 Vote.
    property int face: 0
    // Why the lobby this page opened has closed, told on the note line.
    property string endedReason
    readonly property int current: face === 4 ? 4 : tabs.index
    // UiLayout.ShortBox: shorter than this the options sit beside the list.
    readonly property bool compact: height < 560

    widthEms: 44
    heading: face === 4 ? "vote" : "play"
    strip: face === 4 ? null : tabs

    PlayModel {
        id: playModel
        face: play.current
    }

    Tabs {
        id: tabs
        names: ["Online", "Offline", "Story", "Clips"]
        index: play.face === 4 ? 0 : play.face
        visible: play.face !== 4
    }

    // --------------------------------------------------------- the body
    body: Item {
        id: bodyGrid
        readonly property real rowGap: Theme.roundEven(Theme.em * 0.6)
        // BodyGrid: a third of an em clear of the card's right edge.
        readonly property real inner: width - Theme.roundEven(Theme.em * 0.3)
        implicitHeight: play.current === 0 ? onlineBar.height + rowGap + servers.contentHeight
                      : play.current === 1 || play.current === 4 ? rowGap + tiles.contentHeight
                      : Math.max(list.implicitHeight, options.implicitHeight)

        // Online: the name, the address and Refresh over the server list.
        Item {
            id: onlineBar
            visible: play.current === 0
            width: bodyGrid.inner
            height: nameField.height
            readonly property real gap: Theme.roundEven(Theme.em * 0.45)
            DeckField {
                id: nameField
                widthEms: 7
                width: implicitWidth; height: implicitHeight
                text: playModel.playerName
                KeyNavigation.right: addressField
            }
            DeckButton {
                id: refresh
                anchors.right: parent.right
                text: "Refresh"; face: Theme.slate
                em: Theme.em; sizeEms: 0.95; padXEms: 0.8; padYEms: 0.4; lip: 3
                width: implicitWidth; height: implicitHeight
                onClicked: playModel.reloadServers()
            }
            DeckField {
                id: addressField
                x: nameField.width + onlineBar.gap
                width: refresh.x - onlineBar.gap - x
                height: implicitHeight
                widthEms: 0
                text: playModel.serverEndpoint
                placeholder: "host:port — or pick a row"
                onEditingFinished: playModel.queryStatus(text)
            }
        }
        UiList {
            id: servers
            visible: play.current === 0
            y: onlineBar.height + bodyGrid.rowGap
            width: bodyGrid.inner
            height: bodyGrid.height - y
            spacingEms: 0.32
            autoSelectFirst: false
            model: play.current === 0 ? playModel.servers : []
            delegate: ServerRow { }
            onChosen: index => play.serverChosen(index)
            onActivated: index => play.go()
        }

        // Offline and the ballot: the map cards.
        DeckGrid {
            id: tiles
            visible: play.current === 1 || play.current === 4
            y: bodyGrid.rowGap
            width: bodyGrid.inner
            height: bodyGrid.height - y
            model: play.current === 1 || play.current === 4 ? shell.rooms : []
            delegate: DeckTile {
                property var modelData: ({})
                property int index
                roomKey: modelData.key || ""
                code: modelData.code || ""
                blurb: modelData.name || ""
                verb: play.current === 4 ? "Pick" : "Select"
                chosenVerb: play.current === 4 ? "Picked" : "Selected"
                chosen: play.picked === roomKey && (play.current !== 4 || play.voted)
                tally: play.current === 4 ? (play.tallies[roomKey] || 0) : -1
                leader: play.current === 4 && play.best > 0 && (play.tallies[roomKey] || 0) === play.best
                onClicked: play.current === 4 ? play.castVote(roomKey) : play.openTile(modelData)
            }
        }

        // Story and Clips: the list, and the options beside or above it.
        UiList {
            id: list
            visible: play.current === 2 || play.current === 3
            x: 0
            y: play.compact ? 0 : options.height + bodyGrid.rowGap
            width: play.compact ? bodyGrid.inner - 18 - options.width : bodyGrid.inner
            height: play.compact ? bodyGrid.height : bodyGrid.height - y
            model: play.current === 2 ? playModel.saveSlots : play.current === 3 ? playModel.clips : []
            onActivated: index => play.go()
        }
        Column {
            id: options
            visible: play.current === 2 || play.current === 3
            x: bodyGrid.inner - width
            width: 300
            spacing: 2
            ChoiceRow {
                id: storyHunter
                visible: play.current === 2
                width: parent.width
                label: "Hunter"
                options: playModel.hunters
                index: playModel.lastHunter
                KeyNavigation.down: storyStart
            }
            ChoiceRow {
                id: storyStart
                visible: play.current === 2
                width: parent.width
                label: "Start"
                // RefreshStory: a slot in use can be continued.
                options: list.selectedIndex >= 0 && playModel.saveSlots.length > list.selectedIndex
                         && playModel.saveSlots[list.selectedIndex].used ? ["Continue", "New game"] : ["New game"]
                KeyNavigation.up: storyHunter
            }
        }
    }

    // ------------------------------------------------------ the side panel
    property string picked: playModel.chosenRoom
    property bool voted: false
    property var tallies: ({})
    readonly property int best: {
        let top = 0
        for (const key in tallies)
            top = Math.max(top, tallies[key])
        return top
    }
    property string sideCode
    property string sideName
    property string sideAddress
    property var sideFacts: []
    property bool sideOnline: false

    DeckSide {
        id: side
        z: 10
        Column {
            id: sideColumn
            width: parent.width
            spacing: 8
            Column {
                width: parent.width
                spacing: 3
                Item {
                    width: parent.width
                    height: Math.max(sideChip.height, sideTitle.implicitHeight, closeButton.height)
                    DeckChip {
                        id: sideChip
                        value: play.sideCode
                        width: implicitWidth; height: implicitHeight
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        id: sideTitle
                        x: sideChip.width + 8
                        width: closeButton.x - 8 - x
                        anchors.verticalCenter: parent.verticalCenter
                        text: play.sideName
                        font.family: Theme.pixel; font.pixelSize: 17
                        color: Theme.text
                        elide: Text.ElideRight
                    }
                    DeckButton {
                        id: closeButton
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        text: "x"; face: Theme.slate; em: Theme.em
                        sizeEms: 1; padXEms: 0.6; padYEms: 0.3; lip: 2
                        width: implicitWidth; height: implicitHeight
                        onClicked: side.open = false
                    }
                }
                Text {
                    width: parent.width
                    text: play.sideAddress
                    font.family: Theme.pixel; font.pixelSize: 11
                    color: Theme.textDim
                    wrapMode: Text.Wrap
                }
            }
            Flickable {
                width: parent.width
                height: Math.min(contentHeight, Math.max(60, side.height - Theme.em * 2 - 120))
                contentHeight: sideBody.height
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                Column {
                    id: sideBody
                    width: parent.width
                    spacing: 8
                    HunterStand {
                        width: parent.width
                        height: 150
                        hunter: sideHunter.index
                        suit: sideSuit.index
                    }
                    Column {
                        width: parent.width
                        spacing: 4
                        Repeater {
                            model: play.sideFacts
                            Item {
                                required property var modelData
                                width: parent.width
                                height: 26
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: modelData.key.toUpperCase()
                                    font.family: Theme.pixel; font.pixelSize: 10
                                    color: Theme.textDim
                                }
                                Text {
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: modelData.value
                                    font.family: Theme.pixel; font.pixelSize: 12
                                    color: modelData.tint || Theme.text
                                    elide: Text.ElideRight
                                }
                            }
                        }
                        ChoiceRow {
                            id: sideMode
                            visible: !play.sideOnline
                            width: parent.width
                            label: "Match type"
                            options: playModel.modes
                        }
                        ChoiceRow {
                            id: sideHunter
                            width: parent.width
                            label: "Hunter"
                            options: playModel.hunters
                            index: playModel.lastHunter
                        }
                        ChoiceRow {
                            id: sideSuit
                            width: parent.width
                            label: "Suit"
                            options: ["1", "2", "3", "4"]
                            index: playModel.lastColour
                            preview: Component { Rectangle { radius: 3; color: playModel.suitColour(sideHunter.index, sideSuit.index) } }
                        }
                        ChoiceRow {
                            id: sideBots
                            visible: !play.sideOnline
                            width: parent.width
                            label: "Bots"
                            options: playModel.bots
                            index: playModel.lastBots
                        }
                        ChoiceRow {
                            id: sideSkill
                            visible: !play.sideOnline
                            width: parent.width
                            label: "Bot skill"
                            options: ["Easy", "Normal", "Hard", "Insane"]
                            index: playModel.lastSkill
                        }
                    }
                }
            }
            Item {
                width: parent.width
                height: sideGo.height
                DeckButton {
                    id: sideBack
                    text: "Back"; face: Theme.brass; em: Theme.em
                    sizeEms: 1.1; padXEms: 0.9; padYEms: 0.5; lip: 5
                    width: implicitWidth; height: implicitHeight
                    anchors.verticalCenter: parent.verticalCenter
                    onClicked: side.open = false
                }
                DeckButton {
                    id: sideGo
                    x: sideBack.width + 6
                    width: parent.width - x; height: implicitHeight
                    text: play.sideOnline ? "JOIN" : "START"; face: Theme.moss; em: Theme.em
                    sizeEms: 1.4; padXEms: 1; padYEms: 0.45; lip: 6
                    onClicked: play.go()
                }
            }
        }
    }

    // ------------------------------------------------------------ marks
    no: Mark { id: backMark; shape: "cancel"; label: "back"; onClicked: play.leave() }
    extra: Mark {
        id: lobbyMark
        shape: "add"; label: "create lobby"
        visible: play.current === 0
        onClicked: if (play.nav) play.nav.openCreateServer()
    }
    yes: Mark {
        id: goMark
        shape: "accept"
        label: playModel.goLabel
        enabled: !playModel.busy && (play.current !== 0 || servers.selectedIndex >= 0)
        onClicked: play.go()
    }
    note: Note { text: playModel.note; color: playModel.noteColour }

    // --------------------------------------------------------- behaviour
    function leave() {
        playModel.stopPolling()
        if (nav)
            nav.pop()
    }

    function openTile(room) {
        picked = room.key
        sideOnline = false
        sideCode = room.code
        sideName = shell.roomName(room.key)
        sideAddress = "offline — bots on this machine"
        sideFacts = []
        side.open = true
        playModel.say("", Theme.textDim)
    }

    function serverChosen(index) {
        const row = playModel.servers[index]
        if (!row || row.asking || !row.answered)
            return
        addressField.text = row.endpoint
        sideOnline = true
        sideCode = row.mode && row.mode.length > 0 ? row.mode : "server"
        sideName = row.name
        sideAddress = row.endpoint
        sideFacts = [{ key: "Map", value: row.map }, { key: "Mode", value: row.mode },
                     { key: "Players", value: row.players },
                     { key: "Ping", value: row.ping + " ms", tint: row.pingColour }]
        side.open = true
    }

    // PlayScreen.CastVote: one vote, moved or taken back.
    function castVote(key) {
        const next = Object.assign({}, tallies)
        const taking = !(voted && picked === key)
        if (voted && picked.length > 0)
            next[picked] = Math.max(0, (next[picked] || 0) - 1)
        if (taking) {
            next[key] = Math.max(0, next[key] || 0) + 1
            picked = key
            voted = true
        } else {
            picked = ""
            voted = false
        }
        tallies = next
        let leader = ""
        for (const room of shell.rooms) {
            if (best > 0 && (tallies[room.key] || 0) === best) {
                leader = shell.roomName(room.key)
                break
            }
        }
        playModel.say(leader.length > 0 ? leader + " is leading with " + best + ". Most votes wins."
                                    : "Nobody has picked. The rotation decides.", Theme.textDim)
    }

    function go() {
        switch (current) {
        case 0:
            if (servers.selectedIndex >= 0)
                playModel.join(nameField.text, addressField.text, sideHunter.index, sideSuit.index)
            else if (nav)
                nav.openCreateServer()
            break
        case 1:
            playModel.start(picked, sideMode.index, sideHunter.index, sideSuit.index, sideBots.index, sideSkill.index)
            break
        case 2:
            playModel.startAdventure(list.selectedIndex >= 0 ? playModel.saveSlots[list.selectedIndex].slot : 1,
                                 storyHunter.index, storyStart.value === "New game")
            break
        case 3:
            if (list.selectedIndex >= 0)
                playModel.watch(playModel.clips[list.selectedIndex].path)
            break
        case 4:
            if (voted && picked.length > 0) {
                playModel.propose(picked)
                if (nav)
                    nav.pop()
                shell.resume()
            }
            break
        }
    }

    Connections {
        target: tabs
        function onChanged() {
            side.open = false
        }
    }
    onCurrentChanged: {
        if (current === 4) {
            picked = playModel.votingRoom
            voted = false
            tallies = ({})
        }
    }
    Component.onCompleted: {
        if (endedReason.length > 0)
            playModel.sessionEnded(endedReason)
        if (current === 0)
            backMark.forceActiveFocus()
        else
            list.focusFirst()
    }
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Escape) {
            if (side.open)
                side.open = false
            else
                leave()
            event.accepted = true
            return
        }
        const target = current === 0 ? servers : list
        if (target.visible && target.handleKey(event.key)) {
            event.accepted = true
            return
        }
        if (face !== 4 && (event.key === Qt.Key_Left || event.key === Qt.Key_Right)) {
            Theme.keyboardDriving = true
            tabs.step(event.key === Qt.Key_Left ? -1 : 1)
            event.accepted = true
        }
    }
}
