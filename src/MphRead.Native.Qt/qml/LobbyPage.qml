import QtQuick
import FruityPrime.Launcher

// LobbyScreen: the players and your own choices on the left, the owner's
// match on the right, the chat under both, then Leave, Ready and Start.
FocusScope {
    id: page
    property var nav
    // "main", "map" or "teams".
    property string showing: "main"

    LobbyModel {
        id: lobby
        onClosed: reason => { if (page.nav) page.nav.lobbyClosed(reason) }
    }

    Item {
        anchors.fill: parent
        visible: page.showing === "main"
        Backdrop { anchors.fill: parent }
        // UiLayout.Wash: the standard ground behind a screen's own words.
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0; color: Qt.rgba(10 / 255, 12 / 255, 16 / 255, 120 / 255) }
                GradientStop { position: 0.16; color: Qt.rgba(10 / 255, 12 / 255, 16 / 255, 228 / 255) }
                GradientStop { position: 0.88; color: Qt.rgba(10 / 255, 12 / 255, 16 / 255, 228 / 255) }
                GradientStop { position: 1; color: Qt.rgba(10 / 255, 12 / 255, 16 / 255, 120 / 255) }
            }
        }

        Item {
            id: frame
            width: Math.min(parent.width - 40, 1100)
            height: parent.height - 40
            x: Theme.roundEven((parent.width - width) / 2)
            y: 20

            Text {
                id: heading
                anchors.horizontalCenter: parent.horizontalCenter
                text: lobby.title
                font.family: Theme.mono; font.pixelSize: 15
                color: Theme.textDim
            }

            // Footer, status and chat from the bottom up; the columns take the rest.
            Row {
                id: footer
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                spacing: 16
                Mark { shape: "cancel"; label: "Leave"; onClicked: lobby.leave("") }
                Mark {
                    shape: "accept"; label: lobby.readyLabel
                    enabled: lobby.playerEnabled
                    onClicked: lobby.toggleReady()
                }
                Mark {
                    shape: "accept"; label: "Start match"
                    visible: lobby.owner
                    enabled: lobby.startEnabled
                    onClicked: lobby.startMatch()
                }
            }
            Note {
                id: status
                width: parent.width
                anchors.bottom: footer.top
                anchors.bottomMargin: 3
                text: lobby.status
            }
            Column {
                id: chatPanel
                width: parent.width
                anchors.bottom: status.top
                anchors.bottomMargin: 5
                spacing: 3
                Text {
                    text: "CHAT"
                    bottomPadding: 2
                    font.family: Theme.pixel; font.pixelSize: 11
                    color: Theme.textDim
                }
                Flickable {
                    id: chatScroll
                    width: parent.width
                    height: 58
                    clip: true
                    contentHeight: chatText.height
                    boundsBehavior: Flickable.StopAtBounds
                    Note {
                        id: chatText
                        width: chatScroll.width - 16
                        lines: 0
                        text: lobby.chat
                        onHeightChanged: chatScroll.contentY = Math.max(0, height - chatScroll.height)
                    }
                }
                Item {
                    width: parent.width
                    height: 32
                    DeckField {
                        id: chatEntry
                        width: parent.width - send.width - 8
                        height: 30
                        widthEms: 0
                        placeholder: "Message"
                        onAccepted: { lobby.sendChat(text); text = "" }
                    }
                    DeckButton {
                        id: send
                        x: parent.width - width
                        text: "Send"; face: Theme.blue
                        em: Theme.em; sizeEms: 0.82; padXEms: 0.8; padYEms: 0.34; lip: 4
                        width: implicitWidth; height: implicitHeight
                        onClicked: { lobby.sendChat(chatEntry.text); chatEntry.text = "" }
                    }
                }
            }

            Flickable {
                id: columns
                y: heading.height + 8
                width: parent.width
                height: chatPanel.y - 3 - y
                contentHeight: Math.max(leftColumn.height, rightColumn.height)
                boundsBehavior: Flickable.StopAtBounds
                clip: true
                readonly property real leftWidth: (width - 12) * 0.82 / 2

                Column {
                    id: leftColumn
                    width: columns.leftWidth - 18
                    spacing: 2
                    Caption { width: parent.width; text: "Players" }
                    Repeater {
                        model: lobby.players
                        Item {
                            id: playerRow
                            required property var modelData
                            width: leftColumn.width
                            height: 24
                            Text {
                                id: readyText
                                x: 4
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.state
                                font.family: Theme.pixel; font.pixelSize: 11
                                color: modelData.ready ? Theme.good : Theme.textDim
                            }
                            Text {
                                id: facts
                                x: parent.width - 4 - width
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.detail
                                font.family: Theme.mono; font.pixelSize: 10
                                color: Theme.textDim
                            }
                            Text {
                                x: readyText.x + readyText.width + 10
                                width: Math.max(0, facts.x - 10 - x)
                                anchors.verticalCenter: parent.verticalCenter
                                elide: Text.ElideRight
                                text: modelData.name
                                font.family: Theme.pixel; font.pixelSize: 13
                                color: Theme.text
                            }
                        }
                    }
                    Caption { width: parent.width; text: "Your player" }
                    ChoiceRow {
                        width: parent.width; label: "Hunter"; options: lobby.hunters
                        enabled: lobby.playerEnabled
                        Binding on index { value: lobby.hunter }
                        onChanged: lobby.setHunter(index)
                    }
                    ChoiceRow {
                        width: parent.width; label: "Suit"; options: ["1", "2", "3", "4"]
                        enabled: lobby.playerEnabled
                        Binding on index { value: lobby.suit }
                        onChanged: lobby.setSuit(index)
                    }
                    ChoiceRow {
                        width: parent.width; label: "Team"; options: lobby.teams
                        visible: lobby.chooseTeams
                        enabled: lobby.teamEnabled
                        Binding on index { value: lobby.team }
                        onChanged: lobby.setTeam(index)
                    }
                    Column {
                        visible: lobby.owner
                        width: parent.width
                        spacing: 2
                        Caption { width: parent.width; text: "Owner actions" }
                        ChoiceRow {
                            width: parent.width; label: "Manage player"; options: lobby.targets
                            Binding on index { value: lobby.target }
                            onChanged: lobby.target = index
                        }
                        ChoiceRow {
                            width: parent.width; label: "Move to team"; options: lobby.teams
                            visible: lobby.chooseTeams
                            Binding on index { value: lobby.moveTeam }
                            onChanged: lobby.moveTeam = index
                        }
                        Row {
                            anchors.horizontalCenter: parent.horizontalCenter
                            spacing: 7
                            component Small: DeckButton {
                                em: Theme.em; sizeEms: 0.82; padXEms: 0.8; padYEms: 0.34; lip: 4
                                width: implicitWidth; height: implicitHeight
                            }
                            Small { text: "Move"; face: Theme.blue; visible: lobby.chooseTeams; onClicked: lobby.admin(0) }
                            Small { text: "Transfer"; face: Theme.brass; onClicked: lobby.admin(1) }
                            Small { text: "Kick"; face: Theme.rust; onClicked: lobby.admin(2) }
                        }
                    }
                }

                Column {
                    id: rightColumn
                    x: columns.leftWidth + 12
                    width: columns.width - x
                    spacing: 2
                    enabled: lobby.ownerEnabled
                    Image {
                        id: preview
                        width: parent.width
                        height: 104
                        fillMode: Image.PreserveAspectCrop
                        clip: true
                        source: lobby.room.length > 0 ? lobby.mapShot() : ""
                        visible: status === Image.Ready
                    }
                    PickRow {
                        width: parent.width; label: "Map"; value: lobby.mapName
                        onClicked: if (lobby.canEdit()) page.showing = "map"
                    }
                    ChoiceRow {
                        width: parent.width; label: "Game type"; options: lobby.modes
                        Binding on index { value: lobby.mode }
                        onChanged: lobby.setMode(index)
                    }
                    ChoiceRow {
                        width: parent.width; label: "Matchup"; options: lobby.matchups
                        Binding on index { value: lobby.matchup }
                        onChanged: lobby.setMatchup(index)
                    }
                    PickRow {
                        width: parent.width; label: "Custom teams"; value: lobby.customTeams
                        visible: lobby.customTeamsShown
                        onClicked: if (lobby.canEdit()) page.showing = "teams"
                    }
                    Row {
                        width: parent.width
                        spacing: 12
                        FieldRow {
                            width: (parent.width - 12) / 2; label: "Time limit (minutes)"; boxWidth: 80
                            text: lobby.time
                            onEdited: t => lobby.setTime(t)
                        }
                        FieldRow {
                            width: (parent.width - 12) / 2; label: lobby.goalLabel; boxWidth: 80
                            text: lobby.goal
                            onEdited: t => lobby.setGoal(t)
                        }
                    }
                    Grid {
                        width: parent.width
                        columns: 2
                        columnSpacing: 12
                        rowSpacing: 2
                        Repeater {
                            model: lobby.toggles
                            ButtonToggleRow {
                                required property var modelData
                                required property int index
                                width: (rightColumn.width - 12) / 2
                                visible: modelData.shown
                                label: modelData.label
                                on: modelData.on
                                onToggled: v => lobby.setToggle(index, v)
                            }
                        }
                    }
                    Note { width: parent.width; text: lobby.summary; visible: lobby.summaryShown }
                }
            }
            ThinScroll { flick: columns }
        }
    }

    Loader {
        anchors.fill: parent
        active: page.showing === "map"
        sourceComponent: MapCardPage {
            selected: lobby.room
            onDone: room => { lobby.setRoom(room); page.showing = "main" }
            onCancelled: page.showing = "main"
        }
        onLoaded: item.forceActiveFocus()
    }
    Loader {
        anchors.fill: parent
        active: page.showing === "teams"
        sourceComponent: CustomTeamsPage {
            layout: lobby.customLayout
            maxPlayers: lobby.maxPlayers
            onDone: (count, sizes) => { lobby.setCustomTeams(count, sizes); page.showing = "main" }
            onCancelled: page.showing = "main"
        }
        onLoaded: item.forceActiveFocus()
    }
    onShowingChanged: if (showing === "main") page.forceActiveFocus()
    Keys.onEscapePressed: lobby.leave("")
}
