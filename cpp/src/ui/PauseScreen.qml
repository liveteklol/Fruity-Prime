import QtQuick

// The pause menu, over the match itself: every entry is an action, so there
// is no yes and no to answer and no heading to write.
Item {
    Column {
        anchors.centerIn: parent
        spacing: Theme.gap
        width: 300

        UiButton {
            width: parent.width
            text: "RESUME"
            primary: true
            onClicked: launcher.resume()
        }
        UiButton {
            width: parent.width
            text: launcher.online ? (launcher.spectating ? "REJOIN THE MATCH" : "SPECTATE") : "SPECTATE"
            visible: launcher.online
            onClicked: launcher.toggleSpectating()
        }
        UiButton {
            width: parent.width
            text: "FULLSCREEN"
            onClicked: launcher.toggleFullscreen()
        }
        UiButton {
            width: parent.width
            text: "SETTINGS"
            onClicked: launcher.screen = "settings"
        }
        UiButton {
            width: parent.width
            text: "LEAVE MATCH"
            onClicked: launcher.leaveMatch()
        }
        UiButton {
            width: parent.width
            text: "QUIT"
            onClicked: launcher.quit()
        }
    }
}
