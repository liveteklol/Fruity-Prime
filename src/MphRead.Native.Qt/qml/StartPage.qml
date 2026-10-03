import QtQuick

// StartScreen: the wordmark over the backdrop, Play/Settings/Quit along the
// foot between the profile chip and the support mark.
FocusScope {
    id: page
    signal play()
    signal settings()
    signal quit()

    readonly property real em: Theme.emFor(width, height)
    // StartScreen.BarTurnsWidth: narrower than this the bar stacks.
    readonly property bool column: width < 470

    Backdrop { anchors.fill: parent }

    Column {
        anchors.centerIn: parent
        spacing: 0
        DeckWordmark {
            anchors.horizontalCenter: parent.horizontalCenter
            em: page.em
            sizeEms: Theme.phone ? (page.width > page.height ? 4.4 : 5.2) : 7.6
        }
        Text {
            id: subtitle
            anchors.horizontalCenter: parent.horizontalCenter
            readonly property int fontSize: Math.max(8, Math.round(page.em * 0.82))
            topPadding: Math.round(page.em * 0.82 * 1.4) - 10
            text: "METROID PRIME HUNTERS  ·  REBORN"
            font.family: Theme.pixel
            font.pixelSize: fontSize
            color: Theme.textDim
        }
    }

    // The foot: chip | bar | heart, bottom-aligned; narrower than
    // BarTurnsWidth the chip, then the bar, stack full width and the heart
    // moves to the top-left corner.
    Item {
        id: foot
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        anchors.leftMargin: page.column ? 14 : 26
        anchors.rightMargin: page.column ? 14 : 26
        anchors.bottomMargin: page.column ? 18 : 24
        height: page.column ? chip.height + 9 + bar.height : Math.max(chip.height, bar.height, heart.height)

        DeckChip {
            id: chip
            y: page.column ? 0 : foot.height - height
            width: page.column ? foot.width : implicitWidth
            height: implicitHeight
            key: "Profile"
            value: shell.playerName
        }
        Grid {
            id: bar
            readonly property real columnWidth: Math.max(160, Math.min(320, page.width - 48))
            columns: page.column ? 1 : 3
            spacing: page.column ? 9 : 12
            y: foot.height - height
            // Centred in the grid's middle column, between chip and heart.
            x: page.column ? Theme.roundEven((foot.width - width) / 2)
                           : chip.width + Theme.roundEven((foot.width - chip.width - heart.width - width) / 2)
            DeckButton {
                id: playButton
                text: "PLAY"; face: Theme.blue; em: page.em; idle: true
                width: page.column ? bar.columnWidth : implicitWidth
                focus: true
                KeyNavigation.right: settingsButton; KeyNavigation.down: page.column ? settingsButton : null
                onClicked: page.play()
            }
            DeckButton {
                id: settingsButton
                text: "SETTINGS"; face: Theme.brass; em: page.em
                width: page.column ? bar.columnWidth : implicitWidth
                KeyNavigation.left: playButton; KeyNavigation.right: quitButton
                KeyNavigation.up: page.column ? playButton : null; KeyNavigation.down: page.column ? quitButton : null
                onClicked: page.settings()
            }
            DeckButton {
                id: quitButton
                text: "QUIT"; face: Theme.rust; em: page.em
                width: page.column ? bar.columnWidth : implicitWidth
                KeyNavigation.left: settingsButton; KeyNavigation.right: heart
                KeyNavigation.up: page.column ? settingsButton : null
                onClicked: page.quit()
            }
        }
        DeckButton {
            id: heart
            visible: !page.column
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            face: Theme.rust; em: page.em
            sizeEms: 1.55; padXEms: 0.9; padYEms: 0.7; lip: 5
            heart: true
            tip: "Support this project <3"
            KeyNavigation.left: quitButton
            onClicked: shell.openSupport()
        }
    }
    DeckButton {
        id: heartCorner
        visible: page.column
        x: 14; y: 14
        face: Theme.rust; em: page.em
        sizeEms: 1.55; padXEms: 0.9; padYEms: 0.7; lip: 5
        heart: true
        tip: "Support this project <3"
        onClicked: shell.openSupport()
    }

    // The version line; with an update waiting it can be pressed.
    Text {
        id: versionLine
        anchors.right: parent.right; anchors.top: parent.top
        anchors.rightMargin: 24; anchors.topMargin: 18
        text: shell.version
        font.family: Theme.pixel; font.pixelSize: 12
        color: shell.versionColour
        activeFocusOnTab: shell.updatable
        MouseArea {
            anchors.fill: parent
            enabled: shell.updatable
            cursorShape: shell.updatable ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: shell.updateNow()
        }
        Keys.onReturnPressed: shell.updateNow()
        Keys.onSpacePressed: shell.updateNow()
    }
    Component.onCompleted: {
        shell.startUpdateCheck()
        shell.refreshVersionLine()
    }

    Keys.onPressed: event => {
        if (event.key === Qt.Key_Left || event.key === Qt.Key_Right || event.key === Qt.Key_Tab)
            Theme.keyboardDriving = true
    }
    Keys.onEscapePressed: page.quit()
}
