import FruityPrime.Launcher
import QtQuick

// The menus' root: the front screen or the pause menu under a stack of
// screens, as StartScreen and InGameMenu keep one. Only the top of the stack
// exists; a closed menu leaves nothing in the scene graph.
Item {
    id: root
    // [{ url, props }], the last one showing.
    property var stack: []
    readonly property bool stacked: stack.length > 0

    // The captures hold everything still and switch the phone curve on.
    property bool still: ShellHost.backdropSuspended
    property bool phone: Qt.platform.os === "android" || Qt.platform.os === "ios"
    // Qt on Android sizes this view's root in physical pixels, and what its
    // device pixel ratio then does with that varies: at 1 a 440 dpi phone
    // draws the desktop's 1/96-inch layout at about a third of its size, and
    // at the screen's 2.75 the root is 2.75 times wider than the screen and
    // the picture is cropped and zoomed. Either way the menus are laid out in
    // a stage of the size the screen has in 1/160-inch dp (what the phone
    // curve in Theme is written for), and the stage is drawn at whatever
    // scale the ratio leaves to reach the view's pixels. Off Android the
    // density is 1 and the stage is the view: the root is already in
    // logical pixels and Qt Quick draws it at the window's device pixel
    // ratio, so dividing by that ratio again drew the menus at half size in
    // the top-left quarter of a 4K screen at 200%.
    readonly property real density: ShellHost.deviceDensity
    readonly property real uiScale: density
        / (Qt.platform.os === "android" ? Math.max(1, Screen.devicePixelRatio) : 1)
    Item {
        id: stage
        width: root.width / root.density
        height: root.height / root.density
        scale: root.uiScale
        transformOrigin: Item.TopLeft
    }
    // What the base screen is asked to show (the end panel's tab).
    property var baseProps: ({})
    Binding { target: Theme; property: "still"; value: root.still }
    Binding { target: Theme; property: "phone"; value: root.phone }
    Binding { target: Theme; property: "em"; value: Theme.emFor(stage.width, stage.height) }

    function push(url, props) {
        stack = stack.concat([{ url: url, props: props || {} }])
    }
    function pop() {
        stack = stack.slice(0, stack.length - 1)
        if (!stacked && ShellHost.page === "pause" && !base.item)
            ShellHost.resume()
    }
    function reset() {
        stack = []
    }
    // Replace everything with one screen: the captures' way in.
    function only(url, props) {
        stack = [{ url: url, props: props || {} }]
    }

    // StartScreen's ways out of the front screen.
    function openPlay() {
        if (!ShellHost.gameFilesReady) {
            openSetup()
            return
        }
        push("PlayPage.qml", { face: 0 })
    }
    function openSettings(overGame) {
        push("SettingsPage.qml", { overGame: !!overGame })
    }
    function openSetup() {
        push("SetupPage.qml", {})
    }
    function openCreateServer() {
        push("CreateServerPage.qml", {})
    }
    function openLobby() {
        push("LobbyPage.qml", {})
    }
    // The lobby closed: back to the screen under it, told why.
    function lobbyClosed(reason) {
        stack = stack.slice(0, stack.length - 1)
        if (stack.length > 0 && reason.length > 0) {
            const last = stack[stack.length - 1]
            stack = stack.slice(0, stack.length - 1).concat([{ url: last.url, props: Object.assign({}, last.props, { endedReason: reason }) }])
        }
    }
    function openVote() {
        const why = ShellHost.whyNotVoting()
        if (why.length > 0) {
            ShellHost.systemMessage(why)
            ShellHost.resume()
            return
        }
        push("PlayPage.qml", { face: 4, overGame: true })
    }
    function askToQuit() {
        push("ConfirmPage.qml", { question: "Quit " + ShellHost.brand + "?", yesAction: () => ShellHost.quit() })
    }

    Connections {
        target: ShellHost
        function onBackRequested() {
            if (root.stacked) root.pop()
            else ShellHost.quit()
        }
        function onPageChanged() {
            root.reset()
            if (ShellHost.page === "front" && !ShellHost.gameFilesReady)
                root.openSetup()
        }
        function onLobbyOpened() { root.openLobby() }
        function onKeyboardDriving() { Theme.keyboardDriving = true }
        function onScreenRequested(url, props) {
            if (url.length > 0) {
                root.only(url, props)
            } else {
                root.reset()
                root.baseProps = props
            }
        }
    }

    Loader {
        id: base
        parent: stage
        anchors.fill: parent
        visible: !root.stacked && !classicMenu.running
        enabled: visible
        focus: !root.stacked && !classicMenu.running
        sourceComponent: ShellHost.page === "front" ? start
                       : ShellHost.page === "pause" ? pause
                       : ShellHost.page === "end" ? end
                       : null
        onLoaded: if (!root.stacked) item.forceActiveFocus()
    }
    Component {
        id: start
        StartPage {
            focus: true
            onPlay: root.openPlay()
            onSettings: root.openSettings(false)
            onQuit: root.askToQuit()
        }
    }
    Component {
        id: pause
        PausePage {
            focus: true
            onSettings: root.openSettings(true)
            onVoteMap: root.openVote()
        }
    }
    Component { id: end; EndPanel { focus: true; hunterTab: !!root.baseProps.hunterTab } }

    Loader {
        id: top
        parent: stage
        anchors.fill: parent
        focus: root.stacked
        readonly property var entry: root.stacked ? root.stack[root.stack.length - 1] : null
        onEntryChanged: {
            if (entry) {
                const props = Object.assign({ nav: root }, entry.props)
                setSource(entry.url, props)
            } else {
                source = ""
                if (base.item)
                    base.item.forceActiveFocus()
            }
        }
        onLoaded: item.forceActiveFocus()
    }

    // Debug: the DS game's own title and menus in place of the front
    // screen, from the player's files (Mods/ClassicMenu, after Second Hunt).
    // MULTIPLAYER there opens Fruity Prime's own multiplayer screen.
    property bool classic: false
    ClassicMenu {
        id: classicMenu
        parent: stage
        anchors.fill: parent
        z: 500
        visible: running
        running: root.classic && ShellHost.page === "front" && !root.stacked && ShellHost.gameFilesReady
        focus: running
        onRunningChanged: {
            if (!running && error.length > 0) {
                root.classic = false
                ShellHost.systemMessage("The DS menus could not run: " + error)
            }
        }
        onMultiplayerRequested: {
            root.classic = false
            root.openPlay()
        }
        onAdventureRequested: {
            root.classic = false
            root.openPlay()
        }
        onQuitRequested: ShellHost.quit()
        Component.onCompleted: if (scripted) root.classic = true
    }
    Rectangle {
        id: classicSwitch
        parent: stage
        z: 600
        visible: ShellHost.page === "front" && !root.stacked && ShellHost.gameFilesReady
            && ShellHost.startupState === "FrontReady"
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 8
        width: classicLabel.implicitWidth + 16
        height: classicLabel.implicitHeight + 8
        radius: 4
        color: classicArea.containsMouse ? "#c0303848" : "#a0181c24"
        border.color: "#60e8ecf4"
        Text {
            id: classicLabel
            anchors.centerIn: parent
            color: "#e8ecf4"
            font.pixelSize: 12
            text: root.classic ? "DEBUG: FRUITY UI" : "DEBUG: DS UI"
        }
        MouseArea {
            id: classicArea
            anchors.fill: parent
            hoverEnabled: true
            onClicked: root.classic = !root.classic
        }
    }

    ControllerKeyboard {
        id: controllerKeyboard
        parent: stage
        Component.onCompleted: Theme.keyboard = controllerKeyboard
    }

    // Until the host says the front screen is ready, something opaque is
    // drawn: a host that is still starting, or that failed, must never look
    // like a black window. Plain primitives only -- no images, no settings,
    // no game files -- so it draws whatever else has not loaded yet.
    Rectangle {
        id: boot
        parent: stage
        anchors.fill: parent
        visible: ShellHost.startupState !== "FrontReady"
        color: "#10141c"
        z: 1000
        // Swallow input meant for the pages underneath.
        MouseArea { anchors.fill: parent; enabled: boot.visible }
        Column {
            anchors.centerIn: parent
            width: Math.min(parent.width - 48, 640)
            spacing: 16
            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                color: "#e8ecf4"
                font.pixelSize: Math.max(18, Math.round(boot.height / 24))
                text: ShellHost.startupState === "Failed"
                    ? ShellHost.brand + " could not start"
                    : "Starting " + ShellHost.brand + "…"
            }
            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                color: "#a8b0c0"
                font.pixelSize: Math.max(12, Math.round(boot.height / 40))
                visible: text.length > 0
                text: ShellHost.startupState === "Failed" ? ShellHost.startupError : ""
            }
        }
    }
}
