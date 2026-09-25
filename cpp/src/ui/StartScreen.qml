import QtQuick

// The front screen: the wordmark over what can be done.
Item {
    id: root

    Column {
        anchors.centerIn: parent
        spacing: Theme.gap
        width: 280

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "FRUITY PRIME"
            color: Theme.accent
            font.pixelSize: Theme.titleSize
            font.bold: true
            bottomPadding: Theme.gap
        }
        UiButton {
            width: parent.width
            text: launcher.inMatch ? "RESUME" : "PLAY"
            primary: true
            onClicked: launcher.inMatch ? launcher.resume() : launcher.screen = "play"
        }
        UiButton {
            width: parent.width
            text: "SETTINGS"
            onClicked: launcher.screen = "settings"
        }
        UiButton {
            width: parent.width
            text: "QUIT"
            onClicked: launcher.quit()
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: launcher.status
            color: Theme.dim
            font.pixelSize: Theme.smallSize
            visible: text !== ""
        }
    }
}
