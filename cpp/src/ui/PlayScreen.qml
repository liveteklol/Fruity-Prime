import QtQuick
import QtQuick.Controls.Basic

// Play: a match against bots on this machine, or one on a server. One list,
// and a strip over it saying what the list is.
Item {
    id: root
    property int face: 0 // 0 offline, 1 online

    function indexOfKey(list, key) {
        for (var i = 0; i < list.length; i++)
            if (list[i].key === key)
                return i;
        return 0;
    }

    Column {
        anchors.centerIn: parent
        width: Theme.wellWide
        spacing: Theme.gap

        Row {
            spacing: Theme.gap
            UiButton {
                text: "OFFLINE"
                primary: root.face === 0
                onClicked: root.face = 0
            }
            UiButton {
                text: "ONLINE"
                primary: root.face === 1
                onClicked: {
                    root.face = 1;
                    if (launcher.servers.length === 0)
                        launcher.refreshServers();
                }
            }
        }

        // ---- offline
        Column {
            width: parent.width
            spacing: 0
            visible: root.face === 0

            UiRow {
                label: "Map"
                UiChoice {
                    values: launcher.rooms
                    valueWidth: 220
                    index: root.indexOfKey(launcher.rooms, settings.get("match.room"))
                    onPicked: function (i) {
                        settings.set("match.room", launcher.rooms[i].key);
                    }
                }
            }
            UiRow {
                label: "Mode"
                UiChoice {
                    values: launcher.modes
                    index: root.indexOfKey(launcher.modes, settings.get("match.mode"))
                    onPicked: function (i) {
                        settings.set("match.mode", launcher.modes[i].key);
                    }
                }
            }
            UiRow {
                label: "Hunter"
                UiChoice {
                    values: launcher.hunters
                    index: settings.get("player.hunter")
                    onPicked: function (i) {
                        settings.set("player.hunter", i);
                    }
                }
            }
            UiRow {
                label: "Bots"
                UiChoice {
                    values: ["0", "1", "2", "3", "4", "5", "6", "7"]
                    valueWidth: 100
                    index: settings.get("match.bots")
                    onPicked: function (i) {
                        settings.set("match.bots", i);
                    }
                }
            }
            UiRow {
                label: "Bot skill"
                UiChoice {
                    values: ["Easy", "Medium", "Hard", "Insane"]
                    valueWidth: 140
                    index: settings.get("match.botLevel")
                    onPicked: function (i) {
                        settings.set("match.botLevel", i);
                    }
                }
            }
            UiRow {
                label: "Teams"
                UiChoice {
                    values: ["2", "3", "4"]
                    valueWidth: 100
                    index: Math.max(0, settings.get("match.teams") - 2)
                    onPicked: function (i) {
                        settings.set("match.teams", i + 2);
                    }
                }
            }
            UiRow {
                label: "Friendly fire"
                Switch {
                    checked: settings.get("match.friendlyFire")
                    onToggled: settings.set("match.friendlyFire", checked)
                }
            }
        }

        // ---- online
        Column {
            width: parent.width
            spacing: Theme.gap
            visible: root.face === 1

            Row {
                width: parent.width
                spacing: Theme.gap
                TextField {
                    id: address
                    width: parent.width - 320
                    placeholderText: "host or host:port"
                    text: settings.get("online.server")
                    color: Theme.text
                    background: Rectangle {
                        color: Theme.panel
                        radius: Theme.radius
                        border.width: 1
                        border.color: Theme.edge
                    }
                    onEditingFinished: settings.set("online.server", text)
                }
                UiButton {
                    text: launcher.scanning ? "SCANNING…" : "REFRESH"
                    enabledLook: !launcher.scanning
                    onClicked: launcher.refreshServers()
                }
                UiButton {
                    text: "CREATE SERVER"
                    onClicked: launcher.hostLocal()
                }
            }

            Rectangle {
                width: parent.width
                height: 260
                color: Theme.panel
                radius: Theme.radius
                border.width: 1
                border.color: Theme.edge

                ListView {
                    id: list
                    anchors.fill: parent
                    anchors.margins: 6
                    clip: true
                    model: launcher.servers
                    currentIndex: -1
                    delegate: Rectangle {
                        required property int index
                        required property var modelData
                        width: list.width
                        height: 38
                        color: list.currentIndex === index ? Theme.panelLight : "transparent"
                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.leftMargin: 8
                            spacing: 12
                            Text {
                                width: 200
                                elide: Text.ElideRight
                                text: modelData.name
                                color: modelData.ok ? Theme.text : Theme.dim
                                font.pixelSize: Theme.bodySize
                            }
                            Text {
                                width: 200
                                elide: Text.ElideRight
                                text: modelData.room
                                color: Theme.dim
                                font.pixelSize: Theme.smallSize
                            }
                            Text {
                                width: 110
                                text: modelData.mode
                                color: Theme.dim
                                font.pixelSize: Theme.smallSize
                            }
                            Text {
                                width: 70
                                text: modelData.players + "/" + modelData.maxPlayers
                                color: Theme.dim
                                font.pixelSize: Theme.smallSize
                            }
                            Text {
                                text: modelData.note !== "" ? modelData.note : modelData.ping + " ms"
                                color: modelData.ok ? Theme.good : Theme.bad
                                font.pixelSize: Theme.smallSize
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                list.currentIndex = index;
                                if (modelData.ok)
                                    address.text = modelData.address;
                            }
                            onDoubleClicked: if (modelData.ok)
                                launcher.joinServer(modelData.address)
                        }
                    }
                }
                Text {
                    anchors.centerIn: parent
                    visible: launcher.servers.length === 0
                    text: launcher.scanning ? "Asking the directory…" : "No server answered."
                    color: Theme.dim
                    font.pixelSize: Theme.bodySize
                }
            }
        }

        Text {
            width: parent.width
            wrapMode: Text.WordWrap
            text: launcher.status
            color: Theme.accent
            font.pixelSize: Theme.smallSize
            visible: text !== ""
        }

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: Theme.gap
            UiButton {
                text: "BACK"
                onClicked: launcher.screen = launcher.inMatch ? "pause" : "start"
            }
            UiButton {
                text: root.face === 0 ? "START" : "JOIN"
                primary: true
                enabledLook: !launcher.busy
                onClicked: root.face === 0 ? launcher.startOffline() : launcher.joinServer(address.text)
            }
        }
    }
}
