import QtQuick
import FruityPrime.Launcher

// SettingsView: Display, Audio, Controls (Keyboard, Gamepad, Stylus),
// Profile and Credits, one scrolling page each; Save, or Apply over a match.
Page {
    id: page
    property var nav
    // The page and the Controls sub-page to open on.
    property int section: 0
    property int subsection: 0

    widthEms: 44
    fill: true
    strip: tabs

    SettingsModel {
        id: settings
        inGame: page.overGame
        onClosed: saved => { if (page.nav) page.nav.pop() }
        // A key row handed over: the gamepad page, that pad row focused.
        onPadRowRequested: row => {
            tabs.index = 2
            subTabs.index = 1
            Qt.callLater(() => {
                // The new page's rows are placed now, not at the next polish.
                pageRows.forceLayout()
                column.forceLayout()
                const item = pageRows.focusRow(row)
                if (item)
                    shell.reveal(item)
            })
        }
        // InGameMenu: over a match the menus step aside for the placement.
        onStylusPlacementRequested: if (page.overGame && page.nav) page.nav.reset()
        onGameFilesRequested: {
            if (page.nav) {
                page.nav.pop()
                page.nav.openSetup()
            }
        }
    }

    Tabs {
        id: tabs
        names: ["Display", "Audio", "Controls", "Profile", "Credits"]
        index: page.section
        onChanged: flick.contentY = 0
    }

    body: Item {
        Flickable {
            id: flick
            anchors.fill: parent
            contentWidth: width
            contentHeight: column.height
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            Column {
                id: column
                width: flick.width
                spacing: 2
                Item {
                    visible: tabs.index === 2
                    width: parent.width
                    height: subTabs.height + 8
                    Tabs {
                        id: subTabs
                        padTabs: false
                        x: Theme.roundEven((parent.width - width) / 2)
                        names: ["Keyboard", "Gamepad", "Stylus"]
                        index: page.subsection
                        onChanged: flick.contentY = 0
                    }
                }
                SettingsRows {
                    id: pageRows
                    width: parent.width
                    settings: settings
                    model: [settings.display, settings.audio,
                            [settings.keyboard, settings.gamepad, settings.stylus][subTabs.index],
                            settings.profile, settings.credits][tabs.index]
                }
            }
            WheelHandler {
                enabled: settings.listening
                onWheel: event => {
                    settings.wheel(event.angleDelta.y > 0)
                    event.accepted = true
                }
            }
        }
        ThinScroll { flick: flick }
    }

    no: Mark { shape: "cancel"; label: "cancel"; onClicked: settings.cancel() }
    yes: Mark { shape: "accept"; label: page.overGame ? "apply" : "save"; onClicked: settings.save() }
    // The save error floats over the foot, as SettingsView docks it.
    Note {
        visible: settings.error.length > 0
        z: 2
        width: Math.min(implicitWidth, page.innerWidth)
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 28 + 38
        text: settings.error
        color: Theme.warm
        horizontalAlignment: Text.AlignHCenter
    }

    // While a key row listens every key is its answer, Escape included.
    Keys.priority: Keys.BeforeItem
    Keys.onPressed: event => {
        if (settings.listening) {
            settings.pressKey(event.key, event.nativeScanCode, event.nativeVirtualKey, event.modifiers, event.text)
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_Escape) {
            if (!settings.escape())
                settings.cancel()
            event.accepted = true
        }
    }
    Component.onCompleted: tabs.children[0].forceActiveFocus()
}
