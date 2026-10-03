import QtQuick
import FruityPrime.Launcher

// EndPanelView: the results' side panel -- the next map's ballot, or the
// hunter for the next match -- and READY at its foot.
FocusScope {
    id: panel
    property bool hunterTab: false
    property var state: shell.endState()
    // The Avalonia panel is refreshed every frame the results are up.
    Timer { interval: 100; repeat: true; running: true; onTriggered: panel.state = shell.endState() }

    Item {
        id: host
        width: Theme.phone ? 285 : 340
        anchors.right: parent.right; anchors.rightMargin: 14
        anchors.top: parent.top; anchors.topMargin: 14
        anchors.bottom: parent.bottom; anchors.bottomMargin: 14

        DeckCard {
            id: card
            em: Theme.em
            maxWidthEms: 22
            width: Math.min(host.width, Theme.em * 22)
            height: host.height
            anchors.horizontalCenter: parent.horizontalCenter

            Item {
                width: parent.width
                height: card.height - card.pad * 2
                Tabs {
                    id: tabs
                    anchors.horizontalCenter: parent.horizontalCenter
                    names: ["Vote map", "Change hunter"]
                    index: panel.hunterTab ? 1 : 0
                }
                Item {
                    id: body
                    y: tabs.height + 8
                    width: parent.width
                    height: foot.y - 8 - y
                    DeckGrid {
                        anchors.fill: parent
                        visible: tabs.index === 0
                        fixedColumns: 2
                        ratio: 16 / 9
                        model: panel.state.ballot
                        delegate: DeckTile {
                            property var modelData: ({})
                            property int index
                            roomKey: modelData.key || ""
                            code: modelData.code || ""
                            blurb: modelData.name || ""
                            verb: ""; chosenVerb: ""
                            tally: modelData.votes || 0
                            leader: !!modelData.leader
                            chosen: !!modelData.chosen
                            onClicked: { shell.endChoose(roomKey); panel.state = shell.endState() }
                        }
                    }
                    Note {
                        visible: tabs.index === 0 && panel.state.ballot.length === 0
                        width: parent.width
                        anchors.verticalCenter: parent.verticalCenter
                        horizontalAlignment: Text.AlignHCenter
                        text: "The rotation decides where next."
                    }
                    Column {
                        visible: tabs.index === 1
                        width: parent.width
                        spacing: 8
                        HunterStand {
                            visible: tabs.index === 1
                            width: parent.width
                            height: 150
                            hunter: hunterRow.index
                            suit: suitRow.index
                        }
                        ChoiceRow {
                            id: hunterRow
                            width: parent.width
                            label: "Hunter"
                            options: ["Samus", "Kanden", "Trace", "Sylux", "Noxus", "Spire", "Weavel"]
                            index: panel.state.hunter
                            onChanged: shell.endPick(hunterRow.index, suitRow.index)
                        }
                        ChoiceRow {
                            id: suitRow
                            width: parent.width
                            label: "Suit"
                            options: ["1", "2", "3", "4"]
                            index: panel.state.suit
                            preview: Component { Rectangle { radius: 3; color: shell.suitColour(hunterRow.index, suitRow.index) } }
                            onChanged: shell.endPick(hunterRow.index, suitRow.index)
                        }
                    }
                }
                Item {
                    id: foot
                    width: parent.width
                    height: ready.height
                    y: parent.height - height
                    Note {
                        width: ready.x - 8
                        anchors.verticalCenter: parent.verticalCenter
                        text: panel.state.count
                    }
                    DeckButton {
                        id: ready
                        anchors.right: parent.right
                        text: "READY"
                        face: panel.state.ready ? Theme.moss : Theme.slate
                        em: Theme.em; sizeEms: 1.25; padXEms: 1.3; padYEms: 0.45; lip: 5
                        width: implicitWidth; height: implicitHeight
                        onClicked: { shell.endToggleReady(); panel.state = shell.endState() }
                    }
                }
            }
        }
    }
}
