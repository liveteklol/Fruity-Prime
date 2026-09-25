import QtQuick

// The screens, drawn inside the game window over whatever is behind them:
// the room flying its intro on the front screen, the match during a pause.
Item {
    id: root
    // Nothing is drawn during a match: the HUD is the game's own.
    visible: launcher.screen !== ""

    // A wash over the picture, so the screens read against any room. The
    // pause menu takes a lighter one: the match it exists to keep visible
    // must show through.
    Rectangle {
        anchors.fill: parent
        color: Theme.ink
        opacity: launcher.screen === "pause" ? 0.55 : 0.78
    }

    Loader {
        anchors.fill: parent
        sourceComponent: launcher.screen === "start" ? startScreen
            : launcher.screen === "play" ? playScreen
            : launcher.screen === "settings" ? settingsScreen
            : launcher.screen === "pause" ? pauseScreen : null
    }

    Component { id: startScreen; StartScreen {} }
    Component { id: playScreen; PlayScreen {} }
    Component { id: settingsScreen; SettingsScreen {} }
    Component { id: pauseScreen; PauseScreen {} }

    Text {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 12
        visible: launcher.screen === "start"
        text: "C++ port"
        color: Theme.dim
        font.pixelSize: Theme.smallSize
    }
}
