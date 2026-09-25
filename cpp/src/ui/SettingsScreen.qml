import QtQuick
import QtQuick.Controls.Basic

// Settings: three pages, as the C#'s has -- Game (display, audio, rules),
// Controls (mouse, pad, keys) and Player (name, hunter, servers).
Item {
    id: root
    property int page: 0
    // The row waiting for a key: "" for none.
    property string listening: ""

    focus: true
    Keys.onPressed: function (event) {
        if (root.listening === "")
            return;
        event.accepted = true;
        if (event.key === Qt.Key_Escape) {
            root.listening = "";
            return;
        }
        launcher.setBinding(root.listening, event.key, 0, 0);
        root.listening = "";
    }

    Column {
        anchors.centerIn: parent
        width: Theme.wellNarrow
        spacing: Theme.gap

        Row {
            spacing: Theme.gap
            UiButton {
                text: "GAME"
                primary: root.page === 0
                onClicked: root.page = 0
            }
            UiButton {
                text: "CONTROLS"
                primary: root.page === 1
                onClicked: root.page = 1
            }
            UiButton {
                text: "PLAYER"
                primary: root.page === 2
                onClicked: root.page = 2
            }
        }

        Rectangle {
            width: parent.width
            height: 340
            color: "transparent"

            // ---- game
            Flickable {
                anchors.fill: parent
                visible: root.page === 0
                contentHeight: gamePage.height
                clip: true
                Column {
                    id: gamePage
                    width: parent.width
                    UiRow {
                        label: "Fullscreen"
                        Switch {
                            checked: settings.get("display.fullscreen")
                            onToggled: {
                                settings.set("display.fullscreen", checked);
                                launcher.applySettings();
                            }
                        }
                    }
                    UiRow {
                        label: "Field of view"
                        Row {
                            spacing: 8
                            Slider {
                                width: 200
                                from: 60
                                to: 120
                                stepSize: 1
                                value: settings.get("display.fov")
                                onMoved: {
                                    settings.set("display.fov", Math.round(value));
                                    launcher.applySettings();
                                }
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: settings.get("display.fov") + "°"
                                color: Theme.dim
                                font.pixelSize: Theme.bodySize
                            }
                        }
                    }
                    UiRow {
                        label: "Pro mode HUD"
                        Switch {
                            checked: settings.get("display.proHud")
                            onToggled: {
                                settings.set("display.proHud", checked);
                                launcher.applySettings();
                            }
                        }
                    }
                    UiRow {
                        label: "Match intro"
                        Switch {
                            checked: settings.get("display.intro")
                            onToggled: settings.set("display.intro", checked)
                        }
                    }
                    UiRow {
                        label: "Sound effects"
                        Slider {
                            width: 200
                            from: 0
                            to: 100
                            stepSize: 5
                            value: settings.get("audio.sfx")
                            onMoved: {
                                settings.set("audio.sfx", Math.round(value));
                                launcher.applySettings();
                            }
                        }
                    }
                    UiRow {
                        label: "Music"
                        Slider {
                            width: 200
                            from: 0
                            to: 100
                            stepSize: 5
                            value: settings.get("audio.music")
                            onMoved: {
                                settings.set("audio.music", Math.round(value));
                                launcher.applySettings();
                            }
                        }
                    }
                }
            }

            // ---- controls
            Column {
                anchors.fill: parent
                visible: root.page === 1
                spacing: 4
                Row {
                    width: parent.width
                    spacing: Theme.gap
                    UiRow {
                        width: 400
                        label: "Mouse sensitivity"
                        Slider {
                            width: 180
                            from: 0.25
                            to: 3
                            value: settings.get("mouse.sensitivity")
                            onMoved: {
                                settings.set("mouse.sensitivity", value);
                                launcher.applySettings();
                            }
                        }
                    }
                    UiRow {
                        width: 200
                        label: "Invert Y"
                        Switch {
                            checked: settings.get("mouse.invertY")
                            onToggled: {
                                settings.set("mouse.invertY", checked);
                                launcher.applySettings();
                            }
                        }
                    }
                }
                Rectangle {
                    width: parent.width
                    height: parent.height - 90
                    color: Theme.panel
                    radius: Theme.radius
                    border.width: 1
                    border.color: Theme.edge
                    ListView {
                        anchors.fill: parent
                        anchors.margins: 6
                        clip: true
                        model: launcher.bindings
                        delegate: Item {
                            required property var modelData
                            width: ListView.view.width
                            height: 30
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.left: parent.left
                                anchors.leftMargin: 8
                                text: modelData.title
                                color: Theme.text
                                font.pixelSize: Theme.smallSize
                            }
                            UiButton {
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.right: parent.right
                                implicitWidth: 160
                                implicitHeight: 26
                                text: root.listening === modelData.action ? "press a key…" : modelData.label
                                primary: root.listening === modelData.action
                                onClicked: {
                                    root.listening = modelData.action;
                                    root.forceActiveFocus();
                                }
                            }
                        }
                    }
                }
                Row {
                    spacing: Theme.gap
                    UiButton {
                        text: "RESET KEYS"
                        onClicked: launcher.resetBindings()
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Escape cancels a rebind."
                        color: Theme.dim
                        font.pixelSize: Theme.smallSize
                    }
                }
            }

            // ---- player
            Column {
                anchors.fill: parent
                visible: root.page === 2
                UiRow {
                    label: "Name"
                    TextField {
                        width: 220
                        text: settings.get("player.name")
                        color: Theme.text
                        background: Rectangle {
                            color: Theme.panel
                            radius: Theme.radius
                            border.width: 1
                            border.color: Theme.edge
                        }
                        onEditingFinished: settings.set("player.name", text)
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
                    label: "Suit"
                    UiChoice {
                        values: ["1", "2", "3", "4"]
                        valueWidth: 80
                        index: settings.get("player.suit")
                        onPicked: function (i) {
                            settings.set("player.suit", i);
                        }
                    }
                }
                UiRow {
                    label: "Server directory"
                    TextField {
                        width: 260
                        text: settings.get("online.directory")
                        color: Theme.text
                        background: Rectangle {
                            color: Theme.panel
                            radius: Theme.radius
                            border.width: 1
                            border.color: Theme.edge
                        }
                        onEditingFinished: settings.set("online.directory", text)
                    }
                }
                UiRow {
                    label: "Gamepad"
                    Switch {
                        checked: settings.get("pad.enabled")
                        onToggled: {
                            settings.set("pad.enabled", checked);
                            launcher.applySettings();
                        }
                    }
                }
                UiRow {
                    label: "Aim assist"
                    Switch {
                        checked: settings.get("pad.aimAssist")
                        onToggled: {
                            settings.set("pad.aimAssist", checked);
                            launcher.applySettings();
                        }
                    }
                }
            }
        }

        UiButton {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "DONE"
            primary: true
            onClicked: {
                launcher.applySettings();
                launcher.screen = launcher.inMatch ? "pause" : "start";
            }
        }
    }
}
